#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Measures callback durations without locks or allocations on the audio thread.
// Control side calculates p50, p99, and max callback load ratio against the audio budget
// using a whole-run histogram (AUDIO-ENGINE §5).

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace fm1deck {

static_assert(std::atomic<float>::is_always_lock_free, "atomic<float> must be lock-free");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomic<uint32_t> must be lock-free");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "atomic<uint64_t> must be lock-free");

class CpuMeter {
public:
    // 0% to 200% in 0.5% buckets (400 buckets) + 1 overflow bucket (>= 200%)
    static constexpr int kBuckets = 401;

    struct Metrics {
        uint64_t totalCallbacks = 0;
        float p50Ratio = 0.0f;
        float p99Ratio = 0.0f;
        float maxRatio = 0.0f;
        float p50Us = 0.0f;
        float p99Us = 0.0f;
        float maxUs = 0.0f;
        float budgetUs = 0.0f;
    };

    CpuMeter() {
        reset();
    }

    void reset() noexcept {
        for (auto& b : histogram_) b.store(0, std::memory_order_relaxed);
        totalCallbacks_.store(0, std::memory_order_relaxed);
        maxUs_.store(0.0f, std::memory_order_relaxed);
        maxRatio_.store(0.0f, std::memory_order_relaxed);
    }

    // Audio thread: record callback start
    void onCallbackStart() noexcept {
        startTime_ = std::chrono::steady_clock::now();
    }

    // Audio thread: record callback completion, load ratio and duration
    void onCallbackEnd(int numFrames, int sampleRate) noexcept {
        const auto endTime = std::chrono::steady_clock::now();
        const float durationUs = std::chrono::duration<float, std::micro>(endTime - startTime_).count();
        const float budgetUs = (sampleRate > 0) ? (1.0e6f * static_cast<float>(numFrames) / static_cast<float>(sampleRate)) : 4000.0f;
        const float ratio = (budgetUs > 0.0f) ? (durationUs / budgetUs) : 0.0f;

        int bucket = static_cast<int>(ratio / 0.005f);
        if (bucket < 0) bucket = 0;
        if (bucket >= kBuckets) bucket = kBuckets - 1;

        histogram_[static_cast<size_t>(bucket)].fetch_add(1, std::memory_order_relaxed);
        totalCallbacks_.fetch_add(1, std::memory_order_relaxed);

        updateMax(maxUs_, durationUs);
        updateMax(maxRatio_, ratio);

        lastFrames_.store(numFrames, std::memory_order_relaxed);
        lastSampleRate_.store(sampleRate, std::memory_order_relaxed);
    }

    // Control thread: compute stats over the whole run
    Metrics computeMetrics() const noexcept {
        Metrics m;
        const uint64_t total = totalCallbacks_.load(std::memory_order_relaxed);
        m.totalCallbacks = total;
        m.maxUs = maxUs_.load(std::memory_order_relaxed);
        m.maxRatio = maxRatio_.load(std::memory_order_relaxed);

        const int frames = lastFrames_.load(std::memory_order_relaxed);
        const int rate = lastSampleRate_.load(std::memory_order_relaxed);
        if (frames > 0 && rate > 0) {
            m.budgetUs = 1.0e6f * static_cast<float>(frames) / static_cast<float>(rate);
        } else {
            m.budgetUs = 4000.0f;
        }

        if (total == 0) return m;

        const uint64_t target50 = total / 2;
        const uint64_t target99 = static_cast<uint64_t>(total * 0.99);

        uint64_t cum = 0;
        bool found50 = false;
        bool found99 = false;

        for (int i = 0; i < kBuckets; ++i) {
            cum += histogram_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            if (!found50 && cum >= target50) {
                m.p50Ratio = (static_cast<float>(i) + 0.5f) * 0.005f;
                found50 = true;
            }
            if (!found99 && cum >= target99) {
                m.p99Ratio = (static_cast<float>(i) + 0.5f) * 0.005f;
                found99 = true;
            }
        }

        m.p50Us = m.p50Ratio * m.budgetUs;
        m.p99Us = m.p99Ratio * m.budgetUs;
        return m;
    }

private:
    static void updateMax(std::atomic<float>& atom, float val) noexcept {
        float cur = atom.load(std::memory_order_relaxed);
        while (val > cur && !atom.compare_exchange_weak(cur, val, std::memory_order_relaxed)) {}
    }

    std::chrono::steady_clock::time_point startTime_{};
    std::array<std::atomic<uint32_t>, kBuckets> histogram_{};
    std::atomic<uint64_t> totalCallbacks_{0};
    std::atomic<float> maxUs_{0.0f};
    std::atomic<float> maxRatio_{0.0f};

    std::atomic<int> lastFrames_{192};
    std::atomic<int> lastSampleRate_{48000};
};

} // namespace fm1deck
