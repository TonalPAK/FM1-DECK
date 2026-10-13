#pragma once
// SPDX-License-Identifier: GPL-3.0-only

#include "../fw/FirmwareHost.h"
#include "../fw/FirmwareSlot.h"
#include "CpuMeter.h"
#include "../dsp/master/MasterBus.h"
#include "LinkSync.h"

#include <array>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>

#if defined(__ANDROID__)
#include <oboe/Oboe.h>
#endif

namespace fm1deck {

struct StreamInfo {
    int32_t sampleRate = 48000;
    int32_t burst = 192;
    int32_t bufferSize = 384;
    int32_t capacity = 1920;
    int32_t format = 0;       // 1 = Float
    int32_t sharingMode = 0;  // 0 = Exclusive, 1 = Shared
    int32_t isExclusive = 0;
    int32_t xruns = 0;
    int32_t api = 0;          // 0 = AAudio, 1 = OpenSLES
};

class AudioEngine
#if defined(__ANDROID__)
    : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback
#endif
{
public:
    AudioEngine();
    ~AudioEngine()
#if defined(__ANDROID__)
        override
#endif
    ;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool start(int sampleRate = 48000, int bufferSize = 0);
    void stop();
    bool isRunning() const;

    bool reopenIfNeeded();

    StreamInfo streamInfo() const;
    CpuMeter::Metrics cpuMetrics() const { return cpuMeter_.computeMetrics(); }
    void resetCpuMetrics() noexcept { cpuMeter_.reset(); }
    float liveLoad() const noexcept { return cpuMeter_.liveLoad(); }
    float livePeak() const noexcept { return cpuMeter_.livePeak(); }

    // ---- the rack: kSlots firmware slots rendered side by side and summed into the master bus ----------------------
    // The panel (buttons, keys, encoders, MASTER, MIDI, screen, LEDs, ui_state) talks to the focused slot only.
    static constexpr int kSlots = 4;
    static bool validSlot(int i) noexcept { return i >= 0 && i < kSlots; }

    FirmwareSlot* slot(int i) noexcept { return validSlot(i) ? &slots_[static_cast<size_t>(i)] : nullptr; }
    FirmwareHost* host(int i) noexcept { return validSlot(i) ? hosts_[static_cast<size_t>(i)].get() : nullptr; }
    /** The focused slot: the one the panel plays and shows. */
    FirmwareSlot* slot() noexcept { return slot(focus()); }
    FirmwareHost* host() noexcept { return host(focus()); }
    int focus() const noexcept { return focus_.load(std::memory_order_acquire); }
    /** Control thread: hands the panel to another slot; whatever the panel held on the old one is released there. */
    void setFocus(int i);
    /** Screens are copied out of every loaded slot, so a focus change shows the new slot's screen at once. */
    void setScreenWanted(bool wanted) noexcept {
        for (auto& s : slots_) s.setScreenWanted(wanted);
    }

    /**
     * Mute and solo, a bit per slot: a muted slot keeps playing (in time) unheard; with any solo, only the soloed
     * slots are heard. Each slot's gain glides 10 ms to its target, so a switch does not click.
     */
    void setGate(uint32_t mute, uint32_t solo) noexcept {
        muteMask_.store(mute, std::memory_order_relaxed);
        soloMask_.store(solo, std::memory_order_relaxed);
    }

    /** Per slot: share of the callback budget its firmware took (smoothed) and its output peak (decaying). */
    struct SlotMeter { float load = 0.0f; float level = 0.0f; };
    SlotMeter slotMeter(int i) const noexcept {
        if (!validSlot(i)) return {};
        const auto& m = meters_[static_cast<size_t>(i)];
        return {m.load.load(std::memory_order_relaxed), m.level.load(std::memory_order_relaxed)};
    }

    dsp::MasterBus* masterBus() noexcept { return &bus_; }
    LinkSync* link() noexcept { return &link_; }
    /** Control thread: refresh Link's output latency from the stream (Oboe's estimate + the master bus). */
    void updateOutputLatency();

#if defined(__ANDROID__)
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;
#endif

private:
    struct LinkLane { LinkSync* link; int lane; };
    struct AtomicMeter { std::atomic<float> load{0.0f}; std::atomic<float> level{0.0f}; };

    std::array<FirmwareSlot, kSlots> slots_;
    std::array<std::unique_ptr<FirmwareHost>, kSlots> hosts_;
    std::array<LinkLane, kSlots> lanes_{};
    std::array<AtomicMeter, kSlots> meters_;
    std::array<float, kSlots> slotLoad_{};   // audio thread only
    std::array<float, kSlots> slotLevel_{};  // audio thread only
    std::atomic<int> focus_{0};
    std::atomic<uint32_t> muteMask_{0};
    std::atomic<uint32_t> soloMask_{0};
    std::array<float, kSlots> slotGain_{};   // audio thread only: the gate's glide
    std::mutex focusMutex_;
    CpuMeter cpuMeter_;
    dsp::MasterBus bus_;
    LinkSync link_;
    float busLoad_ = 0.0f;  // audio thread only: smoothed share of the callback budget

    mutable std::mutex streamMutex_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> reopenNeeded_{false};
    int targetSampleRate_{48000};
    int targetBufferSize_{0};

#if defined(__ANDROID__)
    std::shared_ptr<oboe::AudioStream> stream_;
#endif
};

} // namespace fm1deck
