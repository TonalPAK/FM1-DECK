#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// MASTER-BUS §1: dynamic saturation. An RMS follower on mid moves the drive (DYNAMICS), a tilt pre/de-emphasis pair
// places the harmonics (TILT), the shape family is antialiased by residual ADAA1 inside the oversampler, SYMMETRY
// adds even harmonics (then a DC blocker), level compensation keeps mid levels at unity, MIX is parallel against a
// dry path delayed by the oversampler latency.

#include "../DspCore.h"
#include "../Oversampler.h"
#include "../Shaper.h"

#include <array>

namespace fm1deck::dsp {

class DynamicSaturator {
public:
    static constexpr int kMaxBlock = 64;

    void prepare(double rate) {
        rate_ = rate;
        for (auto& o : os_) o.prepare(kMaxBlock);
        for (auto& d : dry_) d.prepare(64);
        env_.prepare(rate, 20.0f, 80.0f);
        drive_.prepare(rate, 5.0f);
        drive_.reset(dbToLin(6.0f));
        mix_.prepare(rate, 20.0f);
        mix_.reset(1.0f);
        sym_.prepare(rate, 20.0f);
        sym_.reset(0.0f);
        tilt_.prepare(rate, 20.0f);
        tilt_.reset(0.0f);
        setFactor(1);
    }

    /** Oversampling factor; the bus fades around this (latency changes). */
    void setFactor(int q) noexcept {
        for (auto& o : os_) o.setFactor(q);
        for (auto& d : dry_) { d.setDelay(os_[0].latency()); d.reset(); }
        osRate_ = rate_ * os_[0].factor();
        lastTilt_ = 1e9f;
        reset();
    }
    /** Everything including the dry delay (after a latency change or a full bypass). */
    void resetAll() noexcept {
        reset();
        for (auto& d : dry_) d.reset();
    }
    int latency() const noexcept { return os_[0].latency(); }

    /** The processing state only; the dry delay keeps running (re-enabling must not drop out). */
    void reset() noexcept {
        for (auto& o : os_) o.reset();
        for (auto& a : adaa_) a.reset();
        for (auto& f : pre_) f.reset();
        for (auto& f : post_) f.reset();
        for (auto& f : dc_) { f.setDcBlocker(5.0, rate_); f.reset(); }
        env_.reset(0.0f);
    }

    // Parameters (MASTER-BUS §1)
    void setDriveDb(float v) noexcept { driveDb_ = v; }
    void setShape(float v) noexcept { shape_ = v; }
    void setSymmetry(float v) noexcept { sym_.setTarget(0.3f * v); }
    void setDynamics(float v) noexcept { dynamics_ = v; }
    void setDynSpeed(float ms) noexcept {
        if (ms != speedMs_) { speedMs_ = ms; env_.setTimes(ms * 0.25f, ms); }
    }
    void setTilt(float v) noexcept { tilt_.setTarget(v); }
    void setMix(float v) noexcept { mix_.setTarget(v); }

    /** In place, n <= kMaxBlock. */
    void process(float* l, float* r, int n) noexcept {
        // 1-2. Envelope on mid at the base rate; the drive target follows it every 16 samples (smoothed 5 ms)
        for (int i = 0; i < n; ++i) {
            const float m = 0.5f * (l[i] + r[i]);
            const float ms = env_.process(m * m);
            if ((i & 15) == 0) {
                float e = 10.0f * std::log10(std::max(ms, 1e-12f)) + 18.0f;
                e = std::clamp(e, -24.0f, 12.0f);
                drive_.setTarget(dbToLin(driveDb_ + dynamics_ * 0.5f * e));
            }
            g_[static_cast<size_t>(i)] = drive_.next();
        }
        // Shape, symmetry, tilt: per block
        curve_.setShape(shape_);
        const float b = sym_.advance(n);
        const double fb = curve_.f(b);
        const float tilt = tilt_.advance(n);
        if (tilt != lastTilt_) {
            lastTilt_ = tilt;
            const double a = std::pow(10.0, 6.0 * tilt / 20.0);
            for (int c = 0; c < 2; ++c) {
                pre_[static_cast<size_t>(c)].setTilt(a, 800.0, osRate_, false);
                post_[static_cast<size_t>(c)].setTilt(a, 800.0, osRate_, true);
            }
        }
        for (auto& a : adaa_) a.refresh(curve_);
        const bool adaaOn = true;  // ADAA at every Q: without it a hard clip aliases more at x4 than with it at x2
        const double small = curve_.small(), c3 = curve_.cubic();
        // 6. Level compensation f(g r) / r at r = 0.25, interpolated across the block
        const double comp0 = compFor(g_[0]), comp1 = compFor(g_[static_cast<size_t>(n - 1)]);

        const int q = os_[0].factor();
        const float mixStart = mix_.value();
        const float mixEnd = mix_.advance(n);
        double sumIn = 0, sumOut = 0;
        float* chans[2] = {l, r};
        for (int c = 0; c < 2; ++c) {
            float* x = chans[c];
            for (int i = 0; i < n; ++i) dryBuf_[static_cast<size_t>(i)] = dry_[static_cast<size_t>(c)].process(x[i]);
            float* u = os_[static_cast<size_t>(c)].up(x, n);
            auto& pre = pre_[static_cast<size_t>(c)];
            auto& post = post_[static_cast<size_t>(c)];
            auto& adaa = adaa_[static_cast<size_t>(c)];
            const int len = n * q;
            for (int j = 0; j < len; ++j) {
                const int i = j / q;
                const double g = g_[static_cast<size_t>(i)];
                const double frac = static_cast<double>(j) / len;
                const double inv = 1.0 / (comp0 + (comp1 - comp0) * frac);
                const double xin = pre.process(u[j]) * g;
                const double y = (adaaOn ? adaa.process(curve_, xin + b, small, c3) : curve_.f(xin + b)) - fb;
                sumIn += xin * xin;
                sumOut += y * y;
                u[j] = post.process(static_cast<float>(y * inv));
            }
            os_[static_cast<size_t>(c)].down(u, wet_.data(), n);
            for (int i = 0; i < n; ++i) {
                const float w = dc_[static_cast<size_t>(c)].process(wet_[static_cast<size_t>(i)]);
                const float mx = mixStart + (mixEnd - mixStart) * (static_cast<float>(i) / n);
                x[i] = dryBuf_[static_cast<size_t>(i)] + mx * (w - dryBuf_[static_cast<size_t>(i)]);
            }
        }
        // Meter: 1 - y/x by RMS
        const float amount = sumIn > 1e-12 ? static_cast<float>(1.0 - std::sqrt(sumOut / sumIn)) : 0.0f;
        amount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /** Delay-only path while the stage is off (keeps the bus latency). */
    void bypass(float* l, float* r, int n) noexcept {
        for (int i = 0; i < n; ++i) { l[i] = dry_[0].process(l[i]); r[i] = dry_[1].process(r[i]); }
        amount_ = 0;
    }

    float amount() const noexcept { return amount_; }
    /** Jump every smoother to its target (prepare: start at the stored values, no glide). */
    void settle() noexcept { mix_.settle(); sym_.settle(); tilt_.settle(); }
    float mixValue() const noexcept { return mix_.value(); }

private:
    double compFor(float g) const noexcept {
        const double r = 0.25;
        const double c = curve_.f(g * r) / r;
        return c > 1e-6 ? c : 1.0;
    }

    double rate_ = 48000, osRate_ = 48000;
    std::array<Oversampler, 2> os_;
    std::array<DelayLine, 2> dry_;
    std::array<ResidualAdaa<SatCurve>, 2> adaa_;
    std::array<FirstOrder, 2> pre_, post_, dc_;
    EnvelopeFollower env_;
    Smoother drive_, mix_, sym_, tilt_;
    SatCurve curve_;
    std::array<float, kMaxBlock> g_{}, dryBuf_{}, wet_{};
    float driveDb_ = 6, shape_ = 0.6f, dynamics_ = 0.5f, speedMs_ = 80, lastTilt_ = 1e9f, amount_ = 0;
};

} // namespace fm1deck::dsp
