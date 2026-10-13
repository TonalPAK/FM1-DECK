#pragma once
// SPDX-License-Identifier: GPL-3.0-only
//
// Ableton Link (tempo, beat phase and start/stop shared over the local network) driving the firmware's MIDI clock
// (docs/LINK.md). Link is created on the first enable (no sockets before the user switches it on) and lives until
// the process ends, so the audio thread never sees it disappear.
//
// Threads: control (JNI) for enable / tempo / quantum / play / status; audio for beginBuffer / beforeSubBlock /
// endBuffer. Link's audio-side calls are real-time safe (no allocation, no locks).

#include "ClockPulser.h"
#include "../fw/IFirmware.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace ableton { class Link; }

namespace fm1deck {

class LinkSync {
public:
    LinkSync();
    ~LinkSync();
    LinkSync(const LinkSync&) = delete;
    LinkSync& operator=(const LinkSync&) = delete;

    struct Status {
        bool enabled = false;
        int peers = 0;
        double tempo = 120.0;
        bool playing = false;
        double beat = 0.0;     // at the time heard now
        int quantum = 4;
        bool running = false;  // the firmware's transport was started by the clock
    };

    // ---- control thread --------------------------------------------------------------------------------------
    void setEnabled(bool on);
    bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    void setTempo(double bpm);
    void setQuantum(int beats);
    /** Start (on the next quantum boundary, together with the peers) or stop the shared transport. */
    void togglePlay();
    /**
     * The panel's buttons on their way to the firmware: while Link is on, a lone PLAY press is the shared transport's
     * (togglePlay) and does not reach the firmware; PLAY with other buttons held (GLO+PLAY, REC+PLAY…) passes.
     */
    uint32_t filterButtons(uint32_t mask);
    Status status() const;
    /** Output latency to add to "now" for the first frame of a buffer (stream + master bus), microseconds. */
    void setOutputLatencyUs(int64_t us) noexcept { latencyUs_.store(us, std::memory_order_relaxed); }
    int64_t outputLatencyUs() const noexcept { return latencyUs_.load(std::memory_order_relaxed); }

    // ---- audio thread ----------------------------------------------------------------------------------------
    /** Once per callback, before the firmware renders: the buffer's frames start at "now + output latency". */
    void beginBuffer(int sampleRate) noexcept;
    /** Before each firmware sub-block of n frames at offset (frames into the buffer): clock bytes for it. */
    void beforeSubBlock(IFirmware* fw, int offset, int n) noexcept;
    void endBuffer() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;          // the Link instance and its audio-side state
    std::atomic<bool> enabled_{false};
    std::atomic<bool> created_{false};    // impl_->link exists (published to the audio thread)
    std::atomic<int> quantum_{4};
    std::atomic<int64_t> latencyUs_{0};
    std::mutex controlMutex_;
    bool playHeld_ = false, playSwallowed_ = false;  // control thread, under controlMutex_
};

} // namespace fm1deck
