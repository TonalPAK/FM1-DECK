#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// Oversampler x1 / x2 / x4 / x8: a cascade of linear-phase half-band FIR stages (Kaiser-windowed sinc). Own code
// instead of HIIR's polyphase IIR (decision 2026-10-12, MASTER-BUS.md): linear phase means the dry path of a
// parallel mix (SAT_MIX, MIX) lines up by a plain integer delay, with no comb filtering.
//
// Stage s runs between rates 2^(s-1) Fe and 2^s Fe. A half-band FIR of N = 4m + 3 taps has its centre tap 0.5 and
// every other tap zero, so up- and down-sampling cost about N / 2 multiply-adds per input pair. The round trip of a
// stage is N - 1 samples at its high rate. The cascade pads the top rate with a delay so that the total latency is
// an integer number of base-rate samples (latency()).

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fm1deck::dsp {

/** One half-band stage: interpolate by 2 and decimate by 2 (two independent filters sharing the coefficients). */
class HalfbandStage {
public:
    /** taps = 4m + 3; beta = Kaiser window parameter. */
    void design(int taps, double beta) {
        n_ = taps;
        c_ = (taps - 1) / 2;  // odd for 4m + 3
        // Even-index taps (k even, k - c odd) are the only non-zero ones besides the centre.
        even_.clear();
        auto bessel0 = [](double x) {
            double s = 1, t = 1;
            for (int i = 1; i < 50; ++i) { t *= (x / (2.0 * i)) * (x / (2.0 * i)); s += t; if (t < 1e-12 * s) break; }
            return s;
        };
        const double i0b = bessel0(beta);
        double sum = 0;
        for (int k = 0; k < taps; k += 2) {
            const double m = k - c_;
            const double sinc = std::sin(kPiLocal * m / 2.0) / (kPiLocal * m / 2.0);
            const double r = 2.0 * k / (taps - 1) - 1.0;
            const double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            const double h = 0.5 * sinc * w;
            even_.push_back(static_cast<float>(h));
            sum += h;
        }
        // Normalise so the even taps sum to 0.5 (unity DC gain with the 0.5 centre tap).
        for (auto& h : even_) h = static_cast<float>(h * (0.5 / sum));
        hist_ = static_cast<int>(even_.size());
        upBuf_.assign(static_cast<size_t>(2 * hist_ + 2 * kMaxIn), 0.0f);
        dnEven_.assign(static_cast<size_t>(2 * hist_ + 2 * kMaxIn), 0.0f);
        dnOdd_.assign(static_cast<size_t>(c_ + 2 * kMaxIn + 2), 0.0f);
        reset();
    }

    void reset() {
        std::fill(upBuf_.begin(), upBuf_.end(), 0.0f);
        std::fill(dnEven_.begin(), dnEven_.end(), 0.0f);
        std::fill(dnOdd_.begin(), dnOdd_.end(), 0.0f);
    }

    int taps() const noexcept { return n_; }
    /** Round-trip latency in samples of the high rate. */
    int roundTrip() const noexcept { return 2 * c_; }

    /**
     * Interpolate n input samples into 2n output samples.
     * Zero-stuffed u[2i] = x[i]: y[2i] = 2 sum_{k even} h[k] x[i - k/2]; y[2i+1] = x[i - (c-1)/2].
     */
    void up(const float* in, float* out, int n) noexcept {
        float* h = upBuf_.data();
        // history of the last hist_ inputs lives at h[0 .. hist_-1]; append the new block after it
        std::copy(in, in + n, h + hist_);
        const int half = (c_ - 1) / 2;
        for (int i = 0; i < n; ++i) {
            const float* x = h + hist_ + i;  // x[0] = current input, x[-j] = j samples ago
            float acc = 0;
            for (int j = 0; j < hist_; ++j) acc += even_[static_cast<size_t>(j)] * x[-j];
            out[2 * i] = 2.0f * acc;
            out[2 * i + 1] = x[-half];
        }
        std::copy(h + n, h + n + hist_, h);
    }

    /**
     * Decimate 2n input samples into n outputs: y[i] = sum_{k even} h[k] v[2i - k] + 0.5 v[2i - c].
     * v[2i - k] (k even) runs over the even-phase inputs; v[2i - c] (c odd) over the odd phase.
     */
    void down(const float* in, float* out, int n) noexcept {
        float* e = dnEven_.data();
        float* o = dnOdd_.data();
        const int oh = (c_ + 1) / 2;  // odd-phase history length needed
        for (int i = 0; i < n; ++i) {
            e[hist_ + i] = in[2 * i];
            o[oh + i] = in[2 * i + 1];
        }
        for (int i = 0; i < n; ++i) {
            const float* x = e + hist_ + i;
            float acc = 0;
            for (int j = 0; j < hist_; ++j) acc += even_[static_cast<size_t>(j)] * x[-j];
            // v[2i - c]: odd sample index 2i - c = 2(i - oh) + 1 -> odd-phase entry i - oh
            acc += 0.5f * o[oh + i - oh];
            out[i] = acc;
        }
        std::copy(e + n, e + n + hist_, e);
        std::copy(o + n, o + n + oh, o);
    }

    static constexpr int kMaxIn = 512;

private:
    static constexpr double kPiLocal = 3.14159265358979323846;
    std::vector<float> even_;
    std::vector<float> upBuf_, dnEven_, dnOdd_;
    int n_ = 3, c_ = 1, hist_ = 1;
};

/** Mono oversampler with factor 1, 2, 4 or 8 and an integer base-rate latency. */
class Oversampler {
public:
    static constexpr int kMaxFactor = 8;

    /** maxBlock = base-rate block size (<= HalfbandStage::kMaxIn / 4 at x8 is not needed: stages see 2^s * block). */
    void prepare(int maxBlock) {
        maxBlock_ = std::min(maxBlock, 64);
        // Stage 1 carries the audio band edge: passband to 20 kHz at 48 kHz, stopband from 28 kHz (80 dB+).
        stages_[0].design(63, 8.0);
        stages_[1].design(19, 7.0);
        stages_[2].design(11, 6.0);
        bufA_.assign(static_cast<size_t>(maxBlock_ * kMaxFactor), 0.0f);
        bufB_.assign(static_cast<size_t>(maxBlock_ * kMaxFactor), 0.0f);
        pad_.prepare(kMaxFactor * 4);
        setFactor(1);
    }

    /** Called on the audio thread at a block boundary; the owner fades around it (latency changes). */
    void setFactor(int q) noexcept {
        factor_ = q >= 8 ? 8 : (q >= 4 ? 4 : (q >= 2 ? 2 : 1));
        stagesUsed_ = factor_ == 8 ? 3 : (factor_ == 4 ? 2 : (factor_ == 2 ? 1 : 0));
        int topSamples = 0;  // round-trip latency in top-rate samples
        for (int s = 0; s < stagesUsed_; ++s) topSamples += stages_[s].roundTrip() * (factor_ >> (s + 1));
        const int padTop = (factor_ - topSamples % factor_) % factor_;
        pad_.setDelay(padTop);
        latency_ = (topSamples + padTop) / factor_;
        reset();
    }

    void reset() noexcept {
        for (auto& s : stages_) s.reset();
        pad_.reset();
    }

    int factor() const noexcept { return factor_; }
    int latency() const noexcept { return latency_; }

    /** Upsample n base samples; returns a pointer to n * factor samples (valid until the next call). */
    float* up(const float* in, int n) noexcept {
        if (factor_ == 1) {
            std::copy(in, in + n, bufA_.data());
            return bufA_.data();
        }
        const float* src = in;
        int len = n;
        float* dst = bufA_.data();
        for (int s = 0; s < stagesUsed_; ++s) {
            stages_[s].up(src, dst, len);
            src = dst;
            len *= 2;
            dst = (dst == bufA_.data()) ? bufB_.data() : bufA_.data();
        }
        float* top = const_cast<float*>(src);
        for (int i = 0; i < len; ++i) top[i] = pad_.process(top[i]);
        return top;
    }

    /** Downsample n * factor top-rate samples (in place allowed) into n base samples. */
    void down(float* in, float* out, int n) noexcept {
        if (factor_ == 1) {
            std::copy(in, in + n, out);
            return;
        }
        // HalfbandStage::down() reads its whole input before writing, so the stages can work in place
        int len = n * factor_;
        for (int s = stagesUsed_ - 1; s >= 0; --s) {
            len /= 2;
            stages_[s].down(in, s == 0 ? out : in, len);
        }
    }

private:
    class PadDelay {
    public:
        void prepare(int max) { buf_.assign(static_cast<size_t>(max + 1), 0.0f); }
        void setDelay(int d) noexcept { d_ = d; pos_ = 0; }
        void reset() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }
        inline float process(float x) noexcept {
            if (d_ == 0) return x;
            const float y = buf_[static_cast<size_t>(pos_)];
            buf_[static_cast<size_t>(pos_)] = x;
            pos_ = (pos_ + 1 == d_) ? 0 : pos_ + 1;
            return y;
        }
    private:
        std::vector<float> buf_;
        int d_ = 0, pos_ = 0;
    };

    std::array<HalfbandStage, 3> stages_;
    std::vector<float> bufA_, bufB_;
    PadDelay pad_;
    int maxBlock_ = 64, factor_ = 1, stagesUsed_ = 0, latency_ = 0;
};

} // namespace fm1deck::dsp
