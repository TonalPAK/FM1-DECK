// SPDX-License-Identifier: GPL-3.0-only
// Ableton Link over JNI (docs/LINK.md). The network starts only when the user switches Link on.
#include "JniCommon.h"

#include <jni.h>

extern "C" {

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeLinkEnable(JNIEnv* /*env*/, jobject /*thiz*/, jboolean on) {
    auto& engine = fm1deck::getAudioEngine();
    engine.updateOutputLatency();
    engine.link()->setEnabled(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeLinkSetTempo(JNIEnv* /*env*/, jobject /*thiz*/, jdouble bpm) {
    fm1deck::getAudioEngine().link()->setTempo(bpm);
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeLinkSetQuantum(JNIEnv* /*env*/, jobject /*thiz*/, jint beats) {
    fm1deck::getAudioEngine().link()->setQuantum(beats);
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeLinkTogglePlay(JNIEnv* /*env*/, jobject /*thiz*/) {
    fm1deck::getAudioEngine().link()->togglePlay();
}

/** out[7]: enabled, peers, tempo, playing, beat, quantum, output latency ms. Also refreshes the latency. */
JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeLinkStatus(JNIEnv* env, jobject /*thiz*/, jdoubleArray out) {
    if (!out || env->GetArrayLength(out) < 7) return;
    auto& engine = fm1deck::getAudioEngine();
    engine.updateOutputLatency();
    const auto s = engine.link()->status();
    const jdouble v[7] = {
        s.enabled ? 1.0 : 0.0, static_cast<double>(s.peers), s.tempo, s.playing ? 1.0 : 0.0, s.beat,
        static_cast<double>(s.quantum), engine.link()->outputLatencyUs() / 1000.0
    };
    env->SetDoubleArrayRegion(out, 0, 7, v);
}

} // extern "C"
