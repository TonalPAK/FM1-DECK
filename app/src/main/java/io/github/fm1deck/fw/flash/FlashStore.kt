package io.github.fm1deck.fw.flash

import android.content.Context
import android.util.Log
import io.github.fm1deck.engine.NativeBridge
import java.io.File

/**
 * Manages SPI NOR flash images per firmware using FlashFileStore.
 * Also persists MASTER pot ADC value (0..1023).
 * Must be called from the single control executor (docs/ARCHITECTURE.md §2).
 */
class FlashStore(private val context: Context) {
    companion object {
        private const val TAG = "FlashStore"
        const val DEFAULT_NOR_SIZE = 1024 * 1024 // 1 MB
        private const val PREFS_NAME = "fm1_settings"
        private const val KEY_MASTER_ADC = "master_adc"
        private const val DEFAULT_MASTER_ADC = 700
        /** The native rack's slot count is at most this (AudioEngine::kSlots). */
        const val RACK_CAPACITY = 8
    }

    private val fileStore = FlashFileStore(context.filesDir) { msg -> Log.d(TAG, msg) }

    /** Flash writes already on disk, per rack slot. */
    private val lastSavedWrites = LongArray(RACK_CAPACITY)

    fun getNorFile(id: String): File = fileStore.getNorFile(id)

    fun loadNor(id: String, expectedSize: Int): ByteArray? {
        return fileStore.loadNor(id, expectedSize)
    }

    fun saveNor(id: String, data: ByteArray, expectedSize: Int): Boolean {
        return fileStore.saveNor(id, data, expectedSize)
    }

    /** Saves the flash of the firmware [id] running in [slot] (nothing when another one runs there). */
    fun saveActiveNow(slot: Int, id: String): Boolean {
        if (id.isEmpty() || NativeBridge.nativeFwActiveId(slot) != id) return false
        val norSize = getModuleNorSize(slot)
        NativeBridge.nativeFwSaveNow(slot)
        val snapshot = NativeBridge.nativeFwSnapshotNor(slot)
        return if (snapshot != null) {
            val ok = saveNor(id, snapshot, norSize)
            if (ok) lastSavedWrites[slot] = NativeBridge.nativeFwNorWrites(slot)
            ok
        } else {
            Log.w(TAG, "Snapshot returned null for $id")
            false
        }
    }

    fun checkAndAutosave(slot: Int, targetId: String) {
        if (targetId.isEmpty()) return
        val activeId = NativeBridge.nativeFwActiveId(slot)
        if (activeId != targetId) {
            Log.d(TAG, "Skipping autosave: slot $slot runs ($activeId), not ($targetId)")
            return
        }

        val writes = NativeBridge.nativeFwNorWrites(slot)
        if (writes > lastSavedWrites[slot]) {
            val norSize = getModuleNorSize(slot)
            val snapshot = NativeBridge.nativeFwSnapshotNor(slot)
            if (snapshot != null) {
                // Re-check active id after snapshot
                if (NativeBridge.nativeFwActiveId(slot) == targetId) {
                    if (saveNor(targetId, snapshot, norSize)) {
                        lastSavedWrites[slot] = writes
                    }
                }
            }
        }
    }

    fun resetWriteCounter(slot: Int) {
        lastSavedWrites[slot] = NativeBridge.nativeFwNorWrites(slot)
    }

    fun loadMasterAdc(): Int {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getInt(KEY_MASTER_ADC, DEFAULT_MASTER_ADC)
    }

    fun saveMasterAdc(adc: Int) {
        val clamped = adc.coerceIn(0, 1023)
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putInt(KEY_MASTER_ADC, clamped).apply()
    }

    /** The flash size of the module in [slot]; before one is loaded, the size every FM-1 firmware uses. */
    fun getModuleNorSize(slot: Int): Int {
        val size = NativeBridge.nativeFwNorSize(slot)
        return if (size > 0) size.toInt() else DEFAULT_NOR_SIZE
    }
}
