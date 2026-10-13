#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// Turns a beat timeline (Ableton Link's, docs/LINK.md) into the MIDI clock a firmware follows: 24 pulses (F8) per
// beat, Start (FA) on beat 0 of a transport start, Stop (FC). Audio thread only; no allocation, no locks.
//
// The engine calls step() once per firmware sub-block (64 frames) with the beats at the sub-block's start and end
// as heard (the host time already includes the output latency). A pulse is sent before the sub-block whose middle
// it falls before, so it lands within half a sub-block (0.7 ms at 48 kHz) of its time.

#include <cmath>
#include <cstdint>

namespace fm1deck {

class ClockPulser {
public:
    static constexpr int kPpqn = 24;
    static constexpr uint32_t kClock = 0xF8, kStart = 0xFA, kStop = 0xFC;

    void reset() noexcept { have_ = false; running_ = false; }

    bool running() const noexcept { return running_; }

    /**
     * b0, b1: the beat at the sub-block's start and end. playing: the session transport; its start is beat 0
     * (the caller maps it). emit(status) is called for each real-time byte to send now, in order.
     */
    template <class Emit>
    void step(double b0, double b1, bool playing, Emit&& emit) {
        const double now = 0.5 * (b0 + b1);
        const int64_t k = static_cast<int64_t>(std::floor(now * kPpqn));
        if (!have_) {
            // First call (or after a jump): no burst of old pulses, the next one comes on its time
            last_ = k;
            have_ = true;
        }
        // The timeline jumped: one pulse, then on time. A small step back (the output latency's estimate moved by a
        // few ms) is waited out: sending that pulse again would make the clock run fast
        if (k < last_ - kMaxBurst || k - last_ > kMaxBurst) last_ = k - 1;

        if (!playing && running_) {
            emit(kStop);
            running_ = false;
        }
        const bool startNow = playing && !running_ && now >= 0.0;
        if (startNow) {
            // Pulses before beat 0, then Start, then the downbeat's pulse (MIDI: the first clock after FA is beat 0)
            for (int64_t p = last_ + 1; p < 0 && p <= k; ++p) emit(kClock);
            if (last_ < -1) last_ = -1;
            emit(kStart);
            running_ = true;
        }
        for (int64_t p = last_ + 1; p <= k; ++p) emit(kClock);
        if (k > last_) last_ = k;  // (a step back keeps the count: those pulses are out)
    }

private:
    static constexpr int64_t kMaxBurst = 4;
    int64_t last_ = 0;
    bool have_ = false;
    bool running_ = false;
};

} // namespace fm1deck
