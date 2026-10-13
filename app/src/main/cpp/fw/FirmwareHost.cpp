// SPDX-License-Identifier: GPL-3.0-only
#include "FirmwareHost.h"

#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "FM1_HOST"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)
#define LOGW(...)
#endif

namespace fm1deck {

FirmwareHost::FirmwareHost(FirmwareSlot* slot) : slot_(slot) {}

FirmwareHost::~FirmwareHost() {
    unload();
}

bool FirmwareHost::audioDrives() const {
    if (!slot_) return false;
    const uint64_t start = slot_->heartbeat();
    return waitUntil([&] { return slot_->heartbeat() != start; }, 40);
}

bool FirmwareHost::load(const std::string& id, const std::string& libPath,
                        const std::vector<uint8_t>& nor, uint32_t seed,
                        uint32_t masterAdc,
                        std::string& errorReason) {
    std::lock_guard<std::mutex> lock(mutex_);
    errorReason.clear();

    if (slot_) {
        if (!slot_->parked() && audioDrives()) {
            slot_->startFadeOut(20);
            waitUntil([&] { return slot_->isFadeOutDone(); }, 35);
        }
        slot_->park();
        slot_->setFirmware(nullptr);
    }
    activeFw_.reset();
    activeId_.clear();

    const std::string path = libPath.empty() ? ("libfw_" + id + ".so") : libPath;
    auto fw = NativeFirmware::open(path, errorReason);
    if (!fw) {
        LOGW("Failed to load firmware %s: %s", id.c_str(), errorReason.c_str());
        return false;
    }

    fw->norErase();
    if (!nor.empty()) {
        if (nor.size() == fw->norSize()) {
            std::memcpy(fw->nor(), nor.data(), nor.size());
        } else {
            LOGW("%s: flash image size %zu != nor_size %u, booted erased",
                 fw->name(), nor.size(), fw->norSize());
        }
    }

    if (seed != 0 && (fw->caps() & FM1_CAP_SEED)) {
        fw->seed(seed);
    }

    fw->master(masterAdc);
    fw->boot();

    if (slot_) {
        slot_->publishNorWrites(fw->norWrites());
        slot_->setFirmware(fw.get());
        slot_->unpark();
        slot_->startFadeIn(20);
    }

    activeId_ = id;
    activeFw_ = std::move(fw);
    LOGI("Loaded firmware %s (%s %s)", activeId_.c_str(), activeFw_->name(), activeFw_->version());
    return true;
}

void FirmwareHost::unload() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_) {
        slot_->park();
        slot_->setFirmware(nullptr);
    }
    activeFw_.reset();
    activeId_.clear();
}

bool FirmwareHost::loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeFw_ != nullptr && slot_ != nullptr && slot_->firmware() != nullptr;
}

std::string FirmwareHost::info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!activeFw_) return "";
    return std::string(activeFw_->name()) + " " + activeFw_->version() +
           " (" + activeFw_->buildInfo() + ")";
}

uint32_t FirmwareHost::caps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeFw_ ? activeFw_->caps() : 0u;
}

uint32_t FirmwareHost::norSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeFw_ ? activeFw_->norSize() : 0u;
}

std::string FirmwareHost::activeId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeId_;
}

bool FirmwareHost::testHeavy() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!activeFw_ || !slot_ || !(activeFw_->caps() & FM1_CAP_TEST_HEAVY)) {
        return false;
    }
    const bool wasParked = slot_->parked();
    if (!wasParked) slot_->park();
    activeFw_->testHeavy();
    if (!wasParked) slot_->unpark();
    return true;
}

int64_t FirmwareHost::saveNow() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!activeFw_ || !slot_) return -1;

    if (!slot_->parked() && audioDrives()) {
        const uint32_t req = slot_->requestSave();
        if (waitUntil([&] { return slot_->saveDone() == req; }, 500)) {
            return static_cast<int64_t>(slot_->norWrites());
        }
        LOGW("%s: audio thread did not complete save within 500ms", activeFw_->name());
    }

    // Direct save when parked or audio idle
    const bool wasParked = slot_->parked();
    if (!wasParked) slot_->park();
    activeFw_->saveNow();
    const uint32_t writes = activeFw_->norWrites();
    slot_->publishNorWrites(writes);
    if (!wasParked) slot_->unpark();
    return static_cast<int64_t>(writes);
}

bool FirmwareHost::snapshot(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!activeFw_ || !slot_) return false;

    const uint32_t size = activeFw_->norSize();
    out.assign(size, 0xFF);

    if (!slot_->parked() && audioDrives()) {
        const uint32_t save = slot_->requestSave();
        if (waitUntil([&] { return slot_->saveDone() == save; }, 500)) {
            const uint32_t snap = slot_->requestSnapshot(out.data());
            if (waitUntil([&] { return slot_->snapshotDone() == snap; }, 1500)) {
                return true;
            }
        }
        LOGW("%s: audio thread snapshot timed out", activeFw_->name());
    }

    // Audio stopped or timed out: take direct snapshot while parked
    const bool wasParked = slot_->parked();
    if (!wasParked) slot_->park();
    slot_->cancelSnapshot();
    activeFw_->saveNow();
    std::memcpy(out.data(), activeFw_->nor(), size);
    slot_->publishNorWrites(activeFw_->norWrites());
    if (!wasParked) slot_->unpark();
    return true;
}

} // namespace fm1deck
