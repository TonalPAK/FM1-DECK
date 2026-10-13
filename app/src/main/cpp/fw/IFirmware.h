#pragma once
// SPDX-License-Identifier: GPL-3.0-only

#include "Fm1FirmwareApi.h"

#include <cstddef>
#include <cstdint>

namespace fm1deck {

class IFirmware {
public:
    virtual ~IFirmware() = default;

    virtual uint32_t abi() const noexcept = 0;
    virtual uint32_t structSize() const noexcept = 0;
    virtual uint32_t caps() const noexcept = 0;
    virtual const char* id() const noexcept = 0;
    virtual const char* name() const noexcept = 0;
    virtual const char* version() const noexcept = 0;
    virtual const char* buildInfo() const noexcept = 0;
    virtual uint32_t sampleRate() const noexcept = 0;
    virtual uint32_t screenW() const noexcept = 0;
    virtual uint32_t screenH() const noexcept = 0;
    virtual uint32_t norSize() const noexcept = 0;

    virtual uint8_t* nor() noexcept = 0;
    virtual uint32_t norWrites() noexcept = 0;
    virtual void norErase() noexcept = 0;

    virtual void seed(uint32_t s) noexcept = 0;
    virtual void boot() noexcept = 0;
    virtual void render(uint32_t n, float* left, float* right) noexcept = 0;

    virtual void buttons(uint32_t mask) noexcept = 0;
    virtual void keys(uint32_t mask) noexcept = 0;
    virtual void enc(uint32_t role, int32_t steps) noexcept = 0;
    virtual void master(uint32_t adc) noexcept = 0;
    virtual uint32_t midi(uint32_t status, uint32_t d1, uint32_t d2) noexcept = 0;
    virtual void hostClock(uint32_t on) noexcept = 0;
    virtual int32_t saveNow() noexcept = 0;

    virtual const uint16_t* screen() noexcept = 0;
    virtual uint32_t screenDraws() noexcept = 0;
    virtual void leds(uint32_t out[FM1_LED_COUNT]) noexcept = 0;
    virtual const uint8_t* animLevels() noexcept = 0;
    virtual uint32_t playing() noexcept = 0;
    virtual uint32_t nowMs() noexcept = 0;

    virtual void uiState(Fm1UiState* out) noexcept = 0;
    virtual void testHeavy() noexcept = 0;
};

} // namespace fm1deck
