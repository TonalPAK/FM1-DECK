// SPDX-License-Identifier: GPL-3.0-only
#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "FM1_ENGINE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)
#define LOGW(...)
#define LOGE(...)
#endif

namespace fm1deck {

static_assert(AudioEngine::kSlots <= LinkSync::kLanes, "every rack slot needs its own Link lane");

AudioEngine::AudioEngine() {
    slotGain_.fill(1.0f);
    for (int i = 0; i < kSlots; ++i) {
        hosts_[static_cast<size_t>(i)] = std::make_unique<FirmwareHost>(&slots_[static_cast<size_t>(i)]);
        lanes_[static_cast<size_t>(i)] = LinkLane{&link_, i};
    }
}

void AudioEngine::setFocus(int i) {
    if (!validSlot(i)) return;
    std::lock_guard<std::mutex> lock(focusMutex_);
    const int old = focus_.load(std::memory_order_acquire);
    if (old == i) return;
    // Nothing stays pressed on the slot the panel leaves (a held key would drone on there)
    slots_[static_cast<size_t>(old)].pushPanel(0u, 0u);
    focus_.store(i, std::memory_order_release);
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start(int sampleRate, int bufferSize) {
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (isRunning_) {
        return true;
    }

    targetSampleRate_ = sampleRate > 0 ? sampleRate : 48000;
    targetBufferSize_ = bufferSize;

#if defined(__ANDROID__)
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setFormat(oboe::AudioFormat::Float);
    builder.setChannelCount(oboe::ChannelCount::Stereo);
    builder.setSampleRate(targetSampleRate_);
    builder.setUsage(oboe::Usage::Media);
    builder.setContentType(oboe::ContentType::Music);
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
    builder.setDataCallback(this);
    builder.setErrorCallback(this);

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
        LOGW("Failed to open exclusive stream (%s), trying shared mode...", oboe::convertToText(result));
        builder.setSharingMode(oboe::SharingMode::Shared);
        result = builder.openStream(stream_);
    }

    if (result != oboe::Result::OK || !stream_) {
        LOGE("Failed to open audio stream: %s", oboe::convertToText(result));
        return false;
    }

    stream_->setPerformanceHintEnabled(true);

    // The master bus allocates for this rate now, while no callback runs (MASTER-BUS.md, P6).
    bus_.prepare(static_cast<double>(stream_->getSampleRate()));

    const int32_t burst = stream_->getFramesPerBurst();
    const int32_t rate = stream_->getSampleRate();
    // 6 ms minimum buffer in Direct mode (AUDIO-ENGINE §5):
    // bufferSize = max(2 * burst, ceil(6 ms * rate / burst) * burst)
    const int32_t minFramesFor6Ms = static_cast<int32_t>(std::ceil((6.0 * static_cast<double>(rate)) / 1000.0));
    const int32_t burstsFor6Ms = (burst > 0) ? ((minFramesFor6Ms + burst - 1) / burst) : 2;
    const int32_t minDirectBuffer = std::max(2, burstsFor6Ms) * burst;

    int32_t bufFrames = (targetBufferSize_ > 0) ? targetBufferSize_ : minDirectBuffer;
    stream_->setBufferSizeInFrames(bufFrames);

    result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("Failed to start audio stream: %s", oboe::convertToText(result));
        stream_->close();
        stream_.reset();
        return false;
    }

    LOGI("Started Oboe stream: rate=%d, burst=%d, buffer=%d, sharing=%s, api=%s",
         stream_->getSampleRate(), burst, stream_->getBufferSizeInFrames(),
         stream_->getSharingMode() == oboe::SharingMode::Exclusive ? "Exclusive" : "Shared",
         stream_->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSLES");
#endif

    isRunning_.store(true, std::memory_order_release);
    reopenNeeded_.store(false, std::memory_order_release);
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (!isRunning_) return;

    isRunning_.store(false, std::memory_order_release);

#if defined(__ANDROID__)
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
        LOGI("Stopped and closed Oboe stream");
    }
#endif
}

bool AudioEngine::isRunning() const {
    return isRunning_.load(std::memory_order_acquire);
}

bool AudioEngine::reopenIfNeeded() {
    if (!reopenNeeded_.load(std::memory_order_acquire)) {
        return false;
    }
    LOGI("Reopening audio stream after error...");
    stop();
    return start(targetSampleRate_, targetBufferSize_);
}

void AudioEngine::updateOutputLatency() {
    double ms = -1.0;
    int rate = 48000;
#if defined(__ANDROID__)
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        if (stream_) {
            rate = stream_->getSampleRate();
            auto lat = stream_->calculateLatencyMillis();
            if (lat) ms = lat.value();
            else ms = 1000.0 * stream_->getBufferSizeInFrames() / rate;  // no timestamp yet: the buffer
        }
    }
#endif
    if (ms < 0.0) return;
    const double busMs = 1000.0 * bus_.latency() / rate;
    // The stream's estimate wanders by a few ms from reading to reading (a shared stream most): follow it slowly,
    // so the heard time the firmwares' clock comes from does not twitch; a real change (> 20 ms) is taken at once
    const int64_t want = static_cast<int64_t>((ms + busMs) * 1000.0);
    const int64_t cur = link_.outputLatencyUs();
    link_.setOutputLatencyUs(cur == 0 || std::llabs(want - cur) > 20000 ? want : cur + (want - cur) / 8);
}

StreamInfo AudioEngine::streamInfo() const {
    StreamInfo info;
    std::lock_guard<std::mutex> lock(streamMutex_);
#if defined(__ANDROID__)
    if (stream_) {
        info.sampleRate = stream_->getSampleRate();
        info.burst = stream_->getFramesPerBurst();
        info.bufferSize = stream_->getBufferSizeInFrames();
        info.capacity = stream_->getBufferCapacityInFrames();
        info.format = stream_->getFormat() == oboe::AudioFormat::Float ? 1 : 0;
        info.sharingMode = stream_->getSharingMode() == oboe::SharingMode::Exclusive ? 0 : 1;
        info.isExclusive = info.sharingMode == 0 ? 1 : 0;
        auto xruns = stream_->getXRunCount();
        if (xruns) info.xruns = xruns.value();
        info.api = stream_->getAudioApi() == oboe::AudioApi::AAudio ? 0 : 1;
    }
#endif
    return info;
}

#if defined(__ANDROID__)
oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) {
#if defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    asm volatile("msr fpcr, %0" : : "r"(fpcr | (1ULL << 24)));
#endif

    cpuMeter_.onCallbackStart();

    float* out = static_cast<float*>(audioData);
    const int sampleRate = stream->getSampleRate();

    // Every slot renders its sub-blocks into its own scratch; the sum goes through the master bus, then interleaved
    constexpr int kMaxScratch = 512;
    std::array<float, kMaxScratch> scratchL;
    std::array<float, kMaxScratch> scratchR;
    std::array<float, kMaxScratch> mixL;
    std::array<float, kMaxScratch> mixR;

    // Link: the clock bytes go to each firmware sub-block by sub-block, timed as heard (docs/LINK.md)
    link_.beginBuffer(sampleRate, numFrames);
    auto linkHook = [](void* ctx, IFirmware* fw, int offset, int n) {
        auto* lane = static_cast<LinkLane*>(ctx);
        lane->link->beforeSubBlock(lane->lane, fw, offset, n);
    };

    const uint32_t mute = muteMask_.load(std::memory_order_relaxed);
    const uint32_t solo = soloMask_.load(std::memory_order_relaxed);
    const float glide = 1.0f / (0.010f * static_cast<float>(sampleRate > 0 ? sampleRate : 48000));

    int framesRemaining = numFrames;
    while (framesRemaining > 0) {
        const int chunk = std::min(kMaxScratch, framesRemaining);
        std::fill_n(mixL.data(), chunk, 0.0f);
        std::fill_n(mixR.data(), chunk, 0.0f);
        for (int s = 0; s < kSlots; ++s) {
            auto& slot = slots_[static_cast<size_t>(s)];
            if (!slot.firmware()) continue;  // an empty slot costs nothing
            const auto ts = std::chrono::steady_clock::now();
            slot.process(chunk, scratchL.data(), scratchR.data(), sampleRate, linkHook, &lanes_[static_cast<size_t>(s)],
                         numFrames - framesRemaining);
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - ts).count();
            // The meter shows the slot's own sound, heard or not (a muted slot still shows it plays)
            const bool heard = !((mute >> s) & 1u) && (solo == 0u || ((solo >> s) & 1u));
            const float target = heard ? 1.0f : 0.0f;
            float g = slotGain_[static_cast<size_t>(s)];
            float peak = 0.0f;
            for (int i = 0; i < chunk; ++i) {
                if (g != target) g = g < target ? std::min(target, g + glide) : std::max(target, g - glide);
                const float l = scratchL[static_cast<size_t>(i)];
                const float r = scratchR[static_cast<size_t>(i)];
                mixL[static_cast<size_t>(i)] += g * l;
                mixR[static_cast<size_t>(i)] += g * r;
                peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            }
            slotGain_[static_cast<size_t>(s)] = g;
            auto& load = slotLoad_[static_cast<size_t>(s)];
            load += 0.008f * (static_cast<float>(ns * 1e-9 * sampleRate / chunk) - load);
            auto& level = slotLevel_[static_cast<size_t>(s)];
            level = std::max(peak, level * 0.94f);
            meters_[static_cast<size_t>(s)].load.store(load, std::memory_order_relaxed);
            meters_[static_cast<size_t>(s)].level.store(level, std::memory_order_relaxed);
        }
        for (int s = 0; s < kSlots; ++s) {
            if (slots_[static_cast<size_t>(s)].firmware()) continue;
            slotLoad_[static_cast<size_t>(s)] = 0.0f;
            slotLevel_[static_cast<size_t>(s)] = 0.0f;
            meters_[static_cast<size_t>(s)].load.store(0.0f, std::memory_order_relaxed);
            meters_[static_cast<size_t>(s)].level.store(0.0f, std::memory_order_relaxed);
        }

        const auto t0 = std::chrono::steady_clock::now();
        bus_.process(mixL.data(), mixR.data(), chunk);
        const auto busNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        const float ratio = static_cast<float>(busNs * 1e-9 * sampleRate / chunk);
        busLoad_ += 0.05f * (ratio - busLoad_);
        bus_.setLoad(busLoad_);

        for (int i = 0; i < chunk; ++i) {
            *out++ = mixL[static_cast<size_t>(i)];
            *out++ = mixR[static_cast<size_t>(i)];
        }
        framesRemaining -= chunk;
    }

    link_.endBuffer();
    cpuMeter_.onCallbackEnd(numFrames, sampleRate);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
    LOGW("Oboe stream error after close: %s", oboe::convertToText(error));
    reopenNeeded_.store(true, std::memory_order_release);
}
#endif

} // namespace fm1deck
