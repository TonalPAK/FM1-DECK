// SPDX-License-Identifier: GPL-3.0-only
// ClockPulser (docs/LINK.md): Link's beat timeline -> the MIDI clock the firmware follows.
#include "../app/src/main/cpp/engine/ClockPulser.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using fm1deck::ClockPulser;

struct Ev { uint32_t status; double beat; };  // the byte and the beat at the middle of its sub-block

/** Runs sub-blocks of 64 frames at 48 kHz over [fromBeat, fromBeat + beats) at bpm; playing(beat) per block. */
template <class Playing>
static std::vector<Ev> run(ClockPulser& p, double fromBeat, double beats, double bpm, Playing playing) {
    std::vector<Ev> out;
    const double perBlock = 64.0 / 48000.0 * bpm / 60.0;
    for (double b = fromBeat; b < fromBeat + beats; b += perBlock) {
        const double mid = b + 0.5 * perBlock;
        p.step(b, b + perBlock, playing(b), [&](uint32_t s) { out.push_back({s, mid}); });
    }
    return out;
}

static int count(const std::vector<Ev>& v, uint32_t s) {
    int n = 0;
    for (const auto& e : v) n += e.status == s;
    return n;
}

static void testSteadyPulses() {
    ClockPulser p;
    const auto ev = run(p, 10.0, 8.0, 120.0, [](double) { return false; });
    CHECK(count(ev, ClockPulser::kClock) >= 191 && count(ev, ClockPulser::kClock) <= 192);  // 24 per beat
    CHECK(count(ev, ClockPulser::kStart) == 0 && count(ev, ClockPulser::kStop) == 0);
    // each pulse goes out at the start of the sub-block whose middle passed it: within half a sub-block of its time
    const double half = 0.5 * 64.0 / 48000.0 * 2.0;  // beats at 120 BPM
    for (const auto& e : ev) {
        if (e.status != ClockPulser::kClock) continue;
        const double sent = e.beat - half;                      // the sub-block's start
        const double due = std::floor(e.beat * 24.0) / 24.0;    // the newest pulse at its middle
        CHECK(std::fabs(sent - due) <= half + 1e-9);
    }
    std::cout << "[clock] 24 pulses per beat, each within half a sub-block (0.67 ms) of its time" << std::endl;
}

static void testStartOnBeatZero() {
    ClockPulser p;
    // the transport plays from the start; beat 0 is the mapped start: one beat of count-in before it
    const auto ev = run(p, -1.0, 2.0, 120.0, [](double) { return true; });
    CHECK(count(ev, ClockPulser::kStart) == 1);
    size_t fa = 0;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].status == ClockPulser::kStart) fa = i;
    CHECK(fa + 1 < ev.size() && ev[fa + 1].status == ClockPulser::kClock);   // FA, then the downbeat's clock
    CHECK(std::fabs(ev[fa].beat) <= 0.5 * 64.0 / 48000.0 * 2.0 + 1e-9);    // at beat 0
    int after = 0;
    for (size_t i = fa + 1; i < ev.size(); ++i) after += ev[i].status == ClockPulser::kClock;
    CHECK(after >= 24 && after <= 25);  // beat 0..1: pulses 0..23 (+ the boundary)
    CHECK(p.running());
    std::cout << "[clock] Start (FA) lands on beat 0, followed by the downbeat's clock" << std::endl;
}

static void testStop() {
    ClockPulser p;
    run(p, 0.0, 1.0, 120.0, [](double) { return true; });
    const auto ev = run(p, 1.0, 1.0, 120.0, [](double b) { return b < 1.5; });
    CHECK(count(ev, ClockPulser::kStop) == 1);
    CHECK(!p.running());
    CHECK(count(ev, ClockPulser::kClock) >= 23);  // the clock keeps running while stopped (tempo)
    std::cout << "[clock] Stop (FC) once; the clock keeps running for the tempo" << std::endl;
}

static void testJumpsMakeNoBurst() {
    ClockPulser p;
    std::vector<uint32_t> out;
    auto emit = [&](uint32_t s) { out.push_back(s); };
    p.step(5.0, 5.01, false, emit);
    out.clear();
    p.step(9.0, 9.01, false, emit);   // jumped 4 beats forward (96 pulses): one pulse, not a burst
    CHECK(out.size() == 1);
    out.clear();
    p.step(2.0, 2.01, false, emit);   // jumped backwards
    CHECK(out.size() == 1);
    out.clear();
    p.step(2.01, 2.02, false, emit);
    CHECK(out.size() <= 1);
    std::cout << "[clock] timeline jumps give one pulse, no burst" << std::endl;
}

static void testSmallStepBackIsWaitedOut() {
    // The heard time moves back a few ms now and then (the latency estimate): no pulse is sent twice
    ClockPulser p;
    int pulses = 0;
    auto emit = [&](uint32_t s) { if (s == ClockPulser::kClock) ++pulses; };
    const double perBlock = 64.0 / 48000.0 * 2.0;  // 120 BPM, 64-frame blocks
    double b = 0.0;
    for (int i = 0; i < 3000; ++i) {
        const double jitter = (i % 50 == 25) ? -0.02 : 0.0;  // 10 ms back every 50 blocks
        p.step(b + jitter, b + jitter + perBlock, false, emit);
        b += perBlock;
    }
    const int expected = static_cast<int>(b * ClockPulser::kPpqn);
    CHECK(pulses >= expected - 2 && pulses <= expected + 1);
    std::cout << "[clock] a small step back sends no pulse twice (" << pulses << " of " << expected << ")" << std::endl;
}

int main() {
    testSteadyPulses();
    testStartOnBeatZero();
    testStop();
    testJumpsMakeNoBurst();
    testSmallStepBackIsWaitedOut();
    std::cout << "[test_clock_pulser] ALL PASSED" << std::endl;
    return 0;
}
