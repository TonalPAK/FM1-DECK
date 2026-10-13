#pragma once
// SPDX-License-Identifier: GPL-3.0-only

#include "../app/src/main/cpp/fw/IFirmware.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>

namespace fm1deck {

class FakeFirmware : public IFirmware {
public:
    struct Event {
        enum Type { BUTTONS, KEYS, ENC, MASTER, MIDI, RENDER, BOOT };
        Type type;
        uint32_t val1;
        int32_t val2;
        uint32_t timeMs;
    };

    FakeFirmware(uint32_t sampleRate = 44100) : sampleRate_(sampleRate) {
        norBuffer_.resize(0x100000, 0xFF);
        std::fill(screenBuffer_.begin(), screenBuffer_.end(), 0);
    }

    int inside() const noexcept { return inside_.load(std::memory_order_acquire); }

    uint32_t abi() const noexcept override { return FM1DECK_FW_ABI; }
    uint32_t structSize() const noexcept override { return sizeof(Fm1FirmwareApi); }
    uint32_t caps() const noexcept override {
        return FM1_CAP_HOST_CLOCK | FM1_CAP_SAVE_NOW | FM1_CAP_UI_STATE | FM1_CAP_TEST_HEAVY;
    }
    const char* id() const noexcept override { return "fake"; }
    const char* name() const noexcept override { return "FAKE"; }
    const char* version() const noexcept override { return "1.0"; }
    const char* buildInfo() const noexcept override { return "fake-build"; }
    uint32_t sampleRate() const noexcept override { return sampleRate_; }
    uint32_t screenW() const noexcept override { return 240; }
    uint32_t screenH() const noexcept override { return 240; }
    uint32_t norSize() const noexcept override { return static_cast<uint32_t>(norBuffer_.size()); }

    uint8_t* nor() noexcept override { return norBuffer_.data(); }
    uint32_t norWrites() noexcept override { return norWrites_; }
    void norErase() noexcept override {
        std::fill(norBuffer_.begin(), norBuffer_.end(), 0xFF);
        norWrites_++;
    }

    void seed(uint32_t) noexcept override {}
    void boot() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(Event{Event::BOOT, 0, 0, nowMs_});
    }

    void render(uint32_t n, float* left, float* right) noexcept override {
        const int prev = inside_.fetch_add(1, std::memory_order_acq_rel);
        if (prev != 0) {
            std::cerr << "FakeFirmware::render concurrent entry detected! prev=" << prev << std::endl;
            std::abort();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            events_.push_back(Event{Event::RENDER, n, 0, nowMs_});
        }
        // Generate a subtle test sine wave or silence
        for (uint32_t i = 0; i < n; ++i) {
            float s = 0.5f * std::sin(2.0 * 3.141592653589793 * 1000.0 * (renderedFrames_ + i) / sampleRate_);
            left[i] = s;
            right[i] = s;
        }
        renderedFrames_ += n;
        nowMs_ = static_cast<uint32_t>((static_cast<uint64_t>(renderedFrames_) * 1000u) / sampleRate_);
        inside_.fetch_sub(1, std::memory_order_acq_rel);
    }

    void buttons(uint32_t mask) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(Event{Event::BUTTONS, mask, 0, nowMs_});
    }

    void keys(uint32_t mask) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(Event{Event::KEYS, mask, 0, nowMs_});
    }

    void enc(uint32_t role, int32_t steps) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(Event{Event::ENC, role, steps, nowMs_});
    }

    void master(uint32_t adc) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(Event{Event::MASTER, adc, 0, nowMs_});
    }

    uint32_t midi(uint32_t status, uint32_t d1, uint32_t d2) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(Event{Event::MIDI, status, static_cast<int32_t>((d1 << 8) | d2), nowMs_});
        return 1;
    }

    void hostClock(uint32_t) noexcept override {}
    int32_t saveNow() noexcept override {
        norWrites_++;
        return 1;
    }

    const uint16_t* screen() noexcept override { return screenBuffer_.data(); }
    uint32_t screenDraws() noexcept override { return screenDraws_; }
    void leds(uint32_t out[FM1_LED_COUNT]) noexcept override {
        std::fill_n(out, FM1_LED_COUNT, 0);
    }
    const uint8_t* animLevels() noexcept override { return nullptr; }
    uint32_t playing() noexcept override { return 1; }
    uint32_t nowMs() noexcept override { return nowMs_; }

    void uiState(Fm1UiState* out) noexcept override {
        std::memset(out, 0, sizeof(*out));
        out->layer = FM1_LAYER_FX;
        std::strncpy(out->page_title, "TEST", sizeof(out->page_title) - 1);
    }

    void testHeavy() noexcept override {}

    std::vector<Event> getEvents() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    void clearEvents() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

    void setNowMs(uint32_t ms) { nowMs_ = ms; }

private:
    uint32_t sampleRate_;
    uint32_t nowMs_{0};
    uint64_t renderedFrames_{0};
    uint32_t norWrites_{0};
    uint32_t screenDraws_{0};
    std::vector<uint8_t> norBuffer_;
    std::array<uint16_t, 240 * 240> screenBuffer_{};
    mutable std::mutex mutex_;
    std::atomic<int> inside_{0};
    std::vector<Event> events_;
};

} // namespace fm1deck
