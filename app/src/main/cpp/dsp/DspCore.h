#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// FM1 DECK DSP building blocks for the master bus (docs/MASTER-BUS.md). Header-only, no allocation after
// prepare(), no locks, no logging: everything here is called from the audio callback.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fm1deck::dsp {

constexpr double kPi = 3.14159265358979323846;

inline float dbToLin(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline float linToDb(float lin) noexcept { return 20.0f * std::log10(std::max(lin, 1e-12f)); }

/** One-pole coefficient for a time constant in ms at a sample rate (63 % rise in t). */
inline float onePoleCoef(float ms, double rate) noexcept {
    if (ms <= 0.0f) return 1.0f;
    return static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * ms * rate)));
}

// ------------------------------------------------------------------------------------------------------------------
// Smoother: one-pole towards a target; per-sample or per-block.
// ------------------------------------------------------------------------------------------------------------------
class Smoother {
public:
    void prepare(double rate, float ms) noexcept { coef_ = onePoleCoef(ms, rate); }
    void reset(float v) noexcept { value_ = target_ = v; }
    void setTarget(float t) noexcept { target_ = t; }
    float target() const noexcept { return target_; }
    float value() const noexcept { return value_; }
    float next() noexcept {
        value_ += coef_ * (target_ - value_);
        return value_;
    }
    /** Advance n samples at once (exact for a one-pole). */
    float advance(int n) noexcept {
        const float k = 1.0f - std::pow(1.0f - coef_, static_cast<float>(n));
        value_ += k * (target_ - value_);
        if (std::fabs(target_ - value_) < 1e-7f) value_ = target_;
        return value_;
    }
    bool settled() const noexcept { return value_ == target_; }
    void settle() noexcept { value_ = target_; }

private:
    float coef_ = 1.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

// ------------------------------------------------------------------------------------------------------------------
// Biquad (transposed direct form II), RBJ designs. Coefficients normalised by a0.
// ------------------------------------------------------------------------------------------------------------------
struct BiquadCoefs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoefs make(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
        BiquadCoefs c;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }
    static BiquadCoefs lowpass(double f, double q, double rate) noexcept {
        const double w = 2.0 * kPi * f / rate, c = std::cos(w), a = std::sin(w) / (2.0 * q);
        return make((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }
    static BiquadCoefs highpass(double f, double q, double rate) noexcept {
        const double w = 2.0 * kPi * f / rate, c = std::cos(w), a = std::sin(w) / (2.0 * q);
        return make((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }
    static BiquadCoefs allpass(double f, double q, double rate) noexcept {
        const double w = 2.0 * kPi * f / rate, c = std::cos(w), a = std::sin(w) / (2.0 * q);
        return make(1 - a, -2 * c, 1 + a, 1 + a, -2 * c, 1 - a);
    }
    /** ITU-R BS.1770 K-weighting stage 1 (high shelf), for any rate (libebur128's derivation). */
    static BiquadCoefs kShelf(double rate) noexcept {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(kPi * f0 / rate), vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
        return make(vh + vb * k / q + k * k, 2.0 * (k * k - vh), vh - vb * k / q + k * k,
                    1.0 + k / q + k * k, 2.0 * (k * k - 1.0), 1.0 - k / q + k * k);
    }
    /** ITU-R BS.1770 K-weighting stage 2 (RLB high-pass). */
    static BiquadCoefs kHighpass(double rate) noexcept {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(kPi * f0 / rate);
        return make(1.0, -2.0, 1.0, 1.0 + k / q + k * k, 2.0 * (k * k - 1.0), 1.0 - k / q + k * k);
    }
};

struct BiquadState {
    double z1 = 0, z2 = 0;
    void reset() noexcept { z1 = z2 = 0; }
    inline float process(const BiquadCoefs& c, float xf) noexcept {
        const double x = xf;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }
    void processBlock(const BiquadCoefs& c, float* buf, int n) noexcept {
        double a = z1, b = z2;
        for (int i = 0; i < n; ++i) {
            const double x = buf[i];
            const double y = c.b0 * x + a;
            a = c.b1 * x - c.a1 * y + b;
            b = c.b2 * x - c.a2 * y;
            buf[i] = static_cast<float>(y);
        }
        z1 = a; z2 = b;
    }
};

// ------------------------------------------------------------------------------------------------------------------
// First-order sections: DC blocker, tilt (and its exact inverse).
// ------------------------------------------------------------------------------------------------------------------
struct FirstOrder {
    double b0 = 1, b1 = 0, a1 = 0;  // y = b0 x + b1 x1 - a1 y1
    double x1 = 0, y1 = 0;
    void reset() noexcept { x1 = y1 = 0; }
    inline float process(float xf) noexcept {
        const double x = xf;
        const double y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = y;
        return static_cast<float>(y);
    }
    /** One-pole DC blocker: y = x - x1 + R y1, R = exp(-2 pi fc / rate). */
    void setDcBlocker(double fc, double rate) noexcept {
        b0 = 1.0; b1 = -1.0; a1 = -std::exp(-2.0 * kPi * fc / rate);
    }
    /**
     * Tilt around f0: gain A at high frequencies, 1/A at low, 1 at f0 (bilinear, prewarped at f0).
     * inverse = true gives the exact inverse filter (also minimum phase, stable).
     */
    void setTilt(double a, double f0, double rate, bool inverse) noexcept {
        const double t = std::tan(kPi * f0 / rate);
        double nb0 = a + t, nb1 = t - a, na0 = 1.0 + a * t, na1 = a * t - 1.0;
        if (inverse) { std::swap(nb0, na0); std::swap(nb1, na1); }
        b0 = nb0 / na0; b1 = nb1 / na0; a1 = na1 / na0;
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Delay line with a fixed capacity (allocated in prepare), integer delay settable on the audio thread.
// ------------------------------------------------------------------------------------------------------------------
class DelayLine {
public:
    void prepare(int maxDelay) {
        size_ = 1;
        while (size_ < maxDelay + 1) size_ <<= 1;
        buf_.assign(static_cast<size_t>(size_), 0.0f);
        mask_ = size_ - 1;
        pos_ = 0;
    }
    void reset() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    void setDelay(int d) noexcept { delay_ = std::clamp(d, 0, size_ - 1); }
    int delay() const noexcept { return delay_; }
    inline float process(float x) noexcept {
        buf_[static_cast<size_t>(pos_)] = x;
        const float y = buf_[static_cast<size_t>((pos_ - delay_) & mask_)];
        pos_ = (pos_ + 1) & mask_;
        return y;
    }

private:
    std::vector<float> buf_;
    int size_ = 1, mask_ = 0, pos_ = 0, delay_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------
// Sliding minimum over the last `window` values (monotonic deque in a fixed ring), O(1) amortised.
// ------------------------------------------------------------------------------------------------------------------
class SlidingMin {
public:
    void prepare(int maxWindow) {
        cap_ = 1;
        while (cap_ < maxWindow + 2) cap_ <<= 1;
        vals_.assign(static_cast<size_t>(cap_), 0.0f);
        idx_.assign(static_cast<size_t>(cap_), 0);
        reset();
    }
    void setWindow(int w) noexcept { window_ = std::max(1, std::min(w, cap_ - 2)); }
    void reset() noexcept { head_ = tail_ = 0; n_ = 0; }
    inline float push(float v) noexcept {
        const int mask = cap_ - 1;
        while (tail_ != head_ && vals_[static_cast<size_t>((tail_ - 1) & mask)] >= v) tail_ = (tail_ - 1) & mask;
        vals_[static_cast<size_t>(tail_)] = v;
        idx_[static_cast<size_t>(tail_)] = n_;
        tail_ = (tail_ + 1) & mask;
        while (idx_[static_cast<size_t>(head_)] <= n_ - window_) head_ = (head_ + 1) & mask;
        ++n_;
        return vals_[static_cast<size_t>(head_)];
    }

private:
    std::vector<float> vals_;
    std::vector<int64_t> idx_;
    int cap_ = 4, head_ = 0, tail_ = 0, window_ = 1;
    int64_t n_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------
// Moving average (boxcar) over a fixed window with a double accumulator, re-summed periodically against drift.
// ------------------------------------------------------------------------------------------------------------------
class Boxcar {
public:
    void prepare(int maxWindow) {
        buf_.assign(static_cast<size_t>(maxWindow + 1), 1.0f);
        cap_ = maxWindow + 1;
        setWindow(1);
    }
    void setWindow(int w) noexcept {
        window_ = std::max(1, std::min(w, cap_));
        reset(1.0f);
    }
    void reset(float v) noexcept {
        std::fill(buf_.begin(), buf_.end(), v);
        sum_ = static_cast<double>(v) * window_;
        pos_ = 0;
        count_ = 0;
    }
    inline float push(float v) noexcept {
        sum_ += static_cast<double>(v) - buf_[static_cast<size_t>(pos_)];
        buf_[static_cast<size_t>(pos_)] = v;
        pos_ = (pos_ + 1 == window_) ? 0 : pos_ + 1;
        if (++count_ >= 65536) {  // re-sum against accumulated rounding
            count_ = 0;
            double s = 0;
            for (int i = 0; i < window_; ++i) s += buf_[static_cast<size_t>(i)];
            sum_ = s;
        }
        return static_cast<float>(sum_ / window_);
    }

private:
    std::vector<float> buf_;
    double sum_ = 0;
    int cap_ = 1, window_ = 1, pos_ = 0, count_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------
// Envelope follower (peak or mean-square) with attack / release.
// ------------------------------------------------------------------------------------------------------------------
class EnvelopeFollower {
public:
    void prepare(double rate, float attackMs, float releaseMs) noexcept {
        rate_ = rate;
        setTimes(attackMs, releaseMs);
    }
    void setTimes(float attackMs, float releaseMs) noexcept {
        att_ = onePoleCoef(attackMs, rate_);
        rel_ = onePoleCoef(releaseMs, rate_);
    }
    void reset(float v = 0.0f) noexcept { env_ = v; }
    inline float process(float x) noexcept {
        env_ += (x > env_ ? att_ : rel_) * (x - env_);
        return env_;
    }
    float value() const noexcept { return env_; }

private:
    double rate_ = 48000;
    float att_ = 1.0f, rel_ = 1.0f, env_ = 0.0f;
};

} // namespace fm1deck::dsp
