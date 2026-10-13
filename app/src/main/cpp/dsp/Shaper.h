#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// Waveshaping curves with closed-form antiderivatives and first-order antiderivative anti-aliasing (ADAA1:
// Parker, Zavalishin, Le Bihan, DAFx 2016).
//
// ADAA is applied to the curve's *residual* r(x) = f(x) - x only; the linear part passes untouched. Plain ADAA1 of
// f is, in the curve's linear region, the two-tap average (x[n] + x[n-1]) / 2: -2 dB at 10 kHz and -11 dB at 20 kHz at
// 48 kHz. That is unacceptable on a master bus at Q x1. The residual is zero (hard clip) or tiny (tanh, algebraic) at
// normal levels, so the bus stays transparent there and the aliasing of the clipped part is still reduced.
// (MASTER-BUS.md §1.4, decision 2026-10-12.)

#include <cmath>

namespace fm1deck::dsp {

// ---------------------------------------------------------------------------------------------------------------
// The saturation family (MASTER-BUS §1.4): shape 0..0.5 hardclip -> tanh, 0.5..1 tanh -> x / sqrt(1 + x^2).
// ---------------------------------------------------------------------------------------------------------------
struct SatCurve {
    double wHard = 0, wTanh = 1, wAlg = 0;
    /** Below this |x| the residual is -cubic() x^3 within -70 dB (the next terms of tanh / algebraic). */
    static constexpr double kSmall = 0.2;
    /** The fast-path limit: kSmall, or 1 for a pure hard clip (exactly linear below 1). */
    double small() const noexcept { return (wTanh == 0 && wAlg == 0) ? 1.0 : kSmall; }
    double cubic() const noexcept { return wTanh / 3.0 + wAlg / 2.0; }

    void setShape(float shape) noexcept {
        const double s = shape < 0 ? 0 : (shape > 1 ? 1 : shape);
        if (s <= 0.5) { const double w = 2.0 * s; wHard = 1.0 - w; wTanh = w; wAlg = 0; }
        else { const double w = 2.0 * s - 1.0; wHard = 0; wTanh = 1.0 - w; wAlg = w; }
    }

    static double hard(double x) noexcept { return x > 1 ? 1 : (x < -1 ? -1 : x); }
    static double hardF(double x) noexcept { const double a = std::fabs(x); return a <= 1 ? 0.5 * x * x : a - 0.5; }
    /** log cosh x; the transcendental part in float (ADAA's difference quotient stays double, see kAdaaEps). */
    static double logCosh(double x) noexcept {
        const double a = std::fabs(x);
        if (a > 9.0) return a - 0.69314718055994530942;  // e^(-2a) < 2e-8
        // log1p(e) = 2 atanh(s), s = e / (2 + e) in (0, 1/3]: the odd series to s^11 is within 5e-8
        const float e = std::exp(static_cast<float>(-2.0 * a));
        const float s = e / (2.0f + e), s2 = s * s;
        const float l = 2.0f * s * (1.0f + s2 * (1.0f / 3 + s2 * (1.0f / 5 + s2 * (1.0f / 7 + s2 * (1.0f / 9 + s2 * (1.0f / 11))))));
        return a + static_cast<double>(l) - 0.69314718055994530942;
    }
    static double alg(double x) noexcept { return x / std::sqrt(1.0 + x * x); }
    static double algF(double x) noexcept { return x * x / (std::sqrt(1.0 + x * x) + 1.0); }  // sqrt(1+x^2) - 1, no cancellation

    double f(double x) const noexcept {
        double y = 0;
        if (wHard != 0) y += wHard * hard(x);
        if (wTanh != 0) y += wTanh * static_cast<double>(std::tanh(static_cast<float>(x)));
        if (wAlg != 0) y += wAlg * alg(x);
        return y;
    }
    double F(double x) const noexcept {
        double y = 0;
        if (wHard != 0) y += wHard * hardF(x);
        if (wTanh != 0) y += wTanh * logCosh(x);
        if (wAlg != 0) y += wAlg * algF(x);
        return y;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The ceiling clipper (MASTER-BUS §4): linear up to the knee k = c * 10^(-3 * shape / 20), then
// k + d * tanh((|x| - k) / d) towards c (d = c - k); shape 0 = hard clip at c.
// ---------------------------------------------------------------------------------------------------------------
struct CeilingCurve {
    double c = 1, k = 1, d = 0;

    void set(float ceiling, float shape) noexcept {
        c = ceiling;
        const double s = shape < 0 ? 0 : (shape > 1 ? 1 : shape);
        k = c * std::pow(10.0, -3.0 * s / 20.0);
        d = c - k;
        if (d < 1e-9) { k = c; d = 0; }
    }
    /** Up to the knee the curve is exactly linear: residual 0. */
    double small() const noexcept { return k; }
    double cubic() const noexcept { return 0.0; }
    double f(double x) const noexcept {
        const double a = std::fabs(x);
        if (a <= k) return x;
        const double m = d > 0 ? k + d * std::tanh((a - k) / d) : c;
        return x < 0 ? -m : m;
    }
    double F(double x) const noexcept {
        const double a = std::fabs(x);
        if (a <= k) return 0.5 * x * x;
        if (d <= 0) return 0.5 * c * c + c * (a - c);
        return 0.5 * k * k + k * (a - k) + d * d * SatCurve::logCosh((a - k) / d);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Residual ADAA1 for one channel: y = x + (R(x) - R(x1)) / (x - x1), R(x) = F(x) - x^2 / 2; mid-point fallback.
// ---------------------------------------------------------------------------------------------------------------
template <class Curve>
class ResidualAdaa {
public:
    /**
     * Below this step the mid-point formula is used. The antiderivatives carry float-precision transcendental
     * parts (~1e-7 absolute), so the quotient's error stays under 1e-4 (-80 dB); the mid-point error is O(dx^2).
     */
    static constexpr double kAdaaEps = 1e-3;

    void reset() noexcept { x1_ = 0; R1_ = 0; r1Valid_ = true; }
    /** Call after the curve changes so the stored antiderivative matches it. */
    void refresh(const Curve& c) noexcept { R1_ = c.F(x1_) - 0.5 * x1_ * x1_; r1Valid_ = true; }

    /** Below smallLimit (both samples) the residual is the cubic term: no transcendental functions. */
    inline double process(const Curve& c, double x, double smallLimit, double c3) noexcept {
        if (std::fabs(x) < smallLimit && std::fabs(x1_) < smallLimit) {
            const double m = 0.5 * (x + x1_);
            x1_ = x;
            r1Valid_ = false;
            return x - c3 * m * m * m;
        }
        if (!r1Valid_) { R1_ = c.F(x1_) - 0.5 * x1_ * x1_; r1Valid_ = true; }
        return process(c, x);
    }

    inline double process(const Curve& c, double x) noexcept {
        const double R = c.F(x) - 0.5 * x * x;
        const double dx = x - x1_;
        double r;
        if (std::fabs(dx) > kAdaaEps) {
            r = (R - R1_) / dx;
        } else {
            const double m = 0.5 * (x + x1_);
            r = c.f(m) - m;
        }
        x1_ = x;
        R1_ = R;
        return x + r;
    }

private:
    double x1_ = 0, R1_ = 0;
    bool r1Valid_ = true;
};

} // namespace fm1deck::dsp
