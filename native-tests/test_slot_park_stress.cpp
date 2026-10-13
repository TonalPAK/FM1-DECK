// SPDX-License-Identifier: GPL-3.0-only
#include "FakeFirmware.h"
#include "../app/src/main/cpp/fw/FirmwareSlot.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace fm1deck;

void testParkUnparkStress() {
    std::cout << "[test_slot_park_stress] Running 200 load/unload park/unpark cycles with active render thread..." << std::endl;

    FirmwareSlot slot;
    auto currentFake = std::make_unique<FakeFirmware>();

    slot.park();
    slot.setFirmware(currentFake.get());
    slot.unpark();

    std::atomic<bool> running{true};
    std::atomic<uint64_t> renderCount{0};

    // Render thread
    std::thread renderThread([&]() {
        std::vector<float> outL(64), outR(64);
        while (running.load(std::memory_order_relaxed)) {
            slot.process(64, outL.data(), outR.data(), 48000);
            renderCount.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Control thread doing 200 load/unload cycles
    constexpr int kCycles = 200;
    for (int i = 0; i < kCycles; ++i) {
        slot.park();
        // PROPERTY CHECK: after park returns, old firmware must NOT be in render
        CHECK(currentFake->inside() == 0 && "Firmware still being rendered after park returned!");
        slot.setFirmware(nullptr);

        // PROPERTY CHECK: destroy the old firmware instance.
        // If the audio thread touches it after park(), ASan will immediately trigger use-after-free!
        currentFake.reset();

        // Allocate a fresh fake per cycle
        currentFake = std::make_unique<FakeFirmware>();
        slot.setFirmware(currentFake.get());
        slot.unpark();

        // Brief pause between cycles
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    running.store(false, std::memory_order_relaxed);
    renderThread.join();

    slot.park();
    CHECK(currentFake->inside() == 0);
    slot.setFirmware(nullptr);
    currentFake.reset();

    std::cout << "  Passed: completed " << kCycles << " park/load/unpark cycles with fresh allocation & destruction. Render blocks rendered: "
              << renderCount.load() << std::endl;
}

void testFadeOutParkFadeIn() {
    std::cout << "[test_slot_park_stress] Testing fade-out -> park -> fade-in..." << std::endl;
    FirmwareSlot slot;
    auto fake = std::make_unique<FakeFirmware>();
    slot.park();
    slot.setFirmware(fake.get());
    slot.unpark();

    std::atomic<bool> running{true};
    std::thread renderThread([&]() {
        std::vector<float> outL(64), outR(64);
        while (running.load(std::memory_order_relaxed)) {
            slot.process(64, outL.data(), outR.data(), 48000);
            std::this_thread::yield();
        }
    });

    // 1. Initial fade in (10 ms)
    slot.startFadeIn(10);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // 2. Start fade-out (20 ms)
    slot.startFadeOut(20);

    // Wait for fade-out to finish
    int waitedMs = 0;
    while (!slot.isFadeOutDone() && waitedMs < 60) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        waitedMs += 2;
    }
    CHECK(slot.isFadeOutDone() && "Fade-out did not complete within timeout!");

    // 3. Second immediate fade-out when already at 0 gain must be immediately done
    slot.startFadeOut(20);
    CHECK(slot.isFadeOutDone() && "Second fade-out when gain is already 0 must be done immediately!");

    // 4. Park under zero gain
    slot.park();
    CHECK(fake->inside() == 0);

    // 5. Unpark and start fade-in
    slot.unpark();
    slot.startFadeIn(20);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    running.store(false, std::memory_order_relaxed);
    renderThread.join();

    slot.park();
    slot.setFirmware(nullptr);
    fake.reset();
    std::cout << "  Passed: fade-out -> park -> fade-in verified successfully." << std::endl;
}

int main() {
    testParkUnparkStress();
    testFadeOutParkFadeIn();
    std::cout << "[test_slot_park_stress] ALL TESTS PASSED!" << std::endl;
    return 0;
}
