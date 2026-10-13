// SPDX-License-Identifier: GPL-3.0-only
#include "../app/src/main/cpp/dsp/Resampler.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace fm1deck;

void testDcGain() {
    std::cout << "[test_resampler] Testing DC gain..." << std::endl;
    Resampler r;
    r.reset(44100.0, 48000.0);

    constexpr int kFrames = 2000;
    std::vector<float> inL(kFrames, 1.0f);
    std::vector<float> inR(kFrames, 1.0f);
    std::vector<float> outL(kFrames, 0.0f);
    std::vector<float> outR(kFrames, 0.0f);

    int inPos = 0;
    int outPos = 0;
    constexpr int kChunk = 64;

    while (outPos + kChunk <= kFrames && inPos + 200 <= kFrames) {
        int need = r.inputNeeded(kChunk);
        for (int i = 0; i < need; ++i) {
            r.inputTailL()[i] = inL[inPos + i];
            r.inputTailR()[i] = inR[inPos + i];
        }
        r.commitInput(need);
        inPos += need;

        r.process(kChunk, outL.data() + outPos, outR.data() + outPos);
        outPos += kChunk;
    }

    // Check steady-state DC gain (last 500 samples)
    float maxErr = 0.0f;
    for (int i = outPos - 500; i < outPos; ++i) {
        float errL = std::fabs(outL[i] - 1.0f);
        float errR = std::fabs(outR[i] - 1.0f);
        if (errL > maxErr) maxErr = errL;
        if (errR > maxErr) maxErr = errR;
    }

    std::cout << "  DC gain max error: " << maxErr << std::endl;
    CHECK(maxErr < 1e-4 && "Resampler DC gain error >= 1e-4");
    std::cout << "  Passed: DC gain == 1.0 within 1e-4" << std::endl;
}

void testSine1kHz() {
    std::cout << "[test_resampler] Testing 1 kHz sine 44.1k -> 48k amplitude..." << std::endl;
    Resampler r;
    r.reset(44100.0, 48000.0);

    constexpr double kPi = 3.14159265358979323846;
    constexpr int kFrames = 4800; // 100 ms at 48k
    std::vector<float> outL(kFrames, 0.0f);
    std::vector<float> outR(kFrames, 0.0f);

    int inTotal = 0;
    int outPos = 0;
    constexpr int kChunk = 64;

    while (outPos + kChunk <= kFrames) {
        int need = r.inputNeeded(kChunk);
        for (int i = 0; i < need; ++i) {
            double t = (inTotal + i) / 44100.0;
            float val = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * t));
            r.inputTailL()[i] = val;
            r.inputTailR()[i] = val;
        }
        r.commitInput(need);
        inTotal += need;

        r.process(kChunk, outL.data() + outPos, outR.data() + outPos);
        outPos += kChunk;
    }

    // Find peak amplitude in steady state (last 2000 samples)
    float peak = 0.0f;
    for (int i = outPos - 2000; i < outPos; ++i) {
        float v = std::fabs(outL[i]);
        if (v > peak) peak = v;
    }

    double errDb = std::fabs(20.0 * std::log10(peak / 1.0));
    std::cout << "  Measured peak: " << peak << ", amplitude error: " << errDb << " dB" << std::endl;
    CHECK(errDb < 0.01 && "1 kHz sine amplitude error >= 0.01 dB");
    std::cout << "  Passed: amplitude error < 0.01 dB" << std::endl;
}

void testImpulseLatency() {
    std::cout << "[test_resampler] Measuring impulse latency..." << std::endl;
    Resampler r;
    r.reset(44100.0, 48000.0);

    constexpr int kFrames = 512;
    std::vector<float> in(kFrames, 0.0f);
    in[0] = 1.0f; // impulse at t=0

    std::vector<float> out(kFrames, 0.0f);
    std::vector<float> dummy(kFrames, 0.0f);

    int inTotal = 0;
    int outPos = 0;
    constexpr int kChunk = 32;

    while (outPos + kChunk <= kFrames && inTotal + 64 <= kFrames) {
        int need = r.inputNeeded(kChunk);
        for (int i = 0; i < need; ++i) {
            r.inputTailL()[i] = in[inTotal + i];
            r.inputTailR()[i] = 0.0f;
        }
        r.commitInput(need);
        inTotal += need;

        r.process(kChunk, out.data() + outPos, dummy.data() + outPos);
        outPos += kChunk;
    }

    int peakIdx = 0;
    float peakVal = 0.0f;
    for (int i = 0; i < outPos; ++i) {
        if (out[i] > peakVal) {
            peakVal = out[i];
            peakIdx = i;
        }
    }

    double latencyMs = (peakIdx * 1000.0) / 48000.0;
    std::cout << "  Impulse peak at output frame: " << peakIdx
              << " (" << latencyMs << " ms at 48 kHz)" << std::endl;
}

int main() {
    testDcGain();
    testSine1kHz();
    testImpulseLatency();
    std::cout << "[test_resampler] ALL TESTS PASSED!" << std::endl;
    return 0;
}
