// SPDX-License-Identifier: GPL-3.0-only
#include "NativeFirmware.h"

#include <dlfcn.h>
#include <cstddef>
#include <sstream>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "FM1_FW"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOGW(...)
#endif

namespace fm1deck {

NativeFirmware::NativeFirmware(void* handle, const Fm1FirmwareApi* api)
    : handle_(handle), api_(api) {}

NativeFirmware::~NativeFirmware() {
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

std::unique_ptr<NativeFirmware> NativeFirmware::open(const std::string& pathOrSoname, std::string& errorReason) {
    errorReason.clear();
    void* handle = dlopen(pathOrSoname.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        errorReason = "dlopen failed for " + pathOrSoname + ": " + (err ? err : "unknown error");
        LOGW("%s", errorReason.c_str());
        return nullptr;
    }

    auto entry = reinterpret_cast<Fm1FirmwareEntry>(dlsym(handle, FM1DECK_FW_ENTRY));
    if (!entry) {
        errorReason = "dlsym failed for " + std::string(FM1DECK_FW_ENTRY) + " in " + pathOrSoname;
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    const Fm1FirmwareApi* api = entry();
    if (!api) {
        errorReason = "Fm1FirmwareEntry returned null in " + pathOrSoname;
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    if (api->abi != FM1DECK_FW_ABI) {
        std::ostringstream ss;
        ss << "ABI mismatch in " << pathOrSoname << ": module has " << api->abi
           << ", host requires " << FM1DECK_FW_ABI;
        errorReason = ss.str();
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    // Check struct_size covers used fields (up to test_heavy)
    const size_t minSize = offsetof(Fm1FirmwareApi, test_heavy) + sizeof(api->test_heavy);
    if (api->struct_size < minSize) {
        std::ostringstream ss;
        ss << "struct_size too small in " << pathOrSoname << ": " << api->struct_size
           << " < " << minSize;
        errorReason = ss.str();
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    // Validate mandatory function pointers
    if (!api->nor || !api->nor_writes || !api->nor_erase || !api->boot ||
        !api->render || !api->buttons || !api->keys || !api->enc ||
        !api->master || !api->midi || !api->screen || !api->screen_draws ||
        !api->leds || !api->playing || !api->now_ms) {
        errorReason = "Module missing mandatory functions in " + pathOrSoname;
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    // Check capability bits vs non-null function pointers
    if ((api->caps & FM1_CAP_HOST_CLOCK) && !api->host_clock) {
        errorReason = "Module advertises FM1_CAP_HOST_CLOCK but host_clock is null";
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }
    if ((api->caps & FM1_CAP_SAVE_NOW) && !api->save_now) {
        errorReason = "Module advertises FM1_CAP_SAVE_NOW but save_now is null";
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }
    if ((api->caps & FM1_CAP_UI_STATE) && !api->ui_state) {
        errorReason = "Module advertises FM1_CAP_UI_STATE but ui_state is null";
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }
    if ((api->caps & FM1_CAP_SEED) && !api->seed) {
        errorReason = "Module advertises FM1_CAP_SEED but seed is null";
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }
    if ((api->caps & FM1_CAP_TEST_HEAVY) && !api->test_heavy) {
        errorReason = "Module advertises FM1_CAP_TEST_HEAVY but test_heavy is null";
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    if (api->sample_rate != 44100) {
        std::ostringstream ss;
        ss << "Invalid sample_rate in " << pathOrSoname << ": " << api->sample_rate << " != 44100";
        errorReason = ss.str();
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    if (api->nor_size == 0) {
        errorReason = "Module nor_size is zero in " + pathOrSoname;
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    if (api->screen_w != 240 || api->screen_h != 240) {
        errorReason = "Module screen is not 240x240 in " + pathOrSoname;
        LOGW("%s", errorReason.c_str());
        dlclose(handle);
        return nullptr;
    }

    return std::unique_ptr<NativeFirmware>(new NativeFirmware(handle, api));
}

} // namespace fm1deck
