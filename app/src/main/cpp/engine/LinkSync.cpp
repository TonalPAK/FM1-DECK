// SPDX-License-Identifier: GPL-3.0-only
#include "LinkSync.h"

#include "../fw/Fm1FirmwareApi.h"

#include <ableton/Link.hpp>

#include <algorithm>
#include <chrono>
#include <optional>

namespace fm1deck {

using std::chrono::microseconds;

struct LinkSync::Impl {
    std::unique_ptr<ableton::Link> link;

    // audio thread
    std::optional<ableton::Link::SessionState> state;
    ClockPulser pulser;
    IFirmware* lastFw = nullptr;
    bool clockOn = false;     // the firmware was told to follow the host clock
    bool active = false;      // this buffer carries a captured state
    bool wasPlaying = false;
    bool dirty = false;       // the captured state changed: commit at the end of the buffer
    int64_t t0 = 0;           // Link time (µs) at which the buffer's first frame is heard
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
    if (!created_.load(std::memory_order_acquire) || !enabled_.load(std::memory_order_acquire)) return;
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
        if (!enabled_.load(std::memory_order_acquire)) {
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
    s.running = s.enabled && s.playing;
    return s;
}

// ---- audio thread ------------------------------------------------------------------------------------------------

void LinkSync::beginBuffer(int sampleRate) noexcept {
    auto& im = *impl_;
    im.active = false;
    im.dirty = false;
    if (!enabled_.load(std::memory_order_acquire) || !created_.load(std::memory_order_acquire)) return;
    auto& link = *im.link;
    im.state.emplace(link.captureAudioSessionState());
    im.quantum = static_cast<double>(quantum_.load(std::memory_order_relaxed));
    im.usPerFrame = 1e6 / static_cast<double>(sampleRate > 0 ? sampleRate : 48000);
    im.t0 = link.clock().micros().count() + latencyUs_.load(std::memory_order_relaxed);
    const bool playing = im.state->isPlaying();
    if (playing && !im.wasPlaying) {
        // Every peer maps beat 0 to its start: with peers, the next quantum boundary in phase with the session
        im.state->requestBeatAtStartPlayingTime(0.0, im.quantum);
        im.dirty = true;
    }
    im.wasPlaying = playing;
    im.active = true;
}

void LinkSync::beforeSubBlock(IFirmware* fw, int offset, int n) noexcept {
    auto& im = *impl_;
    if (!fw) return;
    const bool on = im.active;
    if (fw != im.lastFw || on != im.clockOn) {
        // A new firmware instance, or Link switched: tell the firmware whose clock it follows. "Off" only undoes
        // our own "on": a CLK the user set on the device for a real MIDI clock stays as it is.
        if (on) fw->hostClock(1u);
        else if (im.clockOn && fw == im.lastFw) fw->hostClock(0u);
        im.lastFw = fw;
        im.clockOn = on;
        im.pulser.reset();
        if (!on) im.wasPlaying = false;
    }
    if (!on) return;
    const auto& st = *im.state;
    const double a = static_cast<double>(im.t0) + offset * im.usPerFrame;
    const double b = static_cast<double>(im.t0) + (offset + n) * im.usPerFrame;
    const double b0 = st.beatAtTime(microseconds(static_cast<int64_t>(a)), im.quantum);
    const double b1 = st.beatAtTime(microseconds(static_cast<int64_t>(b)), im.quantum);
    im.pulser.step(b0, b1, st.isPlaying(), [fw](uint32_t status) { fw->midi(status, 0, 0); });
}

void LinkSync::endBuffer() noexcept {
    auto& im = *impl_;
    if (im.active && im.dirty) im.link->commitAudioSessionState(*im.state);
    im.active = false;
}

} // namespace fm1deck
