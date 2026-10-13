#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// MASTER-BUS §0: sanitise (NaN/Inf -> 0, clamp +-64), 5 Hz DC blocker, INPUT_GAIN (20 ms), input meters.

#include "../DspCore.h"

namespace fm1deck::dsp {

class InputStage {
public:
    void prepare(double rate) {
        rate_ = rate;
        for (auto& d : dc_) { d.setDcBlocker(5.0, rate); d.reset(); }
        gain_.prepare(rate, 20.0f);
        gain_.reset(1.0f);
        rmsCoef_ = onePoleCoef(300.0f, rate);
        badSamples_ = 0;
    }
    void setGainDb(float db) noexcept { gain_.setTarget(dbToLin(db)); }
    void settle() noexcept { gain_.settle(); }

    void process(float* l, float* r, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            float a = l[i], b = r[i];
            if (!std::isfinite(a)) { a = 0; ++badSamples_; }
            if (!std::isfinite(b)) { b = 0; ++badSamples_; }
            a = std::clamp(a, -64.0f, 64.0f);
            b = std::clamp(b, -64.0f, 64.0f);
            const float g = gain_.next();
            a = dc_[0].process(a) * g;
            b = dc_[1].process(b) * g;
            peakL_ = std::max(peakL_, std::fabs(a));
            peakR_ = std::max(peakR_, std::fabs(b));
            ms_ += rmsCoef_ * (0.5f * (a * a + b * b) - ms_);
            l[i] = a; r[i] = b;
        }
    }

    /** Peaks since the last call (the bus decays them for the meters). */
    void takePeaks(float& l, float& r) noexcept { l = peakL_; r = peakR_; peakL_ = peakR_ = 0; }
    float rms() const noexcept { return std::sqrt(std::max(ms_, 0.0f)); }
    uint32_t badSamples() const noexcept { return badSamples_; }

private:
    double rate_ = 48000;
    FirstOrder dc_[2];
    Smoother gain_;
    float rmsCoef_ = 0, ms_ = 0, peakL_ = 0, peakR_ = 0;
    uint32_t badSamples_ = 0;
};

} // namespace fm1deck::dsp
