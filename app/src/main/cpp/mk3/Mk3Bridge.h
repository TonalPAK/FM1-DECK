#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Maschine MK3 Hardware Bridge for MWave / FM-1 DECK

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <array>
#include <string>

namespace fm1deck {
class AudioEngine;
}

namespace mk3 {

class Mk3Bridge {
public:
    explicit Mk3Bridge(fm1deck::AudioEngine* engine);
    ~Mk3Bridge();

    Mk3Bridge(const Mk3Bridge&) = delete;
    Mk3Bridge& operator=(const Mk3Bridge&) = delete;

    void start();
    void stop();
    bool isConnected() const noexcept { return connected_.load(std::memory_order_acquire); }
    int activeOctave() const noexcept { return octave_.load(std::memory_order_relaxed); }

private:
    void threadMain();
    int findAndOpenDevice(char* outPath, size_t maxLen);
    void disconnectDevice();

    // USB I/O
    int bulkSend(unsigned int ep, const uint8_t* data, size_t len, unsigned int timeoutMs);
    bool processInputPacket(const uint8_t* buf, int len);

    // Hardware outputs
    void pushButtonLeds();
    void pushPadsLeds();
    void flushScreen(int screenId, const uint8_t* pixels);

    // Screen Renderers
    void renderLeftScreen(uint8_t* pixels);
    void renderRightScreen(uint8_t* pixels);

    fm1deck::AudioEngine* engine_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread workerThread_;

    int usbFd_{-1};
    int currentBus_{-1};
    int currentDev_{-1};

    // State
    std::atomic<int> octave_{0}; // -2 .. +2
    uint32_t seenFrameLeft_{0};

    // Pads state
    struct PadState {
        uint16_t velocity{0};
        bool pressed{false};
        uint8_t color{0};
        int highlightTimer{0};
    };
    std::array<PadState, 16> pads_{};

    // Encoders state
    std::array<uint16_t, 8> encoderVals_{};
    std::array<bool, 8> encoderTouch_{};

    // 4D Joy
    bool joyUp_{false}, joyDown_{false}, joyLeft_{false}, joyRight_{false};

    // Buttons
    bool btnPlay_{false};
    bool btnRec_{false};
    bool btnStop_{false};
    bool btnShift_{false};
    std::array<bool, 8> btnGroup_{};
    std::array<bool, 8> btnDisplay_{}; // 8 buttons above screens

    // Framebuffers
    std::unique_ptr<uint8_t[]> screen0Pixels_;
    std::unique_ptr<uint8_t[]> screen1Pixels_;
    std::unique_ptr<uint8_t[]> prevScreen0Pixels_;
    std::unique_ptr<uint8_t[]> prevScreen1Pixels_;
    std::unique_ptr<uint16_t[]> fwScreenCopy_;
};

} // namespace mk3
