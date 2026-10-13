#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Ported and adapted from MwTrackSlot of GGBOX (an earlier port of these firmwares by the same author).
// Drives one FM-1 firmware instance under real-time constraints:
// - Audio thread: render inside the slot in 64-frame output sub-blocks;
// - HOLD_MS gating: button-mask and key-mask events applied in order >= 24 ms apart by fw.nowMs();
// - Encoders, master pot, MIDI applied immediately;
// - Dekker park/unpark handshake (lock-free on audio thread);
// - LED atomics, triple-buffer screen, seqlock ui_state, save/snapshot requests, heartbeat.

#include "IFirmware.h"
#include "../dsp/Resampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

namespace fm1deck {

static_assert(std::atomic<float>::is_always_lock_free, "atomic<float> must be lock-free");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "atomic<uint64_t> must be lock-free");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomic<uint32_t> must be lock-free");
static_assert(std::atomic<int32_t>::is_always_lock_free, "atomic<int32_t> must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free, "atomic<bool> must be lock-free");
static_assert(std::atomic<int>::is_always_lock_free, "atomic<int> must be lock-free");
static_assert(std::atomic<IFirmware*>::is_always_lock_free, "atomic<IFirmware*> must be lock-free");
static_assert(std::atomic<uint8_t*>::is_always_lock_free, "atomic<uint8_t*> must be lock-free");

class FirmwareSlot {
public:
    static constexpr int kSubBlock = 64;
    static constexpr int kLedPeriodBlocks = 12;      // LEDs and ui_state about every 16 ms at 48 kHz
    static constexpr int kScreenPeriodBlocks = 20;   // Screen at most about every 27 ms
    static constexpr int kHoldMs = 24;               // Contact debounce time in device time (worklet.js)
    static constexpr int kScreenPixels = 240 * 240;

    FirmwareSlot() {
        for (auto& l : leds_) l.store(0, std::memory_order_relaxed);
        screen_ = std::make_unique<uint16_t[]>(static_cast<size_t>(kScreenPixels) * 3);
    }
    ~FirmwareSlot() = default;

    FirmwareSlot(const FirmwareSlot&) = delete;
    FirmwareSlot& operator=(const FirmwareSlot&) = delete;

    // ================= Control side (FirmwareHost, JNI) =================

    // After park() returns, the audio thread is outside the firmware and stays out until unpark().
    void park() noexcept {
        parked_.store(true, std::memory_order_seq_cst);
        while (busy_.load(std::memory_order_seq_cst)) {
            spinPause();
        }
    }

    void unpark() noexcept {
        parked_.store(false, std::memory_order_seq_cst);
    }

    bool parked() const noexcept {
        return parked_.load(std::memory_order_relaxed);
    }

    void startFadeOut(int ms = 20) noexcept {
        fadeDurationMs_.store(ms > 0 ? ms : 20, std::memory_order_relaxed);
        if (currentGain_.load(std::memory_order_relaxed) <= 0.0f) {
            fadeOutDone_.store(true, std::memory_order_release);
        } else {
            fadeOutDone_.store(false, std::memory_order_release);
        }
        fadeTargetGain_.store(0.0f, std::memory_order_release);
    }

    bool isFadeOutDone() const noexcept {
        return fadeOutDone_.load(std::memory_order_acquire) || currentGain_.load(std::memory_order_acquire) <= 0.0f;
    }

    void startFadeIn(int ms = 20) noexcept {
        fadeDurationMs_.store(ms > 0 ? ms : 20, std::memory_order_relaxed);
        fadeOutDone_.store(false, std::memory_order_release);
        fadeTargetGain_.store(1.0f, std::memory_order_release);
    }

    // Only while parked. Generation change signals audio thread to reset resampler and state.
    void setFirmware(IFirmware* fw) noexcept {
        {
            std::lock_guard<std::mutex> lock(maskMutex_);
            lastButtons_ = 0;
            lastKeys_ = 0;
        }
        fw_.store(fw, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }

    IFirmware* firmware() const noexcept {
        return fw_.load(std::memory_order_acquire);
    }

    // Combined panel change: enqueues only the mask(s) that actually changed.
    // When both change, enqueues buttons first, then keys.
    bool pushPanel(uint32_t buttons, uint32_t keys) {
        std::lock_guard<std::mutex> lock(maskMutex_);
        bool ok = true;
        if (buttons != lastButtons_) {
            const uint32_t head = maskHead_.load(std::memory_order_relaxed);
            const uint32_t tail = maskTail_.load(std::memory_order_acquire);
            if (head - tail < kMaskQueueSize) {
                maskQueue_[head & (kMaskQueueSize - 1)] = MaskEvent{false, buttons};
                maskHead_.store(head + 1, std::memory_order_release);
                lastButtons_ = buttons;
            } else {
                ok = false;
            }
        }
        if (keys != lastKeys_) {
            const uint32_t head = maskHead_.load(std::memory_order_relaxed);
            const uint32_t tail = maskTail_.load(std::memory_order_acquire);
            if (head - tail < kMaskQueueSize) {
                maskQueue_[head & (kMaskQueueSize - 1)] = MaskEvent{true, keys};
                maskHead_.store(head + 1, std::memory_order_release);
                lastKeys_ = keys;
            } else {
                ok = false;
            }
        }
        return ok;
    }

    // Panel mask events (buttons / keys), gated by HOLD_MS in order of arrival.
    bool pushButtons(uint32_t mask) {
        std::lock_guard<std::mutex> lock(maskMutex_);
        if (mask == lastButtons_) return true;
        const uint32_t head = maskHead_.load(std::memory_order_relaxed);
        const uint32_t tail = maskTail_.load(std::memory_order_acquire);
        if (head - tail >= kMaskQueueSize) return false;
        maskQueue_[head & (kMaskQueueSize - 1)] = MaskEvent{false, mask};
        maskHead_.store(head + 1, std::memory_order_release);
        lastButtons_ = mask;
        return true;
    }

    bool pushKeys(uint32_t mask) {
        std::lock_guard<std::mutex> lock(maskMutex_);
        if (mask == lastKeys_) return true;
        const uint32_t head = maskHead_.load(std::memory_order_relaxed);
        const uint32_t tail = maskTail_.load(std::memory_order_acquire);
        if (head - tail >= kMaskQueueSize) return false;
        maskQueue_[head & (kMaskQueueSize - 1)] = MaskEvent{true, mask};
        maskHead_.store(head + 1, std::memory_order_release);
        lastKeys_ = mask;
        return true;
    }

    // Encoders, master pot, MIDI: applied immediately at the start of sub-blocks.
    bool pushEnc(uint32_t role, int32_t steps) {
        std::lock_guard<std::mutex> lock(encMutex_);
        const uint32_t head = encHead_.load(std::memory_order_relaxed);
        const uint32_t tail = encTail_.load(std::memory_order_acquire);
        if (head - tail >= kEncQueueSize) return false;
        encQueue_[head & (kEncQueueSize - 1)] = EncEvent{role, steps};
        encHead_.store(head + 1, std::memory_order_release);
        return true;
    }

    void setMaster(uint32_t adc) noexcept {
        masterAdc_.store(adc & 0x3FFu, std::memory_order_relaxed);
        masterChanged_.store(true, std::memory_order_release);
    }

    bool pushMidi(uint32_t status, uint32_t d1, uint32_t d2) {
        std::lock_guard<std::mutex> lock(midiMutex_);
        const uint32_t head = midiHead_.load(std::memory_order_relaxed);
        const uint32_t tail = midiTail_.load(std::memory_order_acquire);
        if (head - tail >= kMidiQueueSize) return false;
        midiQueue_[head & (kMidiQueueSize - 1)] = MidiEvent{status, d1, d2};
        midiHead_.store(head + 1, std::memory_order_release);
        return true;
    }

    void setScreenWanted(bool wanted) noexcept {
        screenWanted_.store(wanted, std::memory_order_relaxed);
    }

    uint32_t copyScreen(uint16_t* out, uint32_t seen) noexcept {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t pub = screenPublished_.load(std::memory_order_acquire);
            const uint32_t frame = pub >> 2;
            if (frame == 0 || frame == seen) return frame;
            const int idx = static_cast<int>(pub & 3u);
            readingIndex_.store(idx, std::memory_order_seq_cst);
            if (writingIndex_.load(std::memory_order_seq_cst) == idx) {
                readingIndex_.store(-1, std::memory_order_release);
                continue;
            }
            std::memcpy(out, screen_.get() + static_cast<size_t>(idx) * kScreenPixels,
                        sizeof(uint16_t) * kScreenPixels);
            readingIndex_.store(-1, std::memory_order_release);
            return frame;
        }
        readingIndex_.store(-1, std::memory_order_release);
        return seen;
    }

    void ledsSnapshot(uint32_t out[FM1_LED_COUNT]) const noexcept {
        for (int i = 0; i < FM1_LED_COUNT; ++i) {
            out[i] = leds_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        }
    }

    bool readUiState(Fm1UiState* out) const noexcept {
        for (int attempt = 0; attempt < 5; ++attempt) {
            uint32_t s1 = uiSeq_.load(std::memory_order_acquire);
            if (s1 & 1u) continue;
            *out = uiState_;
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t s2 = uiSeq_.load(std::memory_order_relaxed);
            if (s1 == s2) return true;
        }
        return false;
    }

    uint32_t playing() const noexcept {
        return playing_.load(std::memory_order_relaxed);
    }

    uint64_t heartbeat() const noexcept {
        return heartbeat_.load(std::memory_order_acquire);
    }

    uint32_t requestSave() noexcept {
        return saveReq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    uint32_t saveDone() const noexcept {
        return saveDone_.load(std::memory_order_acquire);
    }
    int32_t saveResult() const noexcept {
        return saveResult_.load(std::memory_order_acquire);
    }

    uint32_t norWrites() const noexcept {
        return norWrites_.load(std::memory_order_acquire);
    }
    void publishNorWrites(uint32_t writes) noexcept {
        norWrites_.store(writes, std::memory_order_release);
    }

    uint32_t requestSnapshot(uint8_t* dst) noexcept {
        snapDst_.store(dst, std::memory_order_release);
        return snapReq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    uint32_t snapshotDone() const noexcept {
        return snapDone_.load(std::memory_order_acquire);
    }
    void cancelSnapshot() noexcept {
        snapDst_.store(nullptr, std::memory_order_release);
    }

    // ================= Audio thread =================

    bool live() const noexcept {
        return !parked_.load(std::memory_order_relaxed) && fw_.load(std::memory_order_relaxed) != nullptr;
    }

    /**
     * Called on the audio thread before each sub-block renders, with the live firmware, the sub-block's offset
     * into this process() call and its length: the host clock's bytes go in here (LinkSync).
     */
    using SubBlockHook = void (*)(void* ctx, IFirmware* fw, int offset, int n);

    // Renders up to numFrames in 64-frame sub-blocks.
    void process(int numFrames, float* outL, float* outR, int streamSampleRate,
                 SubBlockHook hook = nullptr, void* hookCtx = nullptr, int hookOffset = 0) noexcept {
        int done = 0;
        while (done < numFrames) {
            const int sub = std::min(kSubBlock, numFrames - done);
            hook_ = hook;
            hookCtx_ = hookCtx;
            hookOffset_ = hookOffset + done;
            renderSubBlock(sub, outL + done, outR + done, streamSampleRate);
            done += sub;
        }
        hook_ = nullptr;
    }

private:
    struct MaskEvent { bool isKeys; uint32_t mask; };
    struct EncEvent { uint32_t role; int32_t steps; };
    struct MidiEvent { uint32_t status; uint32_t d1; uint32_t d2; };

    static constexpr uint32_t kMaskQueueSize = 256;
    static constexpr uint32_t kEncQueueSize = 256;
    static constexpr uint32_t kMidiQueueSize = 256;
    static constexpr uint32_t kSnapChunk = 64u * 1024u;

    static void spinPause() noexcept {
#if defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
    }

    void renderSubBlock(int n, float* outL, float* outR, int streamSampleRate) noexcept {
        heartbeat_.fetch_add(1, std::memory_order_release);
        busy_.store(true, std::memory_order_seq_cst);

        IFirmware* fw = parked_.load(std::memory_order_seq_cst) ? nullptr : fw_.load(std::memory_order_acquire);
        if (!fw) {
            std::fill_n(outL, n, 0.0f);
            std::fill_n(outR, n, 0.0f);
            busy_.store(false, std::memory_order_release);
            return;
        }

        const uint32_t gen = generation_.load(std::memory_order_acquire);
        if (gen != seenGeneration_ || streamSampleRate != streamRate_) {
            seenGeneration_ = gen;
            streamRate_ = streamSampleRate;
            resampler_.reset(static_cast<double>(fw->sampleRate()), static_cast<double>(streamSampleRate));
            nextOkMs_ = 0;
            blockCounter_ = 0;
            lastDraws_ = 0;
            snapOffset_ = 0;
        }

        applyImmediateInputs(fw);
        if (hook_) hook_(hookCtx_, fw, hookOffset_, n);
        applyGatedInputs(fw);
        applySave(fw);

        const int need = resampler_.inputNeeded(n);
        if (need > 0) {
            fw->render(static_cast<uint32_t>(need), resampler_.inputTailL(), resampler_.inputTailR());
            resampler_.commitInput(need);
        }
        resampler_.process(n, outL, outR);

        const float targetGain = fadeTargetGain_.load(std::memory_order_acquire);
        const int ms = fadeDurationMs_.load(std::memory_order_relaxed);
        const int rate = streamRate_ > 0 ? streamRate_ : streamSampleRate;
        const int frames = std::max(64, (rate * (ms > 0 ? ms : 20)) / 1000);
        const float step = 1.0f / static_cast<float>(frames);

        float gain = currentGain_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            if (gain < targetGain) {
                gain = std::min(targetGain, gain + step);
            } else if (gain > targetGain) {
                gain = std::max(targetGain, gain - step);
                if (gain == 0.0f) {
                    fadeOutDone_.store(true, std::memory_order_release);
                }
            }
            outL[i] *= gain;
            outR[i] *= gain;
        }
        currentGain_.store(gain, std::memory_order_relaxed);
        if (gain == 0.0f && targetGain == 0.0f) {
            fadeOutDone_.store(true, std::memory_order_release);
        }

        // Re-check gated inputs: fw->render() advanced fw->nowMs(), so a tap release due now is applied!
        applyGatedInputs(fw);

        applySnapshot(fw);
        publishOutputs(fw);

        busy_.store(false, std::memory_order_release);
    }

    void applyImmediateInputs(IFirmware* fw) noexcept {
        // Encoders
        uint32_t encTail = encTail_.load(std::memory_order_relaxed);
        const uint32_t encHead = encHead_.load(std::memory_order_acquire);
        while (encTail != encHead) {
            const auto& e = encQueue_[encTail & (kEncQueueSize - 1)];
            ++encTail;
            fw->enc(e.role, e.steps);
        }
        encTail_.store(encTail, std::memory_order_release);

        // Master pot
        if (masterChanged_.exchange(false, std::memory_order_acquire)) {
            fw->master(masterAdc_.load(std::memory_order_relaxed));
        }

        // MIDI
        uint32_t midiTail = midiTail_.load(std::memory_order_relaxed);
        const uint32_t midiHead = midiHead_.load(std::memory_order_acquire);
        while (midiTail != midiHead) {
            const auto& m = midiQueue_[midiTail & (kMidiQueueSize - 1)];
            ++midiTail;
            fw->midi(m.status, m.d1, m.d2);
        }
        midiTail_.store(midiTail, std::memory_order_release);
    }

    void applyGatedInputs(IFirmware* fw) noexcept {
        const uint32_t now = fw->nowMs();
        uint32_t tail = maskTail_.load(std::memory_order_relaxed);
        const uint32_t head = maskHead_.load(std::memory_order_acquire);

        while (tail != head) {
            if (nextOkMs_ != 0 && static_cast<int32_t>(now - nextOkMs_) < 0) {
                break;
            }
            const auto& ev = maskQueue_[tail & (kMaskQueueSize - 1)];
            ++tail;
            if (ev.isKeys) {
                fw->keys(ev.mask);
            } else {
                fw->buttons(ev.mask);
            }
            nextOkMs_ = now + static_cast<uint32_t>(kHoldMs);
        }
        maskTail_.store(tail, std::memory_order_release);
    }

    void applySave(IFirmware* fw) noexcept {
        const uint32_t req = saveReq_.load(std::memory_order_acquire);
        if (req == saveDone_.load(std::memory_order_relaxed)) return;
        saveResult_.store(fw->saveNow(), std::memory_order_release);
        norWrites_.store(fw->norWrites(), std::memory_order_release);
        saveDone_.store(req, std::memory_order_release);
    }

    void applySnapshot(IFirmware* fw) noexcept {
        const uint32_t req = snapReq_.load(std::memory_order_acquire);
        if (req == snapDone_.load(std::memory_order_relaxed)) {
            snapOffset_ = 0;
            return;
        }
        uint8_t* dst = snapDst_.load(std::memory_order_acquire);
        const uint32_t size = fw->norSize();
        if (!dst || size == 0) {
            snapDone_.store(req, std::memory_order_release);
            return;
        }
        if (snapOffset_ == 0) {
            snapWrites_ = fw->norWrites();
        } else if (fw->norWrites() != snapWrites_) {
            snapOffset_ = 0;
            snapWrites_ = fw->norWrites();
        }
        const uint32_t n = std::min(kSnapChunk, size - snapOffset_);
        std::memcpy(dst + snapOffset_, fw->nor() + snapOffset_, n);
        snapOffset_ += n;
        if (snapOffset_ >= size) {
            snapOffset_ = 0;
            snapDone_.store(req, std::memory_order_release);
        }
    }

    void publishOutputs(IFirmware* fw) noexcept {
        ++blockCounter_;
        if (blockCounter_ % kLedPeriodBlocks == 0) {
            uint32_t v[FM1_LED_COUNT];
            fw->leds(v);
            for (int i = 0; i < FM1_LED_COUNT; ++i) {
                leds_[static_cast<size_t>(i)].store(v[i], std::memory_order_relaxed);
            }
            playing_.store(fw->playing(), std::memory_order_relaxed);
            norWrites_.store(fw->norWrites(), std::memory_order_release);

            if (fw->caps() & FM1_CAP_UI_STATE) {
                uint32_t s = uiSeq_.load(std::memory_order_relaxed);
                uiSeq_.store(s + 1, std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_release);
                fw->uiState(&uiState_);
                uiSeq_.store(s + 2, std::memory_order_release);
            }
        }

        if (!screenWanted_.load(std::memory_order_relaxed) ||
            blockCounter_ - lastScreenBlock_ < kScreenPeriodBlocks) {
            return;
        }

        const uint32_t draws = fw->screenDraws();
        if (draws == lastDraws_) return;
        lastDraws_ = draws;
        lastScreenBlock_ = blockCounter_;

        const uint32_t pub = screenPublished_.load(std::memory_order_relaxed);
        const int published = static_cast<int>(pub & 3u);
        int idx = 0;
        for (; idx < 3; ++idx) {
            if (idx == published && (pub >> 2) != 0) continue;
            writingIndex_.store(idx, std::memory_order_seq_cst);
            if (readingIndex_.load(std::memory_order_seq_cst) != idx) break;
        }
        if (idx >= 3) {
            writingIndex_.store(-1, std::memory_order_release);
            return;
        }
        std::memcpy(screen_.get() + static_cast<size_t>(idx) * kScreenPixels, fw->screen(),
                    sizeof(uint16_t) * kScreenPixels);
        const uint32_t frame = (pub >> 2) + 1;
        screenPublished_.store((frame << 2) | static_cast<uint32_t>(idx), std::memory_order_release);
        writingIndex_.store(-1, std::memory_order_release);
    }

    // Ownership & lifecycle
    std::atomic<IFirmware*> fw_{nullptr};
    std::atomic<uint32_t> generation_{0};
    std::atomic<bool> parked_{true};
    std::atomic<bool> busy_{false};
    std::atomic<uint64_t> heartbeat_{0};

    // Mask events (gated)
    std::mutex maskMutex_;
    uint32_t lastButtons_{0};
    uint32_t lastKeys_{0};
    std::array<MaskEvent, kMaskQueueSize> maskQueue_{};
    std::atomic<uint32_t> maskHead_{0};
    std::atomic<uint32_t> maskTail_{0};

    // Immediate events
    std::mutex encMutex_;
    std::array<EncEvent, kEncQueueSize> encQueue_{};
    std::atomic<uint32_t> encHead_{0};
    std::atomic<uint32_t> encTail_{0};

    std::atomic<uint32_t> masterAdc_{700};
    std::atomic<bool> masterChanged_{false};

    std::mutex midiMutex_;
    std::array<MidiEvent, kMidiQueueSize> midiQueue_{};
    std::atomic<uint32_t> midiHead_{0};
    std::atomic<uint32_t> midiTail_{0};

    // Save & snapshot
    std::atomic<uint32_t> saveReq_{0};
    std::atomic<uint32_t> saveDone_{0};
    std::atomic<int32_t> saveResult_{0};
    std::atomic<uint32_t> norWrites_{0};

    std::atomic<uint32_t> snapReq_{0};
    std::atomic<uint32_t> snapDone_{0};
    std::atomic<uint8_t*> snapDst_{nullptr};

    // Screen
    std::atomic<bool> screenWanted_{false};
    std::unique_ptr<uint16_t[]> screen_;
    std::atomic<uint32_t> screenPublished_{0};
    std::atomic<int> writingIndex_{-1};
    std::atomic<int> readingIndex_{-1};

    // LEDs and outputs
    std::array<std::atomic<uint32_t>, FM1_LED_COUNT> leds_{};
    std::atomic<uint32_t> playing_{0};

    // UI state seqlock
    mutable std::atomic<uint32_t> uiSeq_{0};
    Fm1UiState uiState_{};

    // Audio thread state
    Resampler resampler_;
    SubBlockHook hook_ = nullptr;  // audio thread: this process() call's hook
    void* hookCtx_ = nullptr;
    int hookOffset_ = 0;
    uint32_t seenGeneration_{0};
    int streamRate_{0};
    uint32_t nextOkMs_{0};
    uint32_t lastDraws_{0};
    uint32_t blockCounter_{0};
    uint32_t lastScreenBlock_{0};
    uint32_t snapOffset_{0};
    uint32_t snapWrites_{0};

    // Smooth gain ramp (20 ms fade out / in)
    std::atomic<float> fadeTargetGain_{1.0f};
    std::atomic<int> fadeDurationMs_{20};
    std::atomic<bool> fadeOutDone_{false};
    std::atomic<float> currentGain_{1.0f};
};

} // namespace fm1deck
