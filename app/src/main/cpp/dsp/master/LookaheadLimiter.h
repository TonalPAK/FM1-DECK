#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// MASTER-BUS §3: look-ahead limiter without overshoot. Per channel: peak (sample or 4x true peak, both with the
// detector's kDelay = 24-sample delay) -> required gain min(1, ceiling / peak) -> stereo link -> sliding minimum over the
// look-ahead window W = L + 1 -> boxcar average over W (the attack ramp) -> release smoothing that only ever slows
// the gain's rise. The audio is delayed by L + kDelay, so the gain reaches its target exactly when the peak leaves the
// delay line: g[n] = mean(min(req[n-L .. n])) <= req[n - L] for every sample.

#include "../DspCore.h"
#include "../Meters.h"

#include <array>

namespace fm1deck::dsp {

class LookaheadLimiter {
public:
    static constexpr int kMaxLookaheadMs = 5;

    void prepare(double rate) {
        rate_ = rate;
        const int maxL = static_cast<int>(std::ceil(kMaxLookaheadMs * 0.001 * rate)) + 1;
        for (int c = 0; c < 2; ++c) {
            delay_[static_cast<size_t>(c)].prepare(maxL + TruePeakDetector::kDelay + 2);
            smin_[static_cast<size_t>(c)].prepare(maxL + 2);
            box_[static_cast<size_t>(c)].prepare(maxL + 2);
        }
        ceiling_.prepare(rate, 20.0f);
        ceiling_.reset(dbToLin(-0.3f));
        link_.prepare(rate, 20.0f);
        link_.reset(1.0f);
        setLookaheadMs(1.5f);
    }

    /** Changes the latency: the bus fades around it. */
    void setLookaheadMs(float ms) noexcept {
        lookMs_ = std::clamp(ms, 0.5f, static_cast<float>(kMaxLookaheadMs));
        L_ = std::max(1, static_cast<int>(std::lround(lookMs_ * 0.001 * rate_)));
        for (int c = 0; c < 2; ++c) {
            delay_[static_cast<size_t>(c)].setDelay(L_ + TruePeakDetector::kDelay);
            smin_[static_cast<size_t>(c)].setWindow(L_ + 1);
            box_[static_cast<size_t>(c)].setWindow(L_ + 1);
        }
        reset();
    }
    float lookaheadMs() const noexcept { return lookMs_; }
    int latency() const noexcept { return L_ + TruePeakDetector::kDelay; }

    /** Detector and gain state; with all = true also the audio delay (latency change, full bypass). */
    void reset(bool all = true) noexcept {
        for (int c = 0; c < 2; ++c) {
            if (all) delay_[static_cast<size_t>(c)].reset();
            smin_[static_cast<size_t>(c)].reset();
            box_[static_cast<size_t>(c)].reset(1.0f);
            tp_[static_cast<size_t>(c)].reset();
            gain_[static_cast<size_t>(c)] = 1.0f;
        }
        grSamples_ = 0;
    }

    void setCeilingDb(float db) noexcept { ceiling_.setTarget(dbToLin(db)); }
    float ceilingLin() const noexcept { return ceiling_.value(); }
    void setReleaseMs(float ms) noexcept { releaseMs_ = ms; }
    void setAutoRelease(bool on) noexcept { autoRelease_ = on; }
    void setTruePeak(bool on) noexcept { truePeak_ = on; }
    void setLink(float v) noexcept { link_.setTarget(v); }

    /** e0 .. e1: the stage's on/off crossfade across the block (the gain blends from 1 to the limiter's). */
    void process(float* l, float* r, int n, float e0 = 1.0f, float e1 = 1.0f) noexcept {
        const float slowCoef = onePoleCoef(releaseMs_, rate_);
        const float fastCoef = onePoleCoef(30.0f, rate_);
        const float grHold = 0.3f * static_cast<float>(rate_);
        float minGain = 1.0f;
        float* ch[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            const float ceil = ceiling_.next();
            const float link = link_.next();
            float req[2];
            for (int c = 0; c < 2; ++c) {
                const float x = ch[c][i];
                const float pk = truePeak_ ? tp_[static_cast<size_t>(c)].process(x, 0.5f * ceil)
                                           : tp_[static_cast<size_t>(c)].processSample(x);
                req[c] = pk > ceil ? ceil / pk : 1.0f;
            }
            const float lo = std::min(req[0], req[1]);
            // release blend: fast for single peaks, slow (LIM_RELEASE) for sustained reduction
            const bool reducing = std::min(gain_[0], gain_[1]) < 0.891f;  // -1 dB
            grSamples_ = reducing ? std::min(grSamples_ + 1.0f, grHold) : std::max(grSamples_ - 4.0f, 0.0f);
            const float w = autoRelease_ ? grSamples_ / grHold : 1.0f;
            const float rel = fastCoef + (slowCoef - fastCoef) * w;
            for (int c = 0; c < 2; ++c) {
                const float rq = req[c] + link * (lo - req[c]);
                const float m = smin_[static_cast<size_t>(c)].push(rq);
                const float target = box_[static_cast<size_t>(c)].push(m);
                float& g = gain_[static_cast<size_t>(c)];
                g = target < g ? target : g + rel * (target - g);
                const float e = e0 + (e1 - e0) * (static_cast<float>(i) / n);
                ch[c][i] = delay_[static_cast<size_t>(c)].process(ch[c][i]) * (1.0f + e * (g - 1.0f));
                minGain = std::min(minGain, g);
            }
        }
        grDb_ = linToDb(minGain);
    }

    /** Delay-only path while the stage is off. */
    void bypass(float* l, float* r, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            l[i] = delay_[0].process(l[i]);
            r[i] = delay_[1].process(r[i]);
        }
        grDb_ = 0;
    }

    float grDb() const noexcept { return grDb_; }
    void settle() noexcept { ceiling_.settle(); link_.settle(); }

private:
    double rate_ = 48000;
    std::array<DelayLine, 2> delay_;
    std::array<SlidingMin, 2> smin_;
    std::array<Boxcar, 2> box_;
    std::array<TruePeakDetector, 2> tp_;
    std::array<float, 2> gain_{1.0f, 1.0f};
    Smoother ceiling_, link_;
    float lookMs_ = 1.5f, releaseMs_ = 120.0f, grSamples_ = 0, grDb_ = 0;
    int L_ = 72;
    bool autoRelease_ = true, truePeak_ = true;
};

} // namespace fm1deck::dsp
