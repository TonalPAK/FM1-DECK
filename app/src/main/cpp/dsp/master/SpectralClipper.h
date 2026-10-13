#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// MASTER-BUS §2: spectral clipper at the base rate. LR4 crossover tree (sum = all-pass), each band clipped at its own
// ceiling c_b = C * w_b with the SPC_SHAPE curve (residual ADAA1), SPREAD blends from a broadband clipper (one gain for
// the whole sum) to independent bands, PEAK_MIX returns part of the clipped transients.

#include "../Crossover.h"
#include "../DspCore.h"
#include "../Shaper.h"

#include <array>

namespace fm1deck::dsp {

class SpectralClipper {
public:
    static constexpr int kMaxBlock = CrossoverTree::kMaxBlock;

    void prepare(double rate) {
        rate_ = rate;
        ceiling_.prepare(rate, 20.0f);
        ceiling_.reset(dbToLin(-1.0f));
        spread_.prepare(rate, 20.0f);
        spread_.reset(0.7f);
        peakMix_.prepare(rate, 20.0f);
        peakMix_.reset(0.0f);
        fast_.prepare(rate, 1.0f, 20.0f);
        slow_.prepare(rate, 20.0f, 200.0f);
        setBands(0);
    }

    /** SPC_BANDS 0/1/2 -> 4/6/8 bands; the bus fades the stage around this. */
    void setBands(int index) noexcept {
        bandsIndex_ = index;
        xo_.configure(index, rate_);
        for (int b = 0; b < xo_.bands(); ++b) {
            // band centre: geometric mean of its edges (30 Hz .. 18 kHz outside)
            const double lo = b == 0 ? 30.0 : xo_.splitFreq(b - 1);
            const double hi = b == xo_.bands() - 1 ? 18000.0 : xo_.splitFreq(b);
            weight_[static_cast<size_t>(b)] = static_cast<float>(bandWeight(std::sqrt(lo * hi)));
        }
        reset();
    }
    int bandsIndex() const noexcept { return bandsIndex_; }

    void reset() noexcept {
        xo_.reset();
        for (auto& ch : adaa_) for (auto& a : ch) a.reset();
        fast_.reset(0); slow_.reset(0);
    }

    void setCeilingDb(float db) noexcept { ceiling_.setTarget(dbToLin(db)); }
    void setShape(float s) noexcept { shape_ = s; }
    void setSpread(float s) noexcept { spread_.setTarget(s); }
    void setPeakMix(float m) noexcept { peakMix_.setTarget(m); }
    void setPeakSens(float s) noexcept { peakK_ = std::pow(2.0f, 1.0f + 4.0f * std::clamp(s, 0.0f, 1.0f)); }

    void process(float* l, float* r, int n) noexcept {
        curve_.setShape(shape_);
        const float C = ceiling_.advance(n);
        const float spread = spread_.advance(n);
        const float peakMix = peakMix_.advance(n);
        const int nb = xo_.bands();
        const double small = curve_.small(), c3 = curve_.cubic();
        for (int c = 0; c < 2; ++c) {
            xo_.split(c, c == 0 ? l : r, n, bands_[static_cast<size_t>(c)]);
            for (int b = 0; b < nb; ++b) adaa_[static_cast<size_t>(c)][static_cast<size_t>(b)].refresh(curve_);
        }
        double sumIn = 0, sumOut = 0;
        for (int i = 0; i < n; ++i) {
            float xs[2], out[2];
            for (int c = 0; c < 2; ++c) {
                auto& bd = bands_[static_cast<size_t>(c)];
                float sum = 0, clipped = 0;
                for (int b = 0; b < nb; ++b) {
                    const float xb = bd[static_cast<size_t>(b)][static_cast<size_t>(i)];
                    sum += xb;
                    const float cb = C * weight_[static_cast<size_t>(b)];
                    clipped += cb * static_cast<float>(
                        adaa_[static_cast<size_t>(c)][static_cast<size_t>(b)].process(curve_, xb / cb, small, c3));
                }
                // broadband: one gain for the whole sum (a classic clipper), plain curve; cubic below `small`
                const float us = sum / C;
                float gbb;
                if (std::fabs(us) < small) gbb = 1.0f - static_cast<float>(c3) * us * us;
                else gbb = std::clamp(static_cast<float>(curve_.f(us)) / us, 0.0f, 1.0f);
                xs[c] = sum;
                out[c] = (1.0f - spread) * gbb * sum + spread * clipped;
            }
            // Peak return on the linked mid envelope
            const float m = 0.5f * (std::fabs(xs[0]) + std::fabs(xs[1]));
            const float ef = fast_.process(m), es = slow_.process(m);
            const float t = std::clamp((ef - es) / (es + 1e-9f) * peakK_, 0.0f, 1.0f);
            for (int c = 0; c < 2; ++c) {
                const float y = out[c] + peakMix * t * (xs[c] - out[c]);
                sumIn += static_cast<double>(xs[c]) * xs[c];
                sumOut += static_cast<double>(y) * y;
                (c == 0 ? l : r)[i] = y;
            }
        }
        grDb_ = sumIn > 1e-12 ? static_cast<float>(10.0 * std::log10(std::max(sumOut, 1e-30) / sumIn)) : 0.0f;
        grDb_ = std::min(grDb_, 0.0f);
    }

    float grDb() const noexcept { return grDb_; }
    void settle() noexcept { ceiling_.settle(); spread_.settle(); peakMix_.settle(); }

private:
    /**
     * Band ceiling weights, hand-set after the equal-loudness contours (ISO 226): the lows clip earlier so the bass
     * does not take the headroom of the mids and highs (0.55 at 60 Hz rising to 1.0 at 1 kHz, flat to 5 kHz,
     * easing to 0.9 at 16 kHz).
     */
    static double bandWeight(double fc) noexcept {
        if (fc <= 1000.0) {
            const double t = std::clamp(std::log2(fc / 60.0) / std::log2(1000.0 / 60.0), 0.0, 1.0);
            return 0.55 + 0.45 * t;
        }
        if (fc <= 5000.0) return 1.0;
        const double t = std::clamp(std::log2(fc / 5000.0) / std::log2(16000.0 / 5000.0), 0.0, 1.0);
        return 1.0 - 0.1 * t;
    }

    double rate_ = 48000;
    CrossoverTree xo_;
    std::array<std::array<std::array<float, kMaxBlock>, CrossoverTree::kMaxBands>, 2> bands_{};
    std::array<std::array<ResidualAdaa<SatCurve>, CrossoverTree::kMaxBands>, 2> adaa_{};
    std::array<float, CrossoverTree::kMaxBands> weight_{};
    SatCurve curve_;
    Smoother ceiling_, spread_, peakMix_;
    EnvelopeFollower fast_, slow_;
    float shape_ = 0.5f, peakK_ = 8.0f, grDb_ = 0;
    int bandsIndex_ = 0;
};

} // namespace fm1deck::dsp
