#pragma once
// SPDX-License-Identifier: GPL-3.0-only

#include "../engine/AudioEngine.h"

namespace fm1deck {

inline AudioEngine& getAudioEngine() {
    static AudioEngine engine;
    return engine;
}

} // namespace fm1deck
