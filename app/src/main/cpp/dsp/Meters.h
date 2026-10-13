#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// True-peak detector (4x polyphase interpolation, in the spirit of ITU-R BS.1770-4 Annex 2) and the BS.1770
// short-term loudness meter (K-weighting, 100 ms blocks, 3 s window).

#include "DspCore.h"

#include <array>

namespace fm1deck::dsp {

/**
 * 4x interpolator over a 193-tap Kaiser-windowed sinc with its cut-off at the base Nyquist. BS.1770's 48 taps
 * under-read peaks of content near Nyquist (a spectral clipper's output) by more than the 0.1 dB the limiter
 * promises, so the filter is longer. With 8 * 24 + 1 taps the group delay is exactly kDelay = 24 base samples
 * (0.5 ms at 48 kHz) and phase 0 is a pure delay (sinc zeros): process(x[n]) returns the true peak on both sides of
 * sample n - kDelay (the segments [n - kDelay - 1, n - kDelay] and [n - kDelay, n - kDelay + 1]).
 */
template <int Delay>
class TruePeakDetectorT {
public:
    static constexpr int kDelay = Delay;              // base samples
    static constexpr int kTaps = 8 * kDelay + 1;     // 193
    static constexpr int kPhaseTaps = 2 * kDelay + 1; // 49 per phase
    static constexpr int kHist = Delay > 8 ? 64 : 16; // power of two > kPhaseTaps

    TruePeakDetectorT() {
        auto bessel0 = [](double x) {
            double s = 1, t = 1;
            for (int i = 1; i < 50; ++i) { t *= (x / (2.0 * i)) * (x / (2.0 * i)); s += t; if (t < 1e-12 * s) break; }
            return s;
        };
        const double beta = 9.0, i0b = bessel0(beta);
        for (int k = 0; k < kTaps; ++k) {
            const double m = (k - 4 * kDelay) / 4.0;
            const double sinc = m == 0 ? 1.0 : std::sin(kPi * m) / (kPi * m);
            const double r = 2.0 * k / (kTaps - 1) - 1.0;
            const double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            const double h = sinc * w;
            const int phase = k % 4, j = k / 4;
            coef_[static_cast<size_t>(phase)][static_cast<size_t>(j)] = static_cast<float>(h);
        }
        // Phase 0 is exactly a delay of kDelay base samples.
        for (auto& c : coef_[0]) c = 0;
        coef_[0][kDelay] = 1.0f;
        reset();
    }

    void reset() noexcept { hist_.fill(0.0f); prev_.fill(0.0f); pos_ = 0; prev2_ = 0; }

    /**
     * Feeds one base sample; returns the true peak around sample n - 6 (both neighbouring segments). The 4x points
     * alone under-read a peak between them by up to cos(pi f / 4 fs) (0.14 dB at 11 kHz), so each local maximum is
     * refined with a parabola through its neighbours.
     */
    inline float process(float x, float skipBelow = 0.0f) noexcept {
        pos_ = (pos_ + 1) & (kHist - 1);
        hist_[static_cast<size_t>(pos_)] = x;
        if (skipBelow > 0.0f) {
            // More than 6 dB under the ceiling (the caller passes skipBelow = ceiling / 2) an inter-sample peak
            // cannot reach it in practice: the samples around n - kDelay suffice.
            const float s = std::max(std::fabs(at(kDelay - 1)), std::max(std::fabs(at(kDelay)), std::fabs(at(kDelay + 1))));
            if (s < skipBelow) {
                prev2_ = prev_[3];
                prev_.fill(s);
                return s;
            }
        }
        std::array<float, 9> seq{};  // [last point of the segment before] + previous 4 + current 4
        seq[0] = prev2_;
        for (int p = 0; p < 4; ++p) seq[static_cast<size_t>(1 + p)] = prev_[static_cast<size_t>(p)];
        for (int p = 0; p < 4; ++p) {
            float acc;
            if (p == 0) acc = at(kDelay);
            else {
                acc = 0;
                for (int j = 0; j < kPhaseTaps; ++j) acc += coef_[static_cast<size_t>(p)][static_cast<size_t>(j)] * at(j);
            }
            seq[static_cast<size_t>(5 + p)] = std::fabs(acc);
        }
        prev2_ = prev_[3];
        float peak = 0;
        for (int i = 1; i < 9; ++i) {
            const float y1 = seq[static_cast<size_t>(i)];
            peak = std::max(peak, y1);
            if (i < 8) {
                const float y0 = seq[static_cast<size_t>(i - 1)], y2 = seq[static_cast<size_t>(i + 1)];
                const float den = y0 - 2.0f * y1 + y2;
                if (y1 >= y0 && y1 >= y2 && den < -1e-9f) {
                    const float d = y0 - y2;
                    peak = std::max(peak, y1 - d * d / (8.0f * den));
                }
            }
        }
        for (int p = 0; p < 4; ++p) prev_[static_cast<size_t>(p)] = seq[static_cast<size_t>(5 + p)];
        return peak;
    }

    /** Sample-peak mode with the same delay: |x[n - kDelay]|. */
    inline float processSample(float x) noexcept {
        pos_ = (pos_ + 1) & (kHist - 1);
        hist_[static_cast<size_t>(pos_)] = x;
        return std::fabs(at(kDelay));
    }

private:
    inline float at(int j) const noexcept { return hist_[static_cast<size_t>((pos_ - j) & (kHist - 1))]; }
    std::array<std::array<float, kPhaseTaps>, 4> coef_{};
    std::array<float, kHist> hist_{};
    std::array<float, 4> prev_{};
    int pos_ = 0;
    float prev2_ = 0;
};

/** BS.1770 short-term loudness (3 s), stereo, updated per 100 ms block. */
class LufsMeter {
public:
    void prepare(double rate) {
        shelf_ = BiquadCoefs::kShelf(rate);
        hp_ = BiquadCoefs::kHighpass(rate);
        blockLen_ = static_cast<int>(rate * 0.1);
        reset();
    }
    void reset() noexcept {
        for (auto& s : st_) s.reset();
        blocks_.fill(0.0);
        acc_ = 0; count_ = 0; filled_ = 0; head_ = 0;
        lufs_ = -120.0f;
    }
    inline void process(float l, float r) noexcept {
        const float kl = st_[1].process(hp_, st_[0].process(shelf_, l));
        const float kr = st_[3].process(hp_, st_[2].process(shelf_, r));
        acc_ += static_cast<double>(kl) * kl + static_cast<double>(kr) * kr;
        if (++count_ >= blockLen_) {
            blocks_[static_cast<size_t>(head_)] = acc_ / count_;
            head_ = (head_ + 1) % 30;
            filled_ = std::min(filled_ + 1, 30);
            acc_ = 0; count_ = 0;
            double sum = 0;
            for (int i = 0; i < filled_; ++i) sum += blocks_[static_cast<size_t>(i)];
            const double ms = sum / filled_;
            lufs_ = ms > 1e-12 ? static_cast<float>(-0.691 + 10.0 * std::log10(ms)) : -120.0f;
        }
    }
    float shortTerm() const noexcept { return lufs_; }

private:
    BiquadCoefs shelf_, hp_;
    std::array<BiquadState, 4> st_{};
    std::array<double, 30> blocks_{};
    double acc_ = 0;
    int blockLen_ = 4800, count_ = 0, filled_ = 0, head_ = 0;
    float lufs_ = -120.0f;
};

/** The limiter's detector: 193 taps, accurate near Nyquist (0.5 ms of latency). */
using TruePeakDetector = TruePeakDetectorT<24>;
/** The output meter's: 49 taps (the BS.1770 length); it delays nothing audible. */
using TruePeakMeter = TruePeakDetectorT<6>;

} // namespace fm1deck::dsp
