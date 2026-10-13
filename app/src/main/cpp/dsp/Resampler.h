#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Ported from the MwResampler of GGBOX (an earlier port of these firmwares by the same author).
// 44.1 kHz (the firmware's FS) to the stream's rate: 32 taps, 128 phases of a Kaiser-windowed sinc (cutoff 0.45 of
// the input rate, about 19.8 kHz), linear between phases, unity gain at DC. Same rate: a plain copy.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace fm1deck {

class Resampler {
public:
    static constexpr int kTaps = 32;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kPhases = 128;
    static constexpr int kMaxIn = 1024;

    Resampler() { buildTable(0.45); }

    // Input frame f (from the read position's origin) sits at in[f + kHalf - 1]; frames -15..-1 start silent.
    void reset(double inRate, double outRate) {
        step_ = inRate / std::max(outRate, 1.0);
        bypass_ = std::fabs(step_ - 1.0) < 1.0e-9;
        pos_ = 0.0;
        count_ = kHalf - 1;
        std::fill(inL_.begin(), inL_.end(), 0.0f);
        std::fill(inR_.begin(), inR_.end(), 0.0f);
    }

    // Input frames needed for the next n output frames: output at x reads frames floor(x)-15 .. floor(x)+16.
    int inputNeeded(int n) const noexcept {
        if (bypass_) return n;
        const int last = static_cast<int>(std::floor(pos_ + (n - 1) * step_));
        return std::max(0, last + kTaps - count_);
    }

    float* inputTailL() noexcept { return inL_.data() + (bypass_ ? 0 : count_); }
    float* inputTailR() noexcept { return inR_.data() + (bypass_ ? 0 : count_); }
    void commitInput(int frames) noexcept { if (!bypass_) count_ += frames; }

    // n output frames from the input given since the last call (inputNeeded(n) of them).
    void process(int n, float* outL, float* outR) noexcept {
        if (bypass_) {
            std::memcpy(outL, inL_.data(), sizeof(float) * static_cast<size_t>(n));
            std::memcpy(outR, inR_.data(), sizeof(float) * static_cast<size_t>(n));
            return;
        }
        for (int k = 0; k < n; ++k) {
            const double x = pos_ + k * step_;
            const int i = static_cast<int>(x);
            const double frac = x - i;
            const double ph = frac * kPhases;
            const int p = std::min(static_cast<int>(ph), kPhases - 1);
            const float a = static_cast<float>(ph - p);
            const float* c0 = table_[static_cast<size_t>(p)].data();
            const float* c1 = table_[static_cast<size_t>(p + 1)].data();
            const float* l = inL_.data() + i;  // taps: frames i-15 .. i+16
            const float* r = inR_.data() + i;
            float sl = 0.0f, sr = 0.0f;
            for (int t = 0; t < kTaps; ++t) {
                const float c = c0[t] + a * (c1[t] - c0[t]);
                sl += c * l[t];
                sr += c * r[t];
            }
            outL[k] = sl;
            outR[k] = sr;
        }
        pos_ += n * step_;
        // Keep what the next block's first output reaches back to (frame floor(pos)-15 moves to index 0).
        const int drop = std::max(0, static_cast<int>(pos_));
        if (drop > 0) {
            const int keep = count_ - drop;
            std::memmove(inL_.data(), inL_.data() + drop, sizeof(float) * static_cast<size_t>(keep));
            std::memmove(inR_.data(), inR_.data() + drop, sizeof(float) * static_cast<size_t>(keep));
            count_ = keep;
            pos_ -= drop;
        }
    }

private:
    static double besselI0(double x) noexcept {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 32; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1.0e-12 * sum) break;
        }
        return sum;
    }

    void buildTable(double cutoff) {
        constexpr double kBeta = 7.5;
        constexpr double kPi = 3.14159265358979323846;
        const double i0b = besselI0(kBeta);
        for (int p = 0; p <= kPhases; ++p) {
            const double frac = static_cast<double>(p) / kPhases;
            double sum = 0.0;
            for (int t = 0; t < kTaps; ++t) {
                const double d = (t - (kHalf - 1)) - frac;  // tap frame minus the output position
                const double u = d / kHalf;
                const double w = (std::fabs(u) >= 1.0) ? 0.0 : besselI0(kBeta * std::sqrt(1.0 - u * u)) / i0b;
                const double xx = 2.0 * cutoff * d;
                const double sinc = std::fabs(xx) < 1.0e-12 ? 1.0 : std::sin(kPi * xx) / (kPi * xx);
                const double h = 2.0 * cutoff * sinc * w;
                table_[static_cast<size_t>(p)][static_cast<size_t>(t)] = static_cast<float>(h);
                sum += h;
            }
            for (int t = 0; t < kTaps; ++t)
                table_[static_cast<size_t>(p)][static_cast<size_t>(t)] =
                        static_cast<float>(table_[static_cast<size_t>(p)][static_cast<size_t>(t)] / sum);
        }
    }

    std::array<std::array<float, kTaps>, kPhases + 1> table_{};
    std::array<float, kMaxIn + 2 * kTaps> inL_{};
    std::array<float, kMaxIn + 2 * kTaps> inR_{};
    double step_ = 44100.0 / 48000.0;
    double pos_ = 0.0;
    int count_ = kTaps;
    bool bypass_ = false;
};

} // namespace fm1deck
