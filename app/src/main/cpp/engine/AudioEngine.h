#pragma once
// SPDX-License-Identifier: GPL-3.0-only

#include "../fw/FirmwareHost.h"
#include "../fw/FirmwareSlot.h"
#include "CpuMeter.h"
#include "../dsp/master/MasterBus.h"
#include "LinkSync.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <vector>

#if defined(__ANDROID__)
#include <oboe/Oboe.h>
#endif

namespace fm1deck {

struct StreamInfo {
    int32_t sampleRate = 48000;
    int32_t burst = 192;
    int32_t bufferSize = 384;
    int32_t capacity = 1920;
    int32_t format = 0;       // 1 = Float
    int32_t sharingMode = 0;  // 0 = Exclusive, 1 = Shared
    int32_t isExclusive = 0;
    int32_t xruns = 0;
    int32_t api = 0;          // 0 = AAudio, 1 = OpenSLES
};

class AudioEngine
#if defined(__ANDROID__)
    : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback
#endif
{
public:
    AudioEngine();
    ~AudioEngine()
#if defined(__ANDROID__)
        override
#endif
    ;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool start(int sampleRate = 48000, int bufferSize = 0);
    void stop();
    bool isRunning() const;

    bool reopenIfNeeded();

    StreamInfo streamInfo() const;
    CpuMeter::Metrics cpuMetrics() const { return cpuMeter_.computeMetrics(); }
    void resetCpuMetrics() noexcept { cpuMeter_.reset(); }

    FirmwareSlot* slot() noexcept { return &slot_; }
    dsp::MasterBus* masterBus() noexcept { return &bus_; }
    FirmwareHost* host() noexcept { return &host_; }
    LinkSync* link() noexcept { return &link_; }
    /** Control thread: refresh Link's output latency from the stream (Oboe's estimate + the master bus). */
    void updateOutputLatency();

#if defined(__ANDROID__)
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;
#endif

private:
    FirmwareSlot slot_;
    FirmwareHost host_;
    CpuMeter cpuMeter_;
    dsp::MasterBus bus_;
    LinkSync link_;
    float busLoad_ = 0.0f;  // audio thread only: smoothed share of the callback budget

    mutable std::mutex streamMutex_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> reopenNeeded_{false};
    int targetSampleRate_{48000};
    int targetBufferSize_{0};

#if defined(__ANDROID__)
    std::shared_ptr<oboe::AudioStream> stream_;
#endif
};

} // namespace fm1deck
