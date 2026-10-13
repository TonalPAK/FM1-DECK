// SPDX-License-Identifier: GPL-3.0-only
// Master bus host tests (docs/MASTER-BUS.md §8) and its building blocks.
#include "../app/src/main/cpp/dsp/master/MasterBus.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace fm1deck::dsp;
using P = MasterParamId;

static constexpr double kRate = 48000.0;

static std::vector<float> sine(int n, double f, double amp, double rate = kRate, double phase = 0) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2 * kPi * f * i / rate + phase));
    return v;
}

/** Power of the component at exactly `f` (Goertzel over the whole vector, from index `from`). */
static double tonePower(const std::vector<float>& x, double f, int from, double rate = kRate) {
    const int n = static_cast<int>(x.size()) - from;
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * f * i / rate;
        re += x[static_cast<size_t>(from + i)] * std::cos(a);
        im -= x[static_cast<size_t>(from + i)] * std::sin(a);
    }
    return 2.0 * (re * re + im * im) / (static_cast<double>(n) * n);  // = amplitude^2 / 2 for a sine
}

static double meanSquare(const std::vector<float>& x, int from) {
    double s = 0;
    for (size_t i = static_cast<size_t>(from); i < x.size(); ++i) s += static_cast<double>(x[i]) * x[i];
    return s / static_cast<double>(x.size() - static_cast<size_t>(from));
}

/** Runs a bus over stereo copies of `in` in callback-sized chunks; returns L. */
static std::vector<float> run(MasterBus& bus, const std::vector<float>& in, int chunk = 192) {
    std::vector<float> l = in, r = in;
    for (size_t i = 0; i < in.size(); i += static_cast<size_t>(chunk)) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(chunk), in.size() - i));
        bus.process(l.data() + i, r.data() + i, n);
    }
    return l;
}

static void allOff(MasterBus& bus) {
    bus.setParam(static_cast<int>(P::SAT_ENABLE), 0);
    bus.setParam(static_cast<int>(P::SPC_ENABLE), 0);
    bus.setParam(static_cast<int>(P::LIM_ENABLE), 0);
    bus.setParam(static_cast<int>(P::CLIP_ENABLE), 0);
}

// ---------------------------------------------------------------------------------------------------------------

static void testOversamplerRoundTrip() {
    std::cout << "[master] oversampler round trip, latency, passband, alias rejection" << std::endl;
    for (int q : {2, 4, 8}) {
        Oversampler os;
        os.prepare(64);
        os.setFactor(q);
        const int lat = os.latency();
        const auto x = sine(9600, 1000.0, 0.5);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); i += 64) {
            float* u = os.up(x.data() + i, 64);
            os.down(u, y.data() + i, 64);
        }
        double err = 0;
        for (size_t i = 2000; i < x.size(); ++i) err = std::max(err, std::fabs(static_cast<double>(y[i]) - x[i - static_cast<size_t>(lat)]));
        std::printf("  Q%d latency %d samples, max error vs delayed input %.2e\n", q, lat, err);
        CHECK(err < 1e-3);  // -66 dB at 0.5 amplitude: an integer delay, linear phase
        // Passband: 15 kHz within 0.1 dB
        const auto hf = sine(9600, 15000.0, 0.5);
        for (size_t i = 0; i < hf.size(); i += 64) {
            float* u = os.up(hf.data() + i, 64);
            os.down(u, y.data() + i, 64);
        }
        const double g = 10 * std::log10(tonePower(y, 15000.0, 4800) / tonePower(hf, 15000.0, 4800));
        std::printf("  Q%d gain at 15 kHz %.3f dB\n", q, g);
        CHECK(std::fabs(g) < 0.1);
    }
    // Down-sampling rejects a top-rate tone above the base Nyquist (30 kHz at 96 kHz -> image at 18 kHz)
    Oversampler os;
    os.prepare(64);
    os.setFactor(2);
    const auto top = sine(19200, 30000.0, 0.5, 96000.0);
    std::vector<float> topCopy = top, y(9600);
    for (int b = 0; b < 150; ++b) os.down(topCopy.data() + b * 128, y.data() + b * 64, 64);
    const double rej = 10 * std::log10(meanSquare(y, 2000) / 0.125);
    std::printf("  x2 rejection of a 30 kHz top-rate tone: %.1f dB\n", rej);
    CHECK(rej < -70.0);
}

static void testSlidingMinAndBoxcar() {
    std::cout << "[master] sliding minimum vs brute force" << std::endl;
    SlidingMin sm;
    sm.prepare(100);
    sm.setWindow(37);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> d(0, 1);
    std::vector<float> v;
    for (int i = 0; i < 5000; ++i) {
        v.push_back(d(rng));
        const float m = sm.push(v.back());
        float ref = 1e9f;
        for (int j = std::max(0, i - 36); j <= i; ++j) ref = std::min(ref, v[static_cast<size_t>(j)]);
        CHECK(m == ref);
    }
}

static void testTruePeakDetector() {
    std::cout << "[master] true-peak detector finds inter-sample peaks" << std::endl;
    TruePeakDetector tp;
    // fs/4 sine at 45 degrees: samples at +-0.707, true peak 1.0
    const auto x = sine(4000, 12000.0, 1.0, kRate, kPi / 4);
    float peak = 0, samplePeak = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        peak = std::max(peak, tp.process(x[i]));
        samplePeak = std::max(samplePeak, std::fabs(x[i]));
    }
    std::printf("  sample peak %.3f, detected true peak %.3f\n", samplePeak, peak);
    CHECK(samplePeak < 0.71f);
    CHECK(peak > 0.95f && peak < 1.05f);
}

static void testLufs() {
    std::cout << "[master] LUFS: 1 kHz at -20 dBFS in both channels = -20.0 LUFS" << std::endl;
    LufsMeter m;
    m.prepare(kRate);
    const auto x = sine(static_cast<int>(kRate * 4), 1000.0, std::pow(10.0, -20.0 / 20.0));
    for (float s : x) m.process(s, s);
    std::printf("  measured %.2f LUFS\n", m.shortTerm());
    CHECK(std::fabs(m.shortTerm() + 20.0f) < 0.1f);
}

static void testCrossoverFlat() {
    std::cout << "[master] crossover bands sum flat (all-pass) for 4/6/8 bands" << std::endl;
    for (int bi = 0; bi < 3; ++bi) {
        CrossoverTree xo;
        xo.configure(bi, kRate);
        std::array<std::array<float, 64>, CrossoverTree::kMaxBands> bands{};
        for (double f : {50.0, 300.0, 1000.0, 4000.0, 12000.0}) {
            xo.reset();
            const auto x = sine(24000, f, 0.5);
            std::vector<float> y(x.size());
            for (size_t i = 0; i < x.size(); i += 64) {
                xo.split(0, x.data() + i, 64, bands);
                for (int k = 0; k < 64; ++k) {
                    float s = 0;
                    for (int b = 0; b < xo.bands(); ++b) s += bands[static_cast<size_t>(b)][static_cast<size_t>(k)];
                    y[i + static_cast<size_t>(k)] = s;
                }
            }
            const double g = 10 * std::log10(meanSquare(y, 12000) / meanSquare(x, 12000));
            CHECK(std::fabs(g) < 0.05);
        }
    }
}

static void testNullWhenAllOff() {
    std::cout << "[master] null test: all stages off = DC-blocked input delayed by the reported latency" << std::endl;
    MasterBus bus;
    allOff(bus);
    bus.prepare(kRate);
    const int lat = bus.latency();
    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0, 0.2f);
    std::vector<float> x(24000);
    for (auto& s : x) s = nd(rng);
    const auto y = run(bus, x);
    FirstOrder dc;
    dc.setDcBlocker(5.0, kRate);
    std::vector<float> ref(x.size());
    for (size_t i = 0; i < x.size(); ++i) ref[i] = dc.process(x[i]);
    double err = 0;
    for (size_t i = static_cast<size_t>(lat) + 10; i < x.size(); ++i) err = std::max(err, std::fabs(static_cast<double>(y[i]) - ref[i - static_cast<size_t>(lat)]));
    std::printf("  latency %d samples, max deviation %.2e\n", lat, err);
    CHECK(err < 1e-5);
}

static void testLatencyIsMeasured() {
    std::cout << "[master] reported latency matches an impulse through the default bus at Q1/Q4/Q8" << std::endl;
    for (int q : {1, 4, 8}) {
        MasterBus bus;
        bus.setQuality(q);
        bus.setParam(static_cast<int>(P::SAT_ENABLE), 0);
        bus.setParam(static_cast<int>(P::SPC_ENABLE), 0);
        bus.prepare(kRate);
        std::vector<float> x(8000, 0.0f);
        x[4000] = 0.25f;
        const auto y = run(bus, x, 64);
        size_t at = 0;
        float best = 0;
        for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > best) { best = std::fabs(y[i]); at = i; }
        std::printf("  Q%d reported %d, impulse peak at +%d\n", q, bus.latency(), static_cast<int>(at) - 4000);
        CHECK(static_cast<int>(at) - 4000 == bus.latency());
    }
}

static float ceilingCheck(int q, bool truePeak, const std::vector<float>& x, float inputGainDb) {
    MasterBus bus;
    bus.setQuality(q);
    bus.setParam(static_cast<int>(P::INPUT_GAIN), inputGainDb);
    bus.setParam(static_cast<int>(P::LIM_CEILING), -1.0f);
    bus.setParam(static_cast<int>(P::LIM_TRUE_PEAK), truePeak ? 1.0f : 0.0f);
    bus.prepare(kRate);
    const auto y = run(bus, x);
    float peak = 0;
    for (float s : y) { CHECK(std::isfinite(s)); peak = std::max(peak, std::fabs(s)); }
    return peak;
}

static void testCeilingNeverExceeded() {
    std::cout << "[master] ceiling -1 dBFS holds on noise, sines, square, impulses, sweep (+12 dB in)" << std::endl;
    const float ceil = std::pow(10.0f, -1.0f / 20.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> ud(-1, 1);
    std::vector<std::vector<float>> signals;
    std::vector<float> noise(48000);
    for (auto& s : noise) s = ud(rng);
    signals.push_back(noise);
    signals.push_back(sine(48000, 997.0, 1.0));
    signals.push_back(sine(48000, 60.0, 1.0));
    std::vector<float> square(48000);
    for (size_t i = 0; i < square.size(); ++i) square[i] = ((i / 40) % 2) ? 1.0f : -1.0f;
    signals.push_back(square);
    std::vector<float> imp(48000, 0.0f);
    for (size_t i = 100; i < imp.size(); i += 1777) imp[i] = (i % 2) ? 1.0f : -1.0f;
    signals.push_back(imp);
    std::vector<float> sweep(96000);
    double ph = 0;
    for (size_t i = 0; i < sweep.size(); ++i) {
        const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / sweep.size());
        ph += 2 * kPi * f / kRate;
        sweep[i] = static_cast<float>(std::sin(ph));
    }
    signals.push_back(sweep);
    for (int q : {1, 4}) {
        for (bool tp : {false, true}) {
            for (const auto& s : signals) {
                const float peak = ceilingCheck(q, tp, s, 12.0f);
                CHECK(peak <= ceil * 1.000001f);
            }
        }
    }
    // True-peak mode: the output's inter-sample peaks stay within 0.1 dB of the ceiling (8x reference interpolation)
    MasterBus bus;
    bus.setParam(static_cast<int>(P::INPUT_GAIN), 12.0f);
    bus.setParam(static_cast<int>(P::LIM_CEILING), -1.0f);
    bus.setParam(static_cast<int>(P::CLIP_ENABLE), 0.0f);
    bus.prepare(kRate);
    const auto y = run(bus, sine(48000, 11025.0, 1.0, kRate, 0.3));
    Oversampler ref;
    ref.prepare(64);
    ref.setFactor(8);
    float tpPeak = 0;
    for (size_t i = 0; i + 64 <= y.size(); i += 64) {  // from the start: no switch-on ringing in the reference
        const float* u = ref.up(y.data() + i, 64);
        if (i >= 4800) for (int j = 0; j < 512; ++j) tpPeak = std::max(tpPeak, std::fabs(u[j]));
    }
    std::printf("  TP mode: output true peak %.2f dBTP (ceiling -1)\n", linToDb(tpPeak));
    CHECK(linToDb(tpPeak) <= -1.0f + 0.1f);
}

static void testSilenceDcAndNan() {
    std::cout << "[master] silence, DC and NaN input give finite output" << std::endl;
    for (int q : {1, 8}) {
        MasterBus bus;
        bus.setQuality(q);
        bus.prepare(kRate);
        std::vector<float> x(48000, 0.0f);
        for (float s : run(bus, x)) CHECK(std::isfinite(s) && std::fabs(s) < 1e-6f);
        std::vector<float> dc(48000, 0.9f);
        for (float s : run(bus, dc)) CHECK(std::isfinite(s));
        std::vector<float> bad(4800, 0.1f);
        bad[100] = NAN; bad[200] = INFINITY; bad[300] = -INFINITY;
        for (float s : run(bus, bad)) CHECK(std::isfinite(s));
    }
}

static void testNoClicksOnParameterJumps() {
    std::cout << "[master] every continuous parameter jumping min<->max makes no click" << std::endl;
    int count = 0;
    const auto* table = masterParams(&count);
    for (int k = 0; k < count; ++k) {
        const auto& p = table[k];
        if (p.kind != 'C') continue;
        MasterBus bus;
        bus.prepare(kRate);
        const auto x = sine(48000 * 2, 220.0, 0.25);
        std::vector<float> l = x, r = x;
        bus.setParam(p.id, p.min);
        auto block = [&](size_t from, size_t to) {
            for (size_t i = from; i < to; i += 64) bus.process(l.data() + i, r.data() + i, 64);
        };
        auto maxStep = [&](size_t from, size_t to) {
            float m = 0;
            for (size_t i = from + 1; i < to; ++i) m = std::max(m, std::fabs(l[i] - l[i - 1]));
            return m;
        };
        block(0, 48000);
        bus.setParam(p.id, p.max);
        block(48000, 96000);
        const float before = maxStep(40000, 48000);
        const float during = maxStep(48000, 52800);
        const float after = maxStep(88000, 96000);
        const float bound = 1.5f * std::max(before, after) + 1e-3f;
        if (during > bound) std::printf("  %s: step %.4f > bound %.4f\n", p.key, during, bound);
        CHECK(during <= bound);
    }
}

/**
 * Aliases in the audible band, relative to the fundamental: DFT power below 20 kHz except the 9 kHz bin.
 * (A hard-clipped sine is odd-symmetric: no legitimate 18 kHz.) Folds of 24..28 kHz content land at 20..24 kHz:
 * that is the half-band filters' transition band, accepted by design (MASTER-BUS.md §1.7).
 */
static double aliasLevel(int q) {
    MasterBus bus;
    bus.setQuality(q);
    allOff(bus);
    bus.setParam(static_cast<int>(P::SAT_ENABLE), 1);
    bus.setParam(static_cast<int>(P::SAT_DRIVE), 24.0f);
    bus.setParam(static_cast<int>(P::SAT_DYNAMICS), 0.0f);
    bus.setParam(static_cast<int>(P::SAT_SHAPE), 0.0f);  // hard clip: the worst case
    bus.prepare(kRate);
    const auto x = sine(48000 * 2, 9000.0, 0.5);
    const auto y = run(bus, x);
    const int N = 4800, off = 60000;  // 10 Hz bins
    double fund = 0, alias = 0;
    for (int k = 1; k < 2000; ++k) {  // up to 20 kHz
        double re = 0, im = 0;
        for (int i = 0; i < N; ++i) {
            const double a = 2 * kPi * k * i / N;
            re += y[static_cast<size_t>(off + i)] * std::cos(a);
            im -= y[static_cast<size_t>(off + i)] * std::sin(a);
        }
        const double pw = re * re + im * im;
        if (k == 900) fund = pw; else alias += pw;
    }
    return 10 * std::log10(std::max(alias, 1e-30) / fund);
}

static void testAliasingFallsWithQuality() {
    std::cout << "[master] audible aliasing (< 20 kHz) of a hard-clipped 9 kHz sine (drive 24 dB) falls with Q" << std::endl;
    double prev = 1e9;
    for (int q : {1, 2, 4, 8}) {
        const double a = aliasLevel(q);
        std::printf("  Q%d aliases %.1f dB re fundamental\n", q, a);
        CHECK(a < prev);
        prev = a;
    }
}

static void testSaturatorTransparentAtLowLevel() {
    std::cout << "[master] saturation (residual ADAA) stays flat at low level: 10 kHz at -40 dBFS, drive 0" << std::endl;
    MasterBus bus;
    allOff(bus);
    bus.setParam(static_cast<int>(P::SAT_ENABLE), 1);
    bus.setParam(static_cast<int>(P::SAT_DRIVE), 0.0f);
    bus.setParam(static_cast<int>(P::SAT_DYNAMICS), 0.0f);
    bus.setParam(static_cast<int>(P::SAT_SHAPE), 0.0f);
    bus.prepare(kRate);
    const auto x = sine(48000, 10000.0, 0.01);
    const auto y = run(bus, x);
    const double g = 10 * std::log10(tonePower(y, 10000.0, 24000) / tonePower(x, 10000.0, 24000));
    std::printf("  gain at 10 kHz %.3f dB\n", g);
    CHECK(std::fabs(g) < 0.1);
}

static void testBypassAndReprime() {
    std::cout << "[master] BYPASS and a quality change fade without NaN or jumps" << std::endl;
    MasterBus bus;
    bus.prepare(kRate);
    const auto x = sine(48000, 330.0, 0.3);
    std::vector<float> l = x, r = x;
    for (size_t i = 0; i < x.size(); i += 64) {
        if (i == 9600) bus.setParam(static_cast<int>(P::BYPASS), 1);
        if (i == 19200) bus.setParam(static_cast<int>(P::BYPASS), 0);
        if (i == 28800) bus.setQuality(4);
        if (i == 38400) bus.setParam(static_cast<int>(P::LIM_LOOKAHEAD), 4.0f);
        bus.process(l.data() + i, r.data() + i, 64);
    }
    float maxStep = 0;
    for (size_t i = 1; i < l.size(); ++i) { CHECK(std::isfinite(l[i])); maxStep = std::max(maxStep, std::fabs(l[i] - l[i - 1])); }
    const float sineStep = static_cast<float>(0.3 * 2 * kPi * 330.0 / kRate);
    std::printf("  max step %.4f (the sine's own %.4f)\n", maxStep, sineStep);
    CHECK(maxStep < 3.0f * sineStep);
}

/** The scope history: written only when enabled, one entry per block, newest last, reader catches up. */
static void testScopeHistory() {
    MasterBus bus;
    bus.prepare(kRate);
    auto x = sine(48000, 1000.0, 2.0);  // +6 dBFS: the limiter works
    int count = 0;
    std::vector<float> out(MasterBus::kScopeLen * MasterBus::kScopeFields);
    run(bus, std::vector<float>(4800, 0.0f));
    uint64_t since = bus.readScope(UINT64_MAX, out.data(), MasterBus::kScopeLen, count);
    CHECK(count == 0 && since == 0);  // disabled: nothing written
    bus.setScopeEnabled(true);
    run(bus, std::vector<float>(x.begin(), x.begin() + 6400));  // 100 blocks of 64
    since = bus.readScope(since, out.data(), MasterBus::kScopeLen, count);
    CHECK(count == 100 && since == 100);
    const float* last = out.data() + (count - 1) * MasterBus::kScopeFields;
    std::cout << "[master] scope history: in " << last[0] << " dB, out " << last[1] << " dB, GR " << last[2] << " dB" << std::endl;
    CHECK(last[0] > 5.0f && last[0] < 6.5f);          // input peak, dB
    CHECK(last[1] <= 0.0f && last[1] < last[0] - 3.0f); // output under the ceiling, well below the input
    CHECK(last[2] < 0.0f);                            // gain reduction shown
    since = bus.readScope(since, out.data(), MasterBus::kScopeLen, count);
    CHECK(count == 0);
    // a slow reader gets the newest entries only
    run(bus, x);
    since = bus.readScope(since, out.data(), 64, count);
    CHECK(count == 64 && since == 100 + 750);
}

int main() {
    testOversamplerRoundTrip();
    testSlidingMinAndBoxcar();
    testTruePeakDetector();
    testLufs();
    testCrossoverFlat();
    testNullWhenAllOff();
    testLatencyIsMeasured();
    testCeilingNeverExceeded();
    testSilenceDcAndNan();
    testNoClicksOnParameterJumps();
    testAliasingFallsWithQuality();
    testSaturatorTransparentAtLowLevel();
    testBypassAndReprime();
    testScopeHistory();
    std::cout << "[test_master_bus] ALL PASSED" << std::endl;
    return 0;
}
