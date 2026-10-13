package io.github.fm1deck.engine

import android.graphics.Bitmap

/**
 * JNI surface of libfm1deck.so. Control-rate only (CONTRIBUTING.md): every call returns quickly and never does audio work.
 * P1 engine calls listed in docs/ARCHITECTURE.md §JNI.
 */
object NativeBridge {
    init {
        System.loadLibrary("fm1deck")
    }

    external fun nativeInfo(): String
    external fun nativeStartAudio(sampleRate: Int, bufferSize: Int): Boolean
    external fun nativeStopAudio()
    external fun nativeIsAudioRunning(): Boolean
    external fun nativeReopenIfNeeded(): Boolean
    external fun nativeStreamInfo(): IntArray
    external fun nativeCpuLoad(): FloatArray
    external fun nativeResetCpuMetrics()

    // Firmware per rack slot (0 until nativeRackSlots()); the panel calls below go to the focused slot
    external fun nativeFwLoadNative(slot: Int, id: String, path: String, nor: ByteArray?, seed: Long, masterAdc: Int): Boolean
    external fun nativeFwUnload(slot: Int)
    external fun nativeFwLoaded(slot: Int): Boolean
    external fun nativeFwInfo(slot: Int): String
    external fun nativeFwCaps(slot: Int): Int
    external fun nativeFwNorSize(slot: Int): Long
    external fun nativeFwActiveId(slot: Int): String
    external fun nativeFwSaveNow(slot: Int): Long
    external fun nativeFwSnapshotNor(slot: Int): ByteArray?
    external fun nativeFwNorWrites(slot: Int): Long
    external fun nativeFwTestHeavy(): Boolean

    // The rack: several firmwares rendered at once into the master bus
    external fun nativeRackSlots(): Int
    external fun nativeRackFocus(slot: Int)
    /** Mute and solo, a bit per slot (any solo: only the soloed slots are heard). */
    external fun nativeRackGate(mute: Int, solo: Int)
    /** out[2 + 3 * slots]: live load, live peak, then per slot loaded (0/1), load, output peak. */
    external fun nativeRackStatus(out: FloatArray)

    external fun nativePanel(buttons: Int, keys: Int)
    external fun nativeEnc(role: Int, steps: Int)
    external fun nativeMaster(adc: Int)
    external fun nativeMidi(status: Int, d1: Int, d2: Int)

    external fun nativeLeds(out: IntArray)
    external fun nativeScreenInto(bitmap: Bitmap, seenFrame: Int): Int
    external fun nativeUiState(out: IntArray, labels: Array<String?>): Boolean
    external fun nativeSetScreenWanted(wanted: Boolean)
    external fun nativePlaying(): Boolean

    // Master bus (P6, docs/MASTER-BUS.md). The parameter table comes from MasterBusParams.h at runtime.
    external fun nativeMasterParamCount(): Int
    /** Rows [id, min, max, default, stepped]; keysOut[i] = "KEY|GROUP|unit". */
    external fun nativeMasterParamTable(keysOut: Array<String?>): FloatArray
    external fun nativeMasterSet(id: Int, value: Float)
    external fun nativeMasterSetQuality(q: Int)
    external fun nativeMasterLatency(): Int
    external fun nativeMasterMeters(out: FloatArray)
    /** Scope history after `since`: out[0] = count, then (in dB, out dB, GR dB) per entry. Returns the new counter. */
    external fun nativeMasterScope(since: Long, out: FloatArray): Long
    external fun nativeMasterScopeEnable(on: Boolean)

    // Ableton Link (docs/LINK.md): off until the user switches it on
    external fun nativeLinkEnable(on: Boolean)
    /** The rack's shared clock: every slot follows one timeline, without the network. */
    external fun nativeLinkSetLocal(on: Boolean)
    external fun nativeLinkSetTempo(bpm: Double)
    external fun nativeLinkSetQuantum(beats: Int)
    external fun nativeLinkTogglePlay()
    /** out[8]: enabled, peers, tempo, playing, beat, quantum, output latency ms, rack clock. */
    external fun nativeLinkStatus(out: DoubleArray)
}
