#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// The FM1 DECK master bus (docs/MASTER-BUS.md): input -> dynamic saturation (xQ) -> spectral clipper -> look-ahead
// limiter <-> final clipper (xQ) -> output gain, auto gain, MIX against the latency-aligned dry, BYPASS crossfade,
// meters.
//
// Threads:
// - control thread: prepare() (stream stopped), setParam(), setQuality();
// - audio thread: process() only. It allocates nothing, takes no locks and does not log.
// Parameters travel through a lock-free SPSC queue drained at each 64-frame block. Changes of latency (quality,
// LIM_LOOKAHEAD, CLIP_POSITION) fade the bus out for 10 ms, re-prime, and fade back in. SPC_BANDS fades the spectral
// stage only.

#include "../DspCore.h"
#include "../MasterBusParams.h"
#include "../Meters.h"
#include "DynamicSaturator.h"
#include "FinalClipper.h"
#include "InputStage.h"
#include "LookaheadLimiter.h"
#include "SpectralClipper.h"

#include <array>
#include <atomic>
#include <mutex>

namespace fm1deck::dsp {

struct MasterParamInfo {
    int id;
    const char* key;
    const char* group;
    float min, max, def;
    const char* unit;
    char kind;  // 'C' continuous, 'S' stepped
};

inline const MasterParamInfo* masterParams(int* count) noexcept {
    static const MasterParamInfo table[] = {
#define FM1_X_INFO(id, key, group, mn, mx, def, unit, kind) {id, #key, #group, mn, mx, def, unit, #kind[0]},
        FM1_MASTER_PARAMS(FM1_X_INFO)
#undef FM1_X_INFO
    };
    *count = static_cast<int>(sizeof(table) / sizeof(table[0]));
    return table;
}

class MasterBus {
public:
    static constexpr int kBlock = 64;
    static constexpr int kMaxId = 64;
    static constexpr int kMeterCount = static_cast<int>(MasterMeterId::COUNT);
    /** Scope history: one entry per processed block (<= 64 frames), kScopeFields floats each. */
    static constexpr int kScopeLen = 1024;
    static constexpr int kScopeFields = 3;  // input peak dB, output peak dB, total gain reduction dB (<= 0)

    MasterBus() {
        for (auto& m : meters_) m.store(0.0f, std::memory_order_relaxed);
    }

    /** Control thread, audio stopped: allocate for this rate and apply the current parameter values. */
    void prepare(double rate) {
        rate_ = rate;
        input_.prepare(rate);
        sat_.prepare(rate);
        spc_.prepare(rate);
        lim_.prepare(rate);
        clip_.prepare(rate);
        const int maxLatency = 2 * 64 + static_cast<int>(std::ceil(0.005 * rate)) + 16;
        for (auto& d : dry_) d.prepare(maxLatency);
        outGain_.prepare(rate, 20.0f);
        mix_.prepare(rate, 20.0f);
        autoGain_.prepare(rate, 1000.0f);
        autoGain_.reset(1.0f);
        lufsIn_.prepare(rate);
        lufsOut_.prepare(rate);
        for (auto& t : outTp_) t.reset();
        rampStep_ = static_cast<float>(1.0 / (0.020 * rate));
        fadeStep_ = static_cast<float>(1.0 / (0.010 * rate));
        drainQueue();  // anything sent before the stream existed
        // Apply every value as if it had just arrived, then settle the smoothers.
        for (int i = 0; i < kMaxId; ++i) if (known_[static_cast<size_t>(i)]) apply(i, values_[static_cast<size_t>(i)], true);
        input_.settle(); sat_.settle(); spc_.settle(); lim_.settle();
        quality_ = requestedQuality_.load(std::memory_order_acquire);
        sat_.setFactor(quality_);
        clip_.setFactor(quality_);
        lim_.setLookaheadMs(values_[static_cast<size_t>(MasterParamId::LIM_LOOKAHEAD)]);
        clipBeforeLimiter_ = values_[static_cast<size_t>(MasterParamId::CLIP_POSITION)] >= 0.5f;
        updateLatency();
        busFade_ = 1.0f;
        pendingReprime_ = false;
        prepared_ = true;
    }

    // ---- control thread ----------------------------------------------------------------------------------------

    /** Queue a parameter change (any thread; producers are serialised by a mutex, the consumer is lock-free). */
    void setParam(int id, float value) noexcept {
        std::lock_guard<std::mutex> lock(producerMutex_);
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) & (kQueue - 1);
        if (next == tail_.load(std::memory_order_acquire)) return;  // full: drop (the UI sends again)
        queue_[head] = {id, value};
        head_.store(next, std::memory_order_release);
    }

    /** Oversampling factor 1/2/4/8 for the saturation and the final clipper. */
    void setQuality(int q) noexcept { requestedQuality_.store(q, std::memory_order_release); }

    int latency() const noexcept { return latency_.load(std::memory_order_acquire); }

    void readMeters(float* out) const noexcept {
        for (int i = 0; i < kMeterCount; ++i) out[i] = meters_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    }

    /** The UI's scope is visible: the audio thread writes the history only then. */
    void setScopeEnabled(bool on) noexcept { scopeOn_.store(on, std::memory_order_relaxed); }

    /**
     * Copies the scope entries written after `since` (a running block counter) into out (kScopeFields floats per
     * entry, oldest first, at most maxEntries, the newest ones if more are pending). Returns the new counter.
     * Entries are relaxed atomics: one being overwritten while read is a visual glitch, not a race.
     */
    uint64_t readScope(uint64_t since, float* out, int maxEntries, int& count) const noexcept {
        const uint64_t head = scopeHead_.load(std::memory_order_acquire);
        uint64_t from = since > head ? head : since;
        const uint64_t avail = head - from;
        const uint64_t cap = static_cast<uint64_t>(std::min(maxEntries, kScopeLen - 16));
        if (avail > cap) from = head - cap;
        count = static_cast<int>(head - from);
        for (int i = 0; i < count; ++i) {
            const size_t slot = static_cast<size_t>((from + static_cast<uint64_t>(i)) % kScopeLen) * kScopeFields;
            for (int f = 0; f < kScopeFields; ++f)
                out[i * kScopeFields + f] = scope_[slot + static_cast<size_t>(f)].load(std::memory_order_relaxed);
        }
        return head;
    }

    /** The engine reports the bus's share of the callback budget (0..1). */
    void setLoad(float ratio) noexcept { meters_[static_cast<size_t>(MasterMeterId::DSP_LOAD)].store(ratio * 100.0f, std::memory_order_relaxed); }

    // ---- audio thread ------------------------------------------------------------------------------------------

    void process(float* l, float* r, int n) noexcept {
        if (!prepared_) return;
        int done = 0;
        while (done < n) {
            const int len = std::min(kBlock, n - done);
            processBlock(l + done, r + done, len);
            done += len;
        }
    }

private:
    struct Msg { int id; float value; };
    static constexpr uint32_t kQueue = 256;

    struct Ramp {
        float v = 1, target = 1;
        /** Advance by n samples at `step` per sample; returns the start value. */
        float advance(int n, float step) noexcept {
            const float start = v;
            if (v < target) v = std::min(target, v + step * n);
            else if (v > target) v = std::max(target, v - step * n);
            return start;
        }
        bool off() const noexcept { return v <= 0.0f && target <= 0.0f; }
    };

    void drainQueue() noexcept {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        while (tail != head) {
            const Msg m = queue_[tail];
            if (m.id >= 0 && m.id < kMaxId) {
                values_[static_cast<size_t>(m.id)] = m.value;
                known_[static_cast<size_t>(m.id)] = true;
                if (prepared_) apply(m.id, m.value, false);
            }
            tail = (tail + 1) & (kQueue - 1);
        }
        tail_.store(tail, std::memory_order_release);
    }

    void apply(int id, float v, bool initial) noexcept {
        using P = MasterParamId;
        switch (static_cast<P>(id)) {
            case P::BYPASS: bypass_.target = v >= 0.5f ? 1.0f : 0.0f; if (initial) bypass_.v = bypass_.target; break;
            case P::INPUT_GAIN: input_.setGainDb(v); break;
            case P::OUTPUT_GAIN: outGain_.setTarget(dbToLin(v)); if (initial) outGain_.reset(dbToLin(v)); break;
            case P::MIX: mix_.setTarget(v); if (initial) mix_.reset(v); break;
            case P::AUTO_GAIN: autoGainOn_ = v >= 0.5f; break;
            case P::SAT_ENABLE: satOn_ = v >= 0.5f; updateSatMix(); break;
            case P::SAT_DRIVE: sat_.setDriveDb(v); break;
            case P::SAT_SHAPE: sat_.setShape(v); break;
            case P::SAT_SYMMETRY: sat_.setSymmetry(v); break;
            case P::SAT_DYNAMICS: sat_.setDynamics(v); break;
            case P::SAT_DYN_SPEED: sat_.setDynSpeed(v); break;
            case P::SAT_TILT: sat_.setTilt(v); break;
            case P::SAT_MIX: satMix_ = v; updateSatMix(); break;
            case P::SPC_ENABLE: spcOn_.target = v >= 0.5f ? 1.0f : 0.0f; if (initial) spcOn_.v = spcOn_.target; break;
            case P::SPC_CEILING: spc_.setCeilingDb(v); break;
            case P::SPC_SHAPE: spc_.setShape(v); break;
            case P::SPC_SPREAD: spc_.setSpread(v); break;
            case P::SPC_BANDS: {
                const int b = std::clamp(static_cast<int>(std::lround(v)), 0, 2);
                if (initial) spc_.setBands(b);
                else if (b != spc_.bandsIndex()) pendingBands_ = b;
                break;
            }
            case P::SPC_PEAK_MIX: spc_.setPeakMix(v); break;
            case P::SPC_PEAK_SENS: spc_.setPeakSens(v); break;
            case P::LIM_ENABLE: limOn_.target = v >= 0.5f ? 1.0f : 0.0f; if (initial) limOn_.v = limOn_.target; break;
            case P::LIM_CEILING: lim_.setCeilingDb(v); break;
            case P::LIM_LOOKAHEAD: if (!initial && v != lim_.lookaheadMs()) pendingReprime_ = true; break;
            case P::LIM_RELEASE: lim_.setReleaseMs(v); break;
            case P::LIM_AUTO_RELEASE: lim_.setAutoRelease(v >= 0.5f); break;
            case P::LIM_TRUE_PEAK: lim_.setTruePeak(v >= 0.5f); break;
            case P::LIM_LINK: lim_.setLink(v); break;
            case P::CLIP_ENABLE: clipOn_.target = v >= 0.5f ? 1.0f : 0.0f; if (initial) clipOn_.v = clipOn_.target; break;
            case P::CLIP_SHAPE: clip_.setShape(v); break;
            case P::CLIP_POSITION: if (!initial && (v >= 0.5f) != clipBeforeLimiter_) pendingReprime_ = true; break;
        }
    }

    void updateSatMix() noexcept { sat_.setMix(satOn_ ? satMix_ : 0.0f); }

    void updateLatency() noexcept {
        const int lat = sat_.latency() + lim_.latency() + clip_.latency();
        for (auto& d : dry_) { d.setDelay(lat); d.reset(); }
        latency_.store(lat, std::memory_order_release);
    }

    void reprime() noexcept {
        const int q = requestedQuality_.load(std::memory_order_acquire);
        quality_ = q;
        sat_.setFactor(q);
        clip_.setFactor(q);
        lim_.setLookaheadMs(values_[static_cast<size_t>(MasterParamId::LIM_LOOKAHEAD)]);
        clipBeforeLimiter_ = values_[static_cast<size_t>(MasterParamId::CLIP_POSITION)] >= 0.5f;
        spc_.reset();
        updateLatency();
    }

    void processBlock(float* l, float* r, int n) noexcept {
        drainQueue();
        if (requestedQuality_.load(std::memory_order_relaxed) != quality_) pendingReprime_ = true;

        // Dry: the sanitised input, for MIX and BYPASS (latency-aligned)
        for (int i = 0; i < n; ++i) {
            const float a = std::isfinite(l[i]) ? std::clamp(l[i], -64.0f, 64.0f) : 0.0f;
            const float b = std::isfinite(r[i]) ? std::clamp(r[i], -64.0f, 64.0f) : 0.0f;
            if (autoGainOn_) lufsIn_.process(a, b);
            dryL_[static_cast<size_t>(i)] = dry_[0].process(a);
            dryR_[static_cast<size_t>(i)] = dry_[1].process(b);
        }

        // Re-prime fade (latency changes)
        const float fadeStart = busFade_;
        if (pendingReprime_) {
            busFade_ = std::max(0.0f, busFade_ - fadeStep_ * n);
            if (busFade_ <= 0.0f && fadeStart <= 0.0f) {
                reprime();
                pendingReprime_ = false;
            }
        } else {
            busFade_ = std::min(1.0f, busFade_ + fadeStep_ * n);
        }

        const float byStart = bypass_.advance(n, rampStep_);
        const bool fullyBypassed = byStart >= 1.0f && bypass_.v >= 1.0f;
        if (fullyBypassed) {
            for (int i = 0; i < n; ++i) { l[i] = dryL_[static_cast<size_t>(i)]; r[i] = dryR_[static_cast<size_t>(i)]; }
            wasBypassed_ = true;
            publishMeters(l, r, n, true);
            return;
        }
        if (wasBypassed_) { resetStages(); wasBypassed_ = false; }

        input_.process(l, r, n);

        // 1. Saturation (its MIX smoother carries the on/off fade; once it has settled at 0, delay only)
        if (!satOn_ && sat_.mixValue() <= 1e-4f) {
            sat_.bypass(l, r, n);
            satWasOff_ = true;
        } else {
            if (satWasOff_) { sat_.reset(); satWasOff_ = false; }
            sat_.process(l, r, n);
        }

        // 2. Spectral clipper (zero latency: its dry is the input itself)
        if (pendingBands_ >= 0) {
            spcBandFade_.target = 0.0f;
            if (spcBandFade_.v <= 0.0f) { spc_.setBands(pendingBands_); pendingBands_ = -1; spcBandFade_.target = 1.0f; }
        }
        const float spcE0 = spcOn_.advance(n, rampStep_) * spcBandFade_.v;
        spcBandFade_.advance(n, rampStep_ * 2.0f);
        const float spcE1 = spcOn_.v * spcBandFade_.v;
        if (spcE0 > 0.0f || spcE1 > 0.0f) {
            std::copy(l, l + n, tmpL_.begin());
            std::copy(r, r + n, tmpR_.begin());
            spc_.process(l, r, n);
            if (spcE0 < 1.0f || spcE1 < 1.0f) blend(l, r, n, spcE0, spcE1);
        }

        // 3/4. Limiter and final clipper, in either order
        const float ceiling = lim_.ceilingLin();
        if (clipBeforeLimiter_) { runClipper(l, r, n, ceiling); runLimiter(l, r, n); }
        else { runLimiter(l, r, n); runClipper(l, r, n, lim_.ceilingLin()); }
        if (!limOn_.off() || !clipOn_.off()) {
            const float c = lim_.ceilingLin();
            for (int i = 0; i < n; ++i) { l[i] = std::clamp(l[i], -c, c); r[i] = std::clamp(r[i], -c, c); }
        }

        // 5. Output gain, auto gain, MIX, BYPASS, re-prime fade
        float agStart = autoGain_.value();
        if (autoGainOn_) {
            const float lin = lufsIn_.shortTerm(), lout = lufsOut_.shortTerm();
            if (lin > -70.0f && lout > -70.0f) {
                const float targetDb = std::clamp(lin - (lout - linToDb(autoGain_.value())), -24.0f, 24.0f);
                autoGain_.setTarget(dbToLin(targetDb));
            }
        } else {
            autoGain_.setTarget(1.0f);
        }
        const float agEnd = autoGain_.advance(n);
        const float mixStart = mix_.value(), mixEnd = mix_.advance(n);
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / n;
            const float g = outGain_.next() * (agStart + (agEnd - agStart) * t);
            float a = l[i] * g, b = r[i] * g;
            lufsOut_.process(a, b);
            const float mx = mixStart + (mixEnd - mixStart) * t;
            const float dl = dryL_[static_cast<size_t>(i)], dr = dryR_[static_cast<size_t>(i)];
            a = dl + mx * (a - dl);
            b = dr + mx * (b - dr);
            const float by = byStart + (bypass_.v - byStart) * t;
            a += by * (dl - a);
            b += by * (dr - b);
            const float f = fadeStart + (busFade_ - fadeStart) * t;
            l[i] = a * f;
            r[i] = b * f;
        }
        publishMeters(l, r, n, false);
    }

    void runLimiter(float* l, float* r, int n) noexcept {
        const float e0 = limOn_.advance(n, rampStep_), e1 = limOn_.v;
        if (e0 <= 0.0f && e1 <= 0.0f) {
            if (!limWasOff_) limWasOff_ = true;
            lim_.bypass(l, r, n);
            return;
        }
        if (limWasOff_) { lim_.reset(false); limWasOff_ = false; }
        lim_.process(l, r, n, e0, e1);
    }

    void runClipper(float* l, float* r, int n, float ceiling) noexcept {
        const float e0 = clipOn_.advance(n, rampStep_), e1 = clipOn_.v;
        if (e0 <= 0.0f && e1 <= 0.0f) {
            if (!clipWasOff_) clipWasOff_ = true;
            clip_.bypass(l, r, n);
            return;
        }
        if (clipWasOff_) { clip_.reset(); clipWasOff_ = false; }
        clip_.process(l, r, n, ceiling, e0, e1);
    }

    void blend(float* l, float* r, int n, float e0, float e1) noexcept {
        for (int i = 0; i < n; ++i) {
            const float e = e0 + (e1 - e0) * (static_cast<float>(i) / n);
            l[i] = tmpL_[static_cast<size_t>(i)] + e * (l[i] - tmpL_[static_cast<size_t>(i)]);
            r[i] = tmpR_[static_cast<size_t>(i)] + e * (r[i] - tmpR_[static_cast<size_t>(i)]);
        }
    }

    void resetStages() noexcept {
        sat_.resetAll(); spc_.reset(); lim_.reset(true); clip_.resetAll();
    }

    void publishMeters(const float* l, const float* r, int n, bool bypassed) noexcept {
        const float decay = std::pow(10.0f, -static_cast<float>(n / rate_));  // 20 dB/s on dB scale
        float inL = 0, inR = 0;
        if (!bypassed) input_.takePeaks(inL, inR);
        float outL = 0, outR = 0, tp = 0;
        for (int i = 0; i < n; ++i) {
            outL = std::max(outL, std::fabs(l[i]));
            outR = std::max(outR, std::fabs(r[i]));
            tp = std::max(tp, std::max(outTp_[0].process(l[i]), outTp_[1].process(r[i])));
        }
        auto peak = [&](MasterMeterId id, float lin) {
            const float prev = dbToLin(meters_[static_cast<size_t>(id)].load(std::memory_order_relaxed)) * decay;
            meters_[static_cast<size_t>(id)].store(linToDb(std::max(lin, prev)), std::memory_order_relaxed);
        };
        auto gr = [&](MasterMeterId id, float db) {
            const float prev = meters_[static_cast<size_t>(id)].load(std::memory_order_relaxed);
            const float recovered = std::min(0.0f, prev + 20.0f * static_cast<float>(n / rate_));
            meters_[static_cast<size_t>(id)].store(std::min(db, recovered), std::memory_order_relaxed);
        };
        peak(MasterMeterId::IN_PEAK_L, inL);
        peak(MasterMeterId::IN_PEAK_R, inR);
        meters_[static_cast<size_t>(MasterMeterId::IN_RMS)].store(linToDb(input_.rms()), std::memory_order_relaxed);
        meters_[static_cast<size_t>(MasterMeterId::SAT_AMOUNT)].store(bypassed ? 0.0f : sat_.amount(), std::memory_order_relaxed);
        gr(MasterMeterId::SPC_GR, bypassed || spcOn_.off() ? 0.0f : spc_.grDb());
        gr(MasterMeterId::LIM_GR, bypassed ? 0.0f : lim_.grDb());
        gr(MasterMeterId::CLIP_GR, bypassed ? 0.0f : clip_.grDb());
        peak(MasterMeterId::OUT_PEAK_L, outL);
        peak(MasterMeterId::OUT_PEAK_R, outR);
        peak(MasterMeterId::OUT_TRUE_PEAK, tp);
        meters_[static_cast<size_t>(MasterMeterId::OUT_LUFS_S)].store(lufsOut_.shortTerm(), std::memory_order_relaxed);

        if (scopeOn_.load(std::memory_order_relaxed)) {
            const float outPk = std::max(outL, outR);
            const float inPk = bypassed ? outPk : std::max(inL, inR);
            float grSum = 0.0f;
            if (!bypassed) {
                if (!spcOn_.off()) grSum += spc_.grDb();
                if (!limOn_.off()) grSum += lim_.grDb();
                if (!clipOn_.off()) grSum += clip_.grDb();
            }
            const uint64_t h = scopeHead_.load(std::memory_order_relaxed);
            const size_t slot = static_cast<size_t>(h % kScopeLen) * kScopeFields;
            scope_[slot].store(linToDb(inPk), std::memory_order_relaxed);
            scope_[slot + 1].store(linToDb(outPk), std::memory_order_relaxed);
            scope_[slot + 2].store(std::min(0.0f, grSum), std::memory_order_relaxed);
            scopeHead_.store(h + 1, std::memory_order_release);
        }
    }

    double rate_ = 48000;
    bool prepared_ = false;

    InputStage input_;
    DynamicSaturator sat_;
    SpectralClipper spc_;
    LookaheadLimiter lim_;
    FinalClipper clip_;
    std::array<DelayLine, 2> dry_;
    std::array<float, kBlock> dryL_{}, dryR_{}, tmpL_{}, tmpR_{};
    Smoother outGain_, mix_, autoGain_;
    LufsMeter lufsIn_, lufsOut_;
    std::array<TruePeakMeter, 2> outTp_;

    // parameter mirror (audio thread after prepare; control thread before), defaults from the table
    std::array<float, kMaxId> values_ = defaults();
    std::array<bool, kMaxId> known_ = knownIds();

    Ramp bypass_{0, 0}, spcOn_, limOn_, clipOn_, spcBandFade_;
    float rampStep_ = 0.001f, fadeStep_ = 0.002f, busFade_ = 1.0f, satMix_ = 1.0f;
    bool satOn_ = true, satWasOff_ = false, limWasOff_ = false, clipWasOff_ = false, wasBypassed_ = false;
    bool autoGainOn_ = false, clipBeforeLimiter_ = false, pendingReprime_ = false;
    int pendingBands_ = -1, quality_ = 1;
    std::atomic<int> requestedQuality_{1};
    std::atomic<int> latency_{0};

    std::array<Msg, kQueue> queue_{};
    std::atomic<uint32_t> head_{0}, tail_{0};
    std::mutex producerMutex_;
    std::array<std::atomic<float>, kMeterCount> meters_;
    std::array<std::atomic<float>, static_cast<size_t>(kScopeLen) * kScopeFields> scope_{};
    std::atomic<uint64_t> scopeHead_{0};
    std::atomic<bool> scopeOn_{false};

    static std::array<float, kMaxId> defaults() noexcept {
        std::array<float, kMaxId> v{};
        int count = 0;
        const auto* t = masterParams(&count);
        for (int i = 0; i < count; ++i) v[static_cast<size_t>(t[i].id)] = t[i].def;
        return v;
    }
    static std::array<bool, kMaxId> knownIds() noexcept {
        std::array<bool, kMaxId> k{};
        int count = 0;
        const auto* t = masterParams(&count);
        for (int i = 0; i < count; ++i) k[static_cast<size_t>(t[i].id)] = true;
        return k;
    }
};

} // namespace fm1deck::dsp
