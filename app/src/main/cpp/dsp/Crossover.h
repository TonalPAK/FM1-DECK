#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// Linkwitz-Riley 4th-order crossover tree with all-pass compensation (MASTER-BUS §2.1). Each split is two cascaded
// Butterworth sections; LP4 + HP4 = a 2nd-order all-pass at the split (Q = 1/sqrt 2), exactly also after the bilinear
// transform. Splitting the remainder in turn, band k (from the k-th split's low-pass) is passed through the
// all-passes of splits k+1 .. M-1, so the bands sum to AP_0 * AP_1 * ... * AP_{M-1}: flat magnitude, one phase.

#include "DspCore.h"

#include <array>

namespace fm1deck::dsp {

class CrossoverTree {
public:
    static constexpr int kMaxSplits = 7;
    static constexpr int kMaxBands = kMaxSplits + 1;
    static constexpr int kMaxBlock = 64;

    /** Split frequencies for SPC_BANDS 0 / 1 / 2 (4, 6, 8 bands). */
    static int splitsFor(int bandsIndex, const double** freqs) noexcept {
        static const double f4[] = {150, 1000, 5000};
        static const double f6[] = {120, 400, 1200, 3500, 9000};
        static const double f8[] = {90, 220, 500, 1100, 2400, 5000, 10000};
        switch (bandsIndex) {
            case 1: *freqs = f6; return 5;
            case 2: *freqs = f8; return 7;
            default: *freqs = f4; return 3;
        }
    }

    void configure(int bandsIndex, double rate) noexcept {
        const double* f = nullptr;
        splits_ = splitsFor(bandsIndex, &f);
        for (int k = 0; k < splits_; ++k) {
            const double fk = std::min(f[k], rate * 0.45);
            freq_[static_cast<size_t>(k)] = fk;
            lp_[static_cast<size_t>(k)] = BiquadCoefs::lowpass(fk, 0.70710678118654752, rate);
            hp_[static_cast<size_t>(k)] = BiquadCoefs::highpass(fk, 0.70710678118654752, rate);
            ap_[static_cast<size_t>(k)] = BiquadCoefs::allpass(fk, 0.70710678118654752, rate);
        }
        reset();
    }

    void reset() noexcept {
        for (auto& ch : state_) for (auto& s : ch) s.reset();
    }

    int bands() const noexcept { return splits_ + 1; }
    double splitFreq(int k) const noexcept { return freq_[static_cast<size_t>(k)]; }

    /** Splits one channel's block (n <= kMaxBlock) into bands()[b][i]. */
    void split(int ch, const float* in, int n, std::array<std::array<float, kMaxBlock>, kMaxBands>& out) noexcept {
        auto& st = state_[static_cast<size_t>(ch)];
        std::array<float, kMaxBlock> rest{};
        std::copy(in, in + n, rest.begin());
        int si = 0;  // state index
        for (int k = 0; k < splits_; ++k) {
            auto& band = out[static_cast<size_t>(k)];
            std::copy(rest.begin(), rest.begin() + n, band.begin());
            st[static_cast<size_t>(si++)].processBlock(lp_[static_cast<size_t>(k)], band.data(), n);
            st[static_cast<size_t>(si++)].processBlock(lp_[static_cast<size_t>(k)], band.data(), n);
            st[static_cast<size_t>(si++)].processBlock(hp_[static_cast<size_t>(k)], rest.data(), n);
            st[static_cast<size_t>(si++)].processBlock(hp_[static_cast<size_t>(k)], rest.data(), n);
        }
        std::copy(rest.begin(), rest.begin() + n, out[static_cast<size_t>(splits_)].begin());
        // All-pass compensation: band k through the all-passes of splits k+1 .. M-1
        for (int k = 0; k + 1 < splits_; ++k) {
            for (int j = k + 1; j < splits_; ++j) {
                st[static_cast<size_t>(si++)].processBlock(ap_[static_cast<size_t>(j)], out[static_cast<size_t>(k)].data(), n);
            }
        }
    }

private:
    static constexpr int kMaxStates = 4 * kMaxSplits + kMaxSplits * (kMaxSplits - 1) / 2;
    std::array<BiquadCoefs, kMaxSplits> lp_{}, hp_{}, ap_{};
    std::array<double, kMaxSplits> freq_{};
    std::array<std::array<BiquadState, kMaxStates>, 2> state_{};
    int splits_ = 3;
};

} // namespace fm1deck::dsp
