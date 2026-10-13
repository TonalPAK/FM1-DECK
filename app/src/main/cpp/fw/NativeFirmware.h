#pragma once
// SPDX-License-Identifier: GPL-3.0-only

#include "IFirmware.h"

#include <memory>
#include <string>

namespace fm1deck {

class NativeFirmware final : public IFirmware {
public:
    static std::unique_ptr<NativeFirmware> open(const std::string& pathOrSoname, std::string& errorReason);
    ~NativeFirmware() override;

    NativeFirmware(const NativeFirmware&) = delete;
    NativeFirmware& operator=(const NativeFirmware&) = delete;

    uint32_t abi() const noexcept override { return api_->abi; }
    uint32_t structSize() const noexcept override { return api_->struct_size; }
    uint32_t caps() const noexcept override { return api_->caps; }
    const char* id() const noexcept override { return api_->id; }
    const char* name() const noexcept override { return api_->name; }
    const char* version() const noexcept override { return api_->version; }
    const char* buildInfo() const noexcept override { return api_->build_info; }
    uint32_t sampleRate() const noexcept override { return api_->sample_rate; }
    uint32_t screenW() const noexcept override { return api_->screen_w; }
    uint32_t screenH() const noexcept override { return api_->screen_h; }
    uint32_t norSize() const noexcept override { return api_->nor_size; }

    uint8_t* nor() noexcept override { return api_->nor(); }
    uint32_t norWrites() noexcept override { return api_->nor_writes(); }
    void norErase() noexcept override { api_->nor_erase(); }

    void seed(uint32_t s) noexcept override {
        if (api_->seed) api_->seed(s);
    }
    void boot() noexcept override { api_->boot(); }
    void render(uint32_t n, float* left, float* right) noexcept override {
        api_->render(n, left, right);
    }

    void buttons(uint32_t mask) noexcept override { api_->buttons(mask); }
    void keys(uint32_t mask) noexcept override { api_->keys(mask); }
    void enc(uint32_t role, int32_t steps) noexcept override { api_->enc(role, steps); }
    void master(uint32_t adc) noexcept override { api_->master(adc); }
    uint32_t midi(uint32_t status, uint32_t d1, uint32_t d2) noexcept override {
        return api_->midi(status, d1, d2);
    }
    void hostClock(uint32_t on) noexcept override {
        if (api_->host_clock) api_->host_clock(on);
    }
    int32_t saveNow() noexcept override {
        return api_->save_now ? api_->save_now() : -1;
    }

    const uint16_t* screen() noexcept override { return api_->screen(); }
    uint32_t screenDraws() noexcept override { return api_->screen_draws(); }
    void leds(uint32_t out[FM1_LED_COUNT]) noexcept override { api_->leds(out); }
    const uint8_t* animLevels() noexcept override {
        return api_->anim_levels ? api_->anim_levels() : nullptr;
    }
    uint32_t playing() noexcept override { return api_->playing(); }
    uint32_t nowMs() noexcept override { return api_->now_ms(); }

    void uiState(Fm1UiState* out) noexcept override {
        if (api_->ui_state) api_->ui_state(out);
    }
    void testHeavy() noexcept override {
        if (api_->test_heavy) api_->test_heavy();
    }

    const Fm1FirmwareApi* rawApi() const noexcept { return api_; }

private:
    NativeFirmware(void* handle, const Fm1FirmwareApi* api);

    void* handle_{nullptr};
    const Fm1FirmwareApi* api_{nullptr};
};

} // namespace fm1deck
