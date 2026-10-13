#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Manages the active FM-1 firmware: load/unload/save/snapshot.
// Reached from control threads (JNI). Reaches the audio thread only through
// FirmwareSlot's lock-free park/unpark handshake and request atomics.

#include "FirmwareSlot.h"
#include "NativeFirmware.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fm1deck {

class FirmwareHost {
public:
    explicit FirmwareHost(FirmwareSlot* slot);
    ~FirmwareHost();

    FirmwareHost(const FirmwareHost&) = delete;
    FirmwareHost& operator=(const FirmwareHost&) = delete;

    // Loads a firmware module (soname "libfw_<id>.so" or absolute path).
    // Erases nor and restores nor data if provided.
    bool load(const std::string& id, const std::string& libPath,
              const std::vector<uint8_t>& nor, uint32_t seed,
              uint32_t masterAdc,
              std::string& errorReason);
    bool load(const std::string& id, const std::string& libPath,
              const std::vector<uint8_t>& nor, uint32_t seed,
              std::string& errorReason) {
        return load(id, libPath, nor, seed, 700u, errorReason);
    }

    void unload();
    bool loaded() const;

    std::string info() const;
    uint32_t caps() const;
    uint32_t norSize() const;

    int64_t saveNow();
    bool snapshot(std::vector<uint8_t>& out);
    bool testHeavy();

    std::string activeId() const;
    FirmwareSlot* slot() const noexcept { return slot_; }

private:
    template <typename Done>
    static bool waitUntil(Done done, int timeoutMs) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }

    bool audioDrives() const;

    FirmwareSlot* slot_;
    mutable std::mutex mutex_;
    std::string activeId_;
    std::unique_ptr<NativeFirmware> activeFw_;
};

} // namespace fm1deck
