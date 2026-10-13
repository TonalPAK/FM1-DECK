// SPDX-License-Identifier: GPL-3.0-only
#include "FakeFirmware.h"
#include "../app/src/main/cpp/fw/FirmwareSlot.h"

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

void testTapInsideOneBlock() {
    std::cout << "[test_slot_gating] Running testTapInsideOneBlock..." << std::endl;
    FirmwareSlot slot;
    FakeFirmware fake;
    slot.park();
    slot.setFirmware(&fake);
    slot.unpark();

    // Push down and up before rendering
    slot.pushButtons(1u); // DOWN
    slot.pushButtons(0u); // UP

    std::vector<float> outL(1024), outR(1024);

    // Render in blocks until both events arrive
    bool gotDown = false;
    bool gotUp = false;
    uint32_t downTime = 0;
    uint32_t upTime = 0;

    for (int block = 0; block < 100; ++block) {
        slot.process(64, outL.data(), outR.data(), 48000);
        auto evs = fake.getEvents();
        for (const auto& e : evs) {
            if (e.type == FakeFirmware::Event::BUTTONS) {
                if (e.val1 == 1u && !gotDown) {
                    gotDown = true;
                    downTime = e.timeMs;
                } else if (e.val1 == 0u && gotDown && !gotUp) {
                    gotUp = true;
                    upTime = e.timeMs;
                }
            }
        }
        if (gotDown && gotUp) break;
    }

    CHECK(gotDown && "Did not receive button DOWN");
    CHECK(gotUp && "Did not receive button UP");
    CHECK(upTime >= downTime + 24 && "Button UP arrived sooner than 24 ms after DOWN");
    std::cout << "  Passed: down at " << downTime << " ms, up at " << upTime
              << " ms (delta: " << (upTime - downTime) << " ms >= 24 ms)" << std::endl;
}

void testRapidTaps() {
    std::cout << "[test_slot_gating] Running testRapidTaps..." << std::endl;
    FirmwareSlot slot;
    FakeFirmware fake;
    slot.park();
    slot.setFirmware(&fake);
    slot.unpark();

    constexpr int kTapCount = 10;
    // Push 10 taps on key 5
    for (int i = 0; i < kTapCount; ++i) {
        slot.pushKeys(1u << 5); // DOWN
        slot.pushKeys(0u);      // UP
    }

    std::vector<float> outL(1024), outR(1024);
    std::vector<FakeFirmware::Event> keyEvents;

    // Process enough blocks for 20 events * 24 ms = 480 ms of device time
    // At 48 kHz, 480 ms ≈ 23040 frames ≈ 360 sub-blocks of 64 frames
    for (int block = 0; block < 500; ++block) {
        slot.process(64, outL.data(), outR.data(), 48000);
        auto evs = fake.getEvents();
        keyEvents.clear();
        for (const auto& e : evs) {
            if (e.type == FakeFirmware::Event::KEYS) {
                keyEvents.push_back(e);
            }
        }
        if (keyEvents.size() == kTapCount * 2) break;
    }

    CHECK(keyEvents.size() == kTapCount * 2 && "Not all rapid tap events delivered");

    // Verify ordering and spacing
    for (size_t i = 0; i < keyEvents.size(); ++i) {
        const uint32_t expectedMask = (i % 2 == 0) ? (1u << 5) : 0u;
        CHECK(keyEvents[i].val1 == expectedMask && "Event order or mask corrupted");
        if (i > 0) {
            CHECK(keyEvents[i].timeMs >= keyEvents[i - 1].timeMs + 24 &&
                  "Gating violation: events spaced < 24 ms");
        }
    }

    std::cout << "  Passed: all " << keyEvents.size() << " events delivered in exact order, >= 24ms apart" << std::endl;
}

void testEncodersImmediate() {
    std::cout << "[test_slot_gating] Running testEncodersImmediate..." << std::endl;
    FirmwareSlot slot;
    FakeFirmware fake;
    slot.park();
    slot.setFirmware(&fake);
    slot.unpark();

    // Push a button and an encoder
    slot.pushButtons(1u);
    slot.pushEnc(FM1_E_K2, 5);

    std::vector<float> outL(64), outR(64);
    slot.process(64, outL.data(), outR.data(), 48000);

    auto evs = fake.getEvents();
    bool foundEnc = false;
    for (const auto& e : evs) {
        if (e.type == FakeFirmware::Event::ENC && e.val1 == FM1_E_K2 && e.val2 == 5) {
            foundEnc = true;
            CHECK(e.timeMs == 0 && "Encoder was not delivered immediately at block 0");
        }
    }
    CHECK(foundEnc && "Encoder event missing");
    std::cout << "  Passed: encoder applied immediately at block 0" << std::endl;
}

// MAJOR-1 Case 1: Key change with no button change reaches firmware in the same sub-block
// when previous change was more than 24 ms ago.
void testKeyOnlyImmediateWhenOld() {
    std::cout << "[test_slot_gating] Running testKeyOnlyImmediateWhenOld..." << std::endl;
    FirmwareSlot slot;
    FakeFirmware fake;
    slot.park();
    slot.setFirmware(&fake);
    slot.unpark();

    std::vector<float> outL(64), outR(64);
    // Render 50 blocks (~66 ms) of silence
    for (int i = 0; i < 50; ++i) {
        slot.process(64, outL.data(), outR.data(), 48000);
    }
    fake.clearEvents();

    // Key change with no button change
    slot.pushPanel(0u, 1u << 3);

    // Process exactly 1 sub-block
    slot.process(64, outL.data(), outR.data(), 48000);

    auto evs = fake.getEvents();
    int keyCount = 0;
    int btnCount = 0;
    for (const auto& e : evs) {
        if (e.type == FakeFirmware::Event::KEYS) {
            CHECK(e.val1 == (1u << 3));
            keyCount++;
        } else if (e.type == FakeFirmware::Event::BUTTONS) {
            btnCount++;
        }
    }
    CHECK(keyCount == 1 && "Key event not delivered in the same sub-block");
    CHECK(btnCount == 0 && "Unchanged buttons mask was erroneously enqueued");
    std::cout << "  Passed: key change arrived immediately in sub-block with 0 button events" << std::endl;
}

// MAJOR-1 Case 2: Three key presses 5 ms apart produce exactly three key events (and 0 button events)
void testThreeKeyTouchesProduceThreeEvents() {
    std::cout << "[test_slot_gating] Running testThreeKeyTouchesProduceThreeEvents..." << std::endl;
    FirmwareSlot slot;
    FakeFirmware fake;
    slot.park();
    slot.setFirmware(&fake);
    slot.unpark();

    std::vector<float> outL(64), outR(64);
    // Render 50 blocks (~66 ms) to advance past initial 24 ms
    for (int i = 0; i < 50; ++i) {
        slot.process(64, outL.data(), outR.data(), 48000);
    }
    fake.clearEvents();

    // Finger 1 (at t = 0)
    slot.pushPanel(0u, 1u << 0);

    // Advance 5 ms (~240 frames = 4 blocks of 64)
    for (int i = 0; i < 4; ++i) {
        slot.process(64, outL.data(), outR.data(), 48000);
    }

    // Finger 2 (at t = 5 ms)
    slot.pushPanel(0u, (1u << 0) | (1u << 1));

    // Advance 5 ms
    for (int i = 0; i < 4; ++i) {
        slot.process(64, outL.data(), outR.data(), 48000);
    }

    // Finger 3 (at t = 10 ms)
    slot.pushPanel(0u, (1u << 0) | (1u << 1) | (1u << 2));

    // Drain HOLD_MS queue: 3 events * 24 ms = 72 ms
    for (int i = 0; i < 60; ++i) {
        slot.process(64, outL.data(), outR.data(), 48000);
    }

    auto evs = fake.getEvents();
    std::vector<FakeFirmware::Event> keyEvents;
    int btnCount = 0;
    for (const auto& e : evs) {
        if (e.type == FakeFirmware::Event::KEYS) {
            keyEvents.push_back(e);
        } else if (e.type == FakeFirmware::Event::BUTTONS) {
            btnCount++;
        }
    }

    CHECK(btnCount == 0 && "Unchanged button mask produced spurious button events");
    CHECK(keyEvents.size() == 3 && "Did not produce exactly 3 key events");
    CHECK(keyEvents[0].val1 == (1u << 0));
    CHECK(keyEvents[1].val1 == ((1u << 0) | (1u << 1)));
    CHECK(keyEvents[2].val1 == ((1u << 0) | (1u << 1) | (1u << 2)));
    CHECK(keyEvents[1].timeMs >= keyEvents[0].timeMs + 24);
    CHECK(keyEvents[2].timeMs >= keyEvents[1].timeMs + 24);

    std::cout << "  Passed: 3 key presses produced exactly 3 key events, 0 button events, spaced >= 24ms" << std::endl;
}

int main() {
    testTapInsideOneBlock();
    testRapidTaps();
    testEncodersImmediate();
    testKeyOnlyImmediateWhenOld();
    testThreeKeyTouchesProduceThreeEvents();
    std::cout << "[test_slot_gating] ALL TESTS PASSED!" << std::endl;
    return 0;
}
