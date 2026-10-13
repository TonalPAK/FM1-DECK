#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// MASTER-BUS §4: final clipper at the limiter's ceiling, hard .. 3 dB soft knee (CLIP_SHAPE), residual ADAA1 inside
// the oversampler.

#include "../DspCore.h"
#include "../Oversampler.h"
#include "../Shaper.h"

#include <array>

namespace fm1deck::dsp {

class FinalClipper {
public:
    static constexpr int kMaxBlock = 64;

    void prepare(double /*rate*/) {
        for (auto& o : os_) o.prepare(kMaxBlock);
        for (auto& d : dry_) d.prepare(64);
        setFactor(1);
    }
    void setFactor(int q) noexcept {
        for (auto& o : os_) o.setFactor(q);
        for (auto& d : dry_) { d.setDelay(os_[0].latency()); d.reset(); }
        reset();
    }
    int latency() const noexcept { return os_[0].latency(); }
    /** The processing state only; the dry delay keeps running. */
    void reset() noexcept {
        for (auto& o : os_) o.reset();
        for (auto& a : adaa_) a.reset();
    }
    void resetAll() noexcept {
        reset();
        for (auto& d : dry_) d.reset();
    }
    void setShape(float s) noexcept { shape_ = s; }

    /** e0 .. e1: the stage's on/off crossfade across the block (1 = fully on). */
    void process(float* l, float* r, int n, float ceiling, float e0 = 1.0f, float e1 = 1.0f) noexcept {
        curve_.set(ceiling, shape_);
        double sumIn = 0, sumOut = 0;
        float* ch[2] = {l, r};
        for (int c = 0; c < 2; ++c) {
            auto& adaa = adaa_[static_cast<size_t>(c)];
            adaa.refresh(curve_);
            for (int i = 0; i < n; ++i) dryBuf_[static_cast<size_t>(i)] = dry_[static_cast<size_t>(c)].process(ch[c][i]);
            float* u = os_[static_cast<size_t>(c)].up(ch[c], n);
            const int len = n * os_[0].factor();
            const bool adaaOn = true;
            for (int j = 0; j < len; ++j) {
                const double x = u[j];
                const double y = adaaOn ? adaa.process(curve_, x, curve_.small(), 0.0) : curve_.f(x);
                sumIn += x * x;
                sumOut += y * y;
                u[j] = static_cast<float>(y);
            }
            os_[static_cast<size_t>(c)].down(u, ch[c], n);
            if (e0 < 1.0f || e1 < 1.0f) {
                for (int i = 0; i < n; ++i) {
                    const float e = e0 + (e1 - e0) * (static_cast<float>(i) / n);
                    ch[c][i] = dryBuf_[static_cast<size_t>(i)] + e * (ch[c][i] - dryBuf_[static_cast<size_t>(i)]);
                }
            }
        }
        grDb_ = sumIn > 1e-12 ? std::min(0.0f, static_cast<float>(10.0 * std::log10(std::max(sumOut, 1e-30) / sumIn))) : 0.0f;
    }

    void bypass(float* l, float* r, int n) noexcept {
        for (int i = 0; i < n; ++i) { l[i] = dry_[0].process(l[i]); r[i] = dry_[1].process(r[i]); }
        grDb_ = 0;
    }

    float grDb() const noexcept { return grDb_; }

private:
    std::array<Oversampler, 2> os_;
    std::array<DelayLine, 2> dry_;
    std::array<ResidualAdaa<CeilingCurve>, 2> adaa_;
    CeilingCurve curve_;
    std::array<float, kMaxBlock> dryBuf_{};
    float shape_ = 0.2f, grDb_ = 0;
};

} // namespace fm1deck::dsp
