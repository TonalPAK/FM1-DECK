// SPDX-License-Identifier: GPL-3.0-only
#include "JniCommon.h"

#include <jni.h>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <android/bitmap.h>
#endif

extern "C" {

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwLoadNative(
        JNIEnv* env, jobject /*thiz*/, jstring jid, jstring jpath, jbyteArray jnor, jlong seed, jint masterAdc) {
    if (!jid) return JNI_FALSE;
    const char* idChars = env->GetStringUTFChars(jid, nullptr);
    std::string id(idChars);
    env->ReleaseStringUTFChars(jid, idChars);

    std::string path;
    if (jpath) {
        const char* pathChars = env->GetStringUTFChars(jpath, nullptr);
        path = pathChars;
        env->ReleaseStringUTFChars(jpath, pathChars);
    }

    std::vector<uint8_t> nor;
    if (jnor) {
        const jsize len = env->GetArrayLength(jnor);
        nor.resize(static_cast<size_t>(len));
        env->GetByteArrayRegion(jnor, 0, len, reinterpret_cast<jbyte*>(nor.data()));
    }

    std::string err;
    bool ok = fm1deck::getAudioEngine().host()->load(
            id, path, nor, static_cast<uint32_t>(seed), static_cast<uint32_t>(masterAdc), err);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwNorSize(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto* host = fm1deck::getAudioEngine().host();
    return host ? static_cast<jlong>(host->norSize()) : 0;
}

JNIEXPORT jstring JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwActiveId(JNIEnv* env, jobject /*thiz*/) {
    auto* host = fm1deck::getAudioEngine().host();
    std::string id = host ? host->activeId() : "";
    return env->NewStringUTF(id.c_str());
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwUnload(JNIEnv* /*env*/, jobject /*thiz*/) {
    fm1deck::getAudioEngine().host()->unload();
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwLoaded(JNIEnv* /*env*/, jobject /*thiz*/) {
    return fm1deck::getAudioEngine().host()->loaded() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwInfo(JNIEnv* env, jobject /*thiz*/) {
    std::string info = fm1deck::getAudioEngine().host()->info();
    return env->NewStringUTF(info.c_str());
}

JNIEXPORT jint JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwCaps(JNIEnv* /*env*/, jobject /*thiz*/) {
    return static_cast<jint>(fm1deck::getAudioEngine().host()->caps());
}

JNIEXPORT jlong JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwSaveNow(JNIEnv* /*env*/, jobject /*thiz*/) {
    return static_cast<jlong>(fm1deck::getAudioEngine().host()->saveNow());
}

JNIEXPORT jbyteArray JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwSnapshotNor(JNIEnv* env, jobject /*thiz*/) {
    std::vector<uint8_t> nor;
    if (!fm1deck::getAudioEngine().host()->snapshot(nor)) {
        return nullptr;
    }
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(nor.size()));
    if (arr) {
        env->SetByteArrayRegion(arr, 0, static_cast<jsize>(nor.size()), reinterpret_cast<const jbyte*>(nor.data()));
    }
    return arr;
}

JNIEXPORT jlong JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwNorWrites(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto* slot = fm1deck::getAudioEngine().slot();
    return static_cast<jlong>(slot ? slot->norWrites() : 0);
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeFwTestHeavy(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto* host = fm1deck::getAudioEngine().host();
    if (!host) return JNI_FALSE;
    return host->testHeavy() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativePanel(JNIEnv* /*env*/, jobject /*thiz*/, jint buttons, jint keys) {
    auto* slot = fm1deck::getAudioEngine().slot();
    if (!slot) return;
    // With Link on, a lone PLAY is the shared transport's (LinkSync::filterButtons)
    const uint32_t b = fm1deck::getAudioEngine().link()->filterButtons(static_cast<uint32_t>(buttons));
    slot->pushPanel(b, static_cast<uint32_t>(keys));
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeEnc(JNIEnv* /*env*/, jobject /*thiz*/, jint role, jint steps) {
    auto* slot = fm1deck::getAudioEngine().slot();
    if (slot) slot->pushEnc(static_cast<uint32_t>(role), steps);
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMaster(JNIEnv* /*env*/, jobject /*thiz*/, jint adc) {
    auto* slot = fm1deck::getAudioEngine().slot();
    if (slot) slot->setMaster(static_cast<uint32_t>(adc));
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeMidi(JNIEnv* /*env*/, jobject /*thiz*/, jint status, jint d1, jint d2) {
    auto* slot = fm1deck::getAudioEngine().slot();
    if (slot) slot->pushMidi(static_cast<uint32_t>(status), static_cast<uint32_t>(d1), static_cast<uint32_t>(d2));
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeLeds(JNIEnv* env, jobject /*thiz*/, jintArray outLeds) {
    if (!outLeds || env->GetArrayLength(outLeds) < FM1_LED_COUNT) return;
    auto* slot = fm1deck::getAudioEngine().slot();
    if (!slot) return;
    uint32_t leds[FM1_LED_COUNT];
    slot->ledsSnapshot(leds);
    jint jleds[FM1_LED_COUNT];
    for (int i = 0; i < FM1_LED_COUNT; ++i) jleds[i] = static_cast<jint>(leds[i]);
    env->SetIntArrayRegion(outLeds, 0, FM1_LED_COUNT, jleds);
}

JNIEXPORT jint JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeScreenInto(JNIEnv* env, jobject /*thiz*/, jobject bitmap, jint seenFrame) {
#if defined(__ANDROID__)
    if (!bitmap) return seenFrame;

    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) return seenFrame;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return seenFrame;
    if (info.width != 240 || info.height != 240) return seenFrame;

    static thread_local std::array<uint16_t, 240 * 240> rgb565;
    auto* slot = fm1deck::getAudioEngine().slot();
    if (!slot) return seenFrame;

    const uint32_t frame = slot->copyScreen(rgb565.data(), static_cast<uint32_t>(seenFrame));
    if (frame == 0 || frame == static_cast<uint32_t>(seenFrame)) {
        return static_cast<jint>(frame);
    }

    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0 || !pixels) {
        return static_cast<jint>(frame);
    }

    uint32_t* dst = static_cast<uint32_t*>(pixels);
    for (size_t i = 0; i < 240 * 240; ++i) {
        const uint16_t be = rgb565[i];
        const uint32_t c = static_cast<uint32_t>((be >> 8) | ((be & 0xFFu) << 8)); // panel byte order
        uint32_t r = (c >> 11) & 31u;
        uint32_t g = (c >> 5) & 63u;
        uint32_t b = c & 31u;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        // RGBA_8888 on little-endian ARM/x86: [R, G, B, 0xFF]
        dst[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }

    AndroidBitmap_unlockPixels(env, bitmap);
    return static_cast<jint>(frame);
#else
    (void)env; (void)bitmap;
    return seenFrame;
#endif
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeUiState(JNIEnv* env, jobject /*thiz*/, jintArray outInts, jobjectArray labels) {
    auto* slot = fm1deck::getAudioEngine().slot();
    if (!slot) return JNI_FALSE;

    Fm1UiState state;
    if (!slot->readUiState(&state)) return JNI_FALSE;

    if (outInts && env->GetArrayLength(outInts) >= 17) {
        jint ints[17];
        ints[0] = static_cast<jint>(state.layer);
        ints[1] = static_cast<jint>(state.layer_raw);
        ints[2] = static_cast<jint>(state.page);
        ints[3] = static_cast<jint>(state.flags);
        ints[4] = static_cast<jint>(state.knob_valid);
        for (int k = 0; k < 4; ++k) {
            ints[5 + k] = state.knob_value[k];
            ints[9 + k] = state.knob_min[k];
            ints[13 + k] = state.knob_max[k];
        }
        env->SetIntArrayRegion(outInts, 0, 17, ints);
    }

    if (labels && env->GetArrayLength(labels) >= 5) {
        env->SetObjectArrayElement(labels, 0, env->NewStringUTF(state.page_title));
        for (int k = 0; k < 4; ++k) {
            env->SetObjectArrayElement(labels, 1 + k, env->NewStringUTF(state.knob[k]));
        }
    }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativeSetScreenWanted(JNIEnv* /*env*/, jobject /*thiz*/, jboolean wanted) {
    auto* slot = fm1deck::getAudioEngine().slot();
    if (slot) slot->setScreenWanted(wanted == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_io_github_fm1deck_engine_NativeBridge_nativePlaying(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto* slot = fm1deck::getAudioEngine().slot();
    return (slot && slot->playing()) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
