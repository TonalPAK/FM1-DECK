// SPDX-License-Identifier: GPL-3.0-only
#include "JniCommon.h"
#include "../dsp/MasterBusParams.h"

#include <jni.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <oboe/Oboe.h>
#endif

extern "C" {

JNIEXPORT jstring JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeInfo(JNIEnv* env, jobject /*thiz*/) {
    const std::string info = std::string("fm1deck native | fw ABI ") + std::to_string(FM1DECK_FW_ABI) +
#if defined(__ANDROID__)
            " | oboe " + std::to_string(OBOE_VERSION_MAJOR) + "." + std::to_string(OBOE_VERSION_MINOR) +
#else
            " | host" +
#endif
            " | master meters " + std::to_string(static_cast<int>(MasterMeterId::COUNT));
    return env->NewStringUTF(info.c_str());
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeStartAudio(JNIEnv* /*env*/, jobject /*thiz*/, jint sampleRate, jint bufferSize) {
    return fm1deck::getAudioEngine().start(sampleRate, bufferSize) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeStopAudio(JNIEnv* /*env*/, jobject /*thiz*/) {
    fm1deck::getAudioEngine().stop();
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeIsAudioRunning(JNIEnv* /*env*/, jobject /*thiz*/) {
    return fm1deck::getAudioEngine().isRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeReopenIfNeeded(JNIEnv* /*env*/, jobject /*thiz*/) {
    return fm1deck::getAudioEngine().reopenIfNeeded() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jintArray JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeStreamInfo(JNIEnv* env, jobject /*thiz*/) {
    const auto info = fm1deck::getAudioEngine().streamInfo();
    jint arr[9] = {
        info.sampleRate,
        info.burst,
        info.bufferSize,
        info.capacity,
        info.format,
        info.sharingMode,
        info.isExclusive,
        info.xruns,
        info.api
    };
    jintArray result = env->NewIntArray(9);
    if (result) {
        env->SetIntArrayRegion(result, 0, 9, arr);
    }
    return result;
}

JNIEXPORT jfloatArray JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeCpuLoad(JNIEnv* env, jobject /*thiz*/) {
    const auto m = fm1deck::getAudioEngine().cpuMetrics();
    jfloat arr[7] = {
        m.p50Us,
        m.p99Us,
        m.maxUs,
        m.budgetUs,
        m.p50Ratio,
        m.p99Ratio,
        m.maxRatio
    };
    jfloatArray result = env->NewFloatArray(7);
    if (result) {
        env->SetFloatArrayRegion(result, 0, 7, arr);
    }
    return result;
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeResetCpuMetrics(JNIEnv* /*env*/, jobject /*thiz*/) {
    fm1deck::getAudioEngine().resetCpuMetrics();
}

// ---- master bus (P6) ---------------------------------------------------------------------------------------------

/** Rows of [id, min, max, default, kind (0 = continuous, 1 = stepped)]; names in keysOut as "KEY|GROUP|unit". */
JNIEXPORT jfloatArray JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterParamTable(JNIEnv* env, jobject /*thiz*/, jobjectArray keysOut) {
    int count = 0;
    const auto* t = fm1deck::dsp::masterParams(&count);
    std::vector<jfloat> rows(static_cast<size_t>(count) * 5);
    for (int i = 0; i < count; ++i) {
        rows[static_cast<size_t>(i) * 5 + 0] = static_cast<jfloat>(t[i].id);
        rows[static_cast<size_t>(i) * 5 + 1] = t[i].min;
        rows[static_cast<size_t>(i) * 5 + 2] = t[i].max;
        rows[static_cast<size_t>(i) * 5 + 3] = t[i].def;
        rows[static_cast<size_t>(i) * 5 + 4] = t[i].kind == 'S' ? 1.0f : 0.0f;
        if (keysOut && i < env->GetArrayLength(keysOut)) {
            const std::string name = std::string(t[i].key) + "|" + t[i].group + "|" + t[i].unit;
            jstring s = env->NewStringUTF(name.c_str());
            env->SetObjectArrayElement(keysOut, i, s);
            env->DeleteLocalRef(s);
        }
    }
    jfloatArray result = env->NewFloatArray(static_cast<jsize>(rows.size()));
    if (result) env->SetFloatArrayRegion(result, 0, static_cast<jsize>(rows.size()), rows.data());
    return result;
}

JNIEXPORT jint JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterParamCount(JNIEnv* /*env*/, jobject /*thiz*/) {
    int count = 0;
    fm1deck::dsp::masterParams(&count);
    return count;
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterSet(JNIEnv* /*env*/, jobject /*thiz*/, jint id, jfloat value) {
    fm1deck::getAudioEngine().masterBus()->setParam(id, value);
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterSetQuality(JNIEnv* /*env*/, jobject /*thiz*/, jint q) {
    fm1deck::getAudioEngine().masterBus()->setQuality(q);
}

JNIEXPORT jint JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterLatency(JNIEnv* /*env*/, jobject /*thiz*/) {
    return fm1deck::getAudioEngine().masterBus()->latency();
}

/** Fills out[MasterMeterId::COUNT] (dB / ratio values per FM1_MASTER_METERS). */
JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterMeters(JNIEnv* env, jobject /*thiz*/, jfloatArray out) {
    constexpr int n = fm1deck::dsp::MasterBus::kMeterCount;
    if (!out || env->GetArrayLength(out) < n) return;
    float m[n];
    fm1deck::getAudioEngine().masterBus()->readMeters(m);
    env->SetFloatArrayRegion(out, 0, n, m);
}

/**
 * Scope history since `since`: out[0] = entry count, then MasterBus::kScopeFields floats per entry (in peak dB,
 * out peak dB, gain reduction dB). Returns the new running counter.
 */
JNIEXPORT jlong JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterScope(JNIEnv* env, jobject /*thiz*/, jlong since, jfloatArray out) {
    using fm1deck::dsp::MasterBus;
    if (!out) return since;
    const int len = env->GetArrayLength(out);
    const int maxEntries = std::min((len - 1) / MasterBus::kScopeFields, MasterBus::kScopeLen);
    if (maxEntries <= 0) return since;
    static thread_local std::array<float, 1 + MasterBus::kScopeLen * MasterBus::kScopeFields> buf;
    int count = 0;
    const uint64_t head = fm1deck::getAudioEngine().masterBus()->readScope(
        static_cast<uint64_t>(since), buf.data() + 1, maxEntries, count);
    buf[0] = static_cast<float>(count);
    env->SetFloatArrayRegion(out, 0, 1 + count * MasterBus::kScopeFields, buf.data());
    return static_cast<jlong>(head);
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMasterScopeEnable(JNIEnv* /*env*/, jobject /*thiz*/, jboolean on) {
    fm1deck::getAudioEngine().masterBus()->setScopeEnabled(on == JNI_TRUE);
}

} // extern "C"
