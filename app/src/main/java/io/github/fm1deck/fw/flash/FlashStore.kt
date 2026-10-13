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
    }

    private val fileStore = FlashFileStore(context.filesDir) { msg -> Log.d(TAG, msg) }

    @Volatile
    private var lastSavedWrites = 0L

    fun getNorFile(id: String): File = fileStore.getNorFile(id)

    fun loadNor(id: String, expectedSize: Int = getModuleNorSize()): ByteArray? {
        return fileStore.loadNor(id, expectedSize)
    }

    fun saveNor(id: String, data: ByteArray, expectedSize: Int = getModuleNorSize()): Boolean {
        return fileStore.saveNor(id, data, expectedSize)
    }

    fun saveActiveNow(id: String): Boolean {
        if (id.isEmpty()) return false
        val norSize = getModuleNorSize()
        NativeBridge.nativeFwSaveNow()
        val snapshot = NativeBridge.nativeFwSnapshotNor()
        return if (snapshot != null) {
            val ok = saveNor(id, snapshot, norSize)
            if (ok) lastSavedWrites = NativeBridge.nativeFwNorWrites()
            ok
        } else {
            Log.w(TAG, "Snapshot returned null for $id")
            false
        }
    }

    fun checkAndAutosave(targetId: String) {
        if (targetId.isEmpty()) return
        val activeId = NativeBridge.nativeFwActiveId()
        if (activeId != targetId) {
            Log.d(TAG, "Skipping autosave: active firmware ($activeId) != target ($targetId)")
            return
        }

        val writes = NativeBridge.nativeFwNorWrites()
        if (writes > lastSavedWrites) {
            val norSize = getModuleNorSize()
            val snapshot = NativeBridge.nativeFwSnapshotNor()
            if (snapshot != null) {
                // Re-check active id after snapshot
                if (NativeBridge.nativeFwActiveId() == targetId) {
                    if (saveNor(targetId, snapshot, norSize)) {
                        lastSavedWrites = writes
                    }
                }
            }
        }
    }

    fun resetWriteCounter() {
        lastSavedWrites = NativeBridge.nativeFwNorWrites()
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

    private fun getModuleNorSize(): Int {
        val size = NativeBridge.nativeFwNorSize()
        return if (size > 0) size.toInt() else DEFAULT_NOR_SIZE
    }
}
