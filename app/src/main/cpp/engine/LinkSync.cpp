// SPDX-License-Identifier: GPL-3.0-only
#include "LinkSync.h"

#include "../fw/Fm1FirmwareApi.h"

#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace fm1deck {

using std::chrono::microseconds;

struct LinkSync::Impl {
    std::unique_ptr<ableton::Link> link;

    // audio thread
    std::optional<ableton::Link::SessionState> state;
    struct Lane {
        ClockPulser pulser;
        IFirmware* lastFw = nullptr;
        bool clockOn = false;     // the firmware was told to follow the host clock
        double join = 0.0;        // the beat this lane's transport counts from (a late joiner: the next bar)
    };
    Lane lanes[kLanes];
    bool active = false;      // this buffer carries a captured state
    bool wasPlaying = false;
    bool dirty = false;       // the captured state changed: commit at the end of the buffer
    int64_t t0 = 0;           // Link time (µs) at which the buffer's first frame is heard
    // The buffers' host time from the frames played (a regression over the last 512 callbacks): a callback that
    // comes late or early (big, uneven bursts on a shared stream) does not bend the clock the firmwares hear
    ableton::link::HostTimeFilter<ableton::Link::Clock> filter;
    double sampleTime = 0.0;
    int filterRate = 0;
    int64_t lastRawUs = 0;
    double usPerFrame = 1e6 / 48000.0;
    double quantum = 4.0;
};

LinkSync::LinkSync() : impl_(std::make_unique<Impl>()) {}
LinkSync::~LinkSync() = default;

void LinkSync::setEnabled(bool on) {
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (on && !created_.load(std::memory_order_acquire)) {
        impl_->link = std::make_unique<ableton::Link>(120.0);
        impl_->link->enableStartStopSync(true);
        created_.store(true, std::memory_order_release);
    }
    if (created_.load(std::memory_order_acquire)) impl_->link->enable(on);
    if (!on) { playHeld_ = false; playSwallowed_ = false; }
    enabled_.store(on, std::memory_order_release);
}

void LinkSync::setLocal(bool on) {
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (on && !created_.load(std::memory_order_acquire)) {
        // The timeline without the network: Link opens its sockets only on enable()
        impl_->link = std::make_unique<ableton::Link>(120.0);
        impl_->link->enableStartStopSync(true);
        created_.store(true, std::memory_order_release);
    }
    if (!on && !enabled_.load(std::memory_order_acquire)) { playHeld_ = false; playSwallowed_ = false; }
    local_.store(on, std::memory_order_release);
}

void LinkSync::setTempo(double bpm) {
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (!created_.load(std::memory_order_acquire)) return;
    auto& link = *impl_->link;
    auto state = link.captureAppSessionState();
    state.setTempo(std::clamp(bpm, 20.0, 999.0), link.clock().micros());
    link.commitAppSessionState(state);
}

void LinkSync::setQuantum(int beats) {
    quantum_.store(std::clamp(beats, 1, 16), std::memory_order_relaxed);
}

void LinkSync::togglePlay() {
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (!created_.load(std::memory_order_acquire) || !clockOn()) return;
    auto& link = *impl_->link;
    auto state = link.captureAppSessionState();
    // The start's beat 0 is mapped on the audio thread (requestBeatAtStartPlayingTime), on every peer alike
    state.setIsPlaying(!state.isPlaying(), link.clock().micros());
    link.commitAppSessionState(state);
}

uint32_t LinkSync::filterButtons(uint32_t mask) {
    const uint32_t playBit = 1u << FM1_B_PLAY;
    bool toggle = false;
    uint32_t out = mask;
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        const bool play = (mask & playBit) != 0;
        if (!clockOn()) {
            playHeld_ = play;
            playSwallowed_ = false;
            return mask;
        }
        if (play && !playHeld_ && (mask & ~playBit) == 0) {
            playSwallowed_ = true;
            toggle = true;
        }
        if (!play) playSwallowed_ = false;
        playHeld_ = play;
        if (playSwallowed_) out &= ~playBit;
    }
    if (toggle) togglePlay();
    return out;
}

LinkSync::Status LinkSync::status() const {
    Status s;
    s.enabled = enabled_.load(std::memory_order_acquire);
    s.quantum = quantum_.load(std::memory_order_relaxed);
    if (!created_.load(std::memory_order_acquire)) return s;
    auto& link = *impl_->link;
    const auto state = link.captureAppSessionState();
    const auto heard = link.clock().micros() + microseconds(latencyUs_.load(std::memory_order_relaxed));
    s.peers = static_cast<int>(link.numPeers());
    s.tempo = state.tempo();
    s.playing = state.isPlaying();
    s.beat = state.beatAtTime(heard, static_cast<double>(s.quantum));
    s.local = local_.load(std::memory_order_acquire);
    s.running = (s.enabled || s.local) && s.playing;
    return s;
}

// ---- audio thread ------------------------------------------------------------------------------------------------

void LinkSync::beginBuffer(int sampleRate, int numFrames) noexcept {
    auto& im = *impl_;
    im.active = false;
    im.dirty = false;
    if (!clockOn() || !created_.load(std::memory_order_acquire)) return;
    auto& link = *im.link;
    im.state.emplace(link.captureAudioSessionState());
    im.quantum = static_cast<double>(quantum_.load(std::memory_order_relaxed));
    im.usPerFrame = 1e6 / static_cast<double>(sampleRate > 0 ? sampleRate : 48000);
    const int64_t raw = link.clock().micros().count();
    if (sampleRate != im.filterRate || raw - im.lastRawUs > 200000) {
        // A new rate, or the stream (or the clock) was away: start the regression over
        im.filter.reset();
        im.filterRate = sampleRate;
    }
    im.lastRawUs = raw;
    const int64_t host = im.filter.sampleTimeToHostTime(im.sampleTime).count();
    im.sampleTime += numFrames;
    im.t0 = host + latencyUs_.load(std::memory_order_relaxed);
    const bool playing = im.state->isPlaying();
    if (playing && !im.wasPlaying) {
        // Every peer maps beat 0 to its start: with peers, the next quantum boundary in phase with the session
        im.state->requestBeatAtStartPlayingTime(0.0, im.quantum);
        im.dirty = true;
    }
    im.wasPlaying = playing;
    im.active = true;
}

void LinkSync::beforeSubBlock(int laneIndex, IFirmware* fw, int offset, int n) noexcept {
    auto& im = *impl_;
    if (!fw || laneIndex < 0 || laneIndex >= kLanes) return;
    auto& ln = im.lanes[laneIndex];
    const bool on = im.active;
    bool joined = false;
    if (fw != ln.lastFw || on != ln.clockOn) {
        // A new firmware instance, or Link switched: tell the firmware whose clock it follows. "Off" only undoes
        // our own "on": a CLK the user set on the device for a real MIDI clock stays as it is.
        if (on) fw->hostClock(1u);
        else if (ln.clockOn && fw == ln.lastFw) fw->hostClock(0u);
        ln.lastFw = fw;
        ln.clockOn = on;
        ln.pulser.reset();
        joined = true;
        if (!on) im.wasPlaying = false;
    }
    if (!on) return;
    const auto& st = *im.state;
    const double a = static_cast<double>(im.t0) + offset * im.usPerFrame;
    const double b = static_cast<double>(im.t0) + (offset + n) * im.usPerFrame;
    const double b0 = st.beatAtTime(microseconds(static_cast<int64_t>(a)), im.quantum);
    const double b1 = st.beatAtTime(microseconds(static_cast<int64_t>(b)), im.quantum);
    // A firmware that comes in while the others play starts with them on the next bar (quantum), in phase
    if (!st.isPlaying()) ln.join = 0.0;
    else if (joined && b0 > 0.0) ln.join = std::ceil(b0 / im.quantum) * im.quantum;
    ln.pulser.step(b0 - ln.join, b1 - ln.join, st.isPlaying(), [fw](uint32_t status) { fw->midi(status, 0, 0); });
}

void LinkSync::endBuffer() noexcept {
    auto& im = *impl_;
    if (im.active && im.dirty) im.link->commitAudioSessionState(*im.state);
    im.active = false;
}

} // namespace fm1deck
