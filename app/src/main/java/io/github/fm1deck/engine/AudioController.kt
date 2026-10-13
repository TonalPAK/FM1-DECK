package io.github.fm1deck.engine

import io.github.fm1deck.fw.flash.FlashStore
import java.util.concurrent.Executors
import java.util.concurrent.Future

data class StreamStatus(
    val sampleRate: Int = 48000,
    val burst: Int = 192,
    val bufferSize: Int = 384,
    val capacity: Int = 1920,
    val isFloat: Boolean = true,
    val isExclusive: Boolean = false,
    val xruns: Int = 0,
    val api: String = "AAudio"
)

data class CpuStatus(
    val p50Us: Float = 0f,
    val p99Us: Float = 0f,
    val maxUs: Float = 0f,
    val budgetUs: Float = 4000f,
    val p50Ratio: Float = 0f,
    val p99Ratio: Float = 0f,
    val maxRatio: Float = 0f
) {
    val p50Percent: Float get() = p50Ratio * 100f
    val p99Percent: Float get() = p99Ratio * 100f
    val maxPercent: Float get() = maxRatio * 100f
}

/**
 * Manages the audio stream, firmware loading, and flash persistence through NativeBridge
 * on a single dedicated control executor (docs/ARCHITECTURE.md §2).
 */
object AudioController {
    val executor = Executors.newSingleThreadExecutor { r ->
        Thread(r, "fm1-control").apply { isDaemon = true }
    }

    @Volatile
    private var isStarted = false

    fun start(sampleRate: Int = 48000, bufferSize: Int = 0): Future<Boolean> {
        return executor.submit<Boolean> {
            val ok = NativeBridge.nativeStartAudio(sampleRate, bufferSize)
            isStarted = ok
            ok
        }
    }

    fun stop(): Future<Unit> {
        return executor.submit<Unit> {
            NativeBridge.nativeStopAudio()
            isStarted = false
        }
    }

    fun reopenIfNeeded(): Future<Boolean> {
        return executor.submit<Boolean> {
            val reopened = NativeBridge.nativeReopenIfNeeded()
            if (reopened) isStarted = true
            reopened
        }
    }

    fun isRunning(): Boolean = isStarted && NativeBridge.nativeIsAudioRunning()

    fun resetCpuMetrics() {
        NativeBridge.nativeResetCpuMetrics()
    }

    fun testHeavy(): Future<Unit> {
        return executor.submit<Unit> {
            NativeBridge.nativeResetCpuMetrics()
            NativeBridge.nativeFwTestHeavy()
        }
    }

    private val activeCaps = java.util.concurrent.atomic.AtomicInteger(0)
    private val isFwLoaded = java.util.concurrent.atomic.AtomicBoolean(false)

    fun getActiveCaps(): Int = activeCaps.get()
    fun isLoaded(): Boolean = isFwLoaded.get()

    private fun updateFwState() {
        val loaded = NativeBridge.nativeFwLoaded()
        isFwLoaded.set(loaded)
        if (loaded) {
            activeCaps.set(NativeBridge.nativeFwCaps())
        } else {
            activeCaps.set(0)
        }
    }

    fun ensureLoaded(fwId: String, soname: String, flashStore: FlashStore, masterAdc: Int): Future<Boolean> {
        return executor.submit<Boolean> {
            if (!NativeBridge.nativeFwLoaded()) {
                val nor = flashStore.loadNor(fwId)
                val ok = NativeBridge.nativeFwLoadNative(fwId, soname, nor, 0L, masterAdc)
                flashStore.resetWriteCounter()
                updateFwState()
                ok
            } else {
                updateFwState()
                true
            }
        }
    }

    fun loadFirmware(fwId: String, soname: String, flashStore: FlashStore, masterAdc: Int): Future<Boolean> {
        return executor.submit<Boolean> {
            val nor = flashStore.loadNor(fwId)
            val ok = NativeBridge.nativeFwLoadNative(fwId, soname, nor, 0L, masterAdc)
            flashStore.resetWriteCounter()
            updateFwState()
            ok
        }
    }

    fun switchFirmware(oldFwId: String, newFwId: String, newSoname: String, flashStore: FlashStore, masterAdc: Int): Future<Boolean> {
        return executor.submit<Boolean> {
            flashStore.saveActiveNow(oldFwId)
            val nor = flashStore.loadNor(newFwId)
            val ok = NativeBridge.nativeFwLoadNative(newFwId, newSoname, nor, 0L, masterAdc)
            flashStore.resetWriteCounter()
            updateFwState()
            ok
        }
    }

    fun reloadFirmware(currentFwId: String, soname: String, flashStore: FlashStore, masterAdc: Int): Future<Boolean> {
        return executor.submit<Boolean> {
            flashStore.saveActiveNow(currentFwId)
            val nor = flashStore.loadNor(currentFwId)
            val ok = NativeBridge.nativeFwLoadNative(currentFwId, soname, nor, 0L, masterAdc)
            flashStore.resetWriteCounter()
            updateFwState()
            ok
        }
    }

    fun checkAndAutosave(activeFwId: String, flashStore: FlashStore): Future<Unit> {
        return executor.submit<Unit> {
            flashStore.checkAndAutosave(activeFwId)
        }
    }

    fun onStop(activeFwId: String, masterAdc: Int, flashStore: FlashStore): Future<Unit> {
        return executor.submit<Unit> {
            flashStore.saveActiveNow(activeFwId)
            flashStore.saveMasterAdc(masterAdc)
            NativeBridge.nativeStopAudio()
            isStarted = false
        }
    }

    fun getStreamStatus(): StreamStatus {
        val arr = NativeBridge.nativeStreamInfo()
        if (arr.size < 9) return StreamStatus()
        return StreamStatus(
            sampleRate = arr[0],
            burst = arr[1],
            bufferSize = arr[2],
            capacity = arr[3],
            isFloat = arr[4] == 1,
            isExclusive = arr[6] == 1,
            xruns = arr[7],
            api = if (arr[8] == 0) "AAudio" else "OpenSL ES"
        )
    }

    fun getCpuStatus(): CpuStatus {
        val arr = NativeBridge.nativeCpuLoad()
        if (arr.size < 6) return CpuStatus()
        val p50Ratio = arr[4]
        val p99Ratio = arr[5]
        val maxRatio = if (arr.size >= 7) arr[6] else if (arr[3] > 0f) arr[2] / arr[3] else 0f
        return CpuStatus(
            p50Us = arr[0],
            p99Us = arr[1],
            maxUs = arr[2],
            budgetUs = arr[3],
            p50Ratio = p50Ratio,
            p99Ratio = p99Ratio,
            maxRatio = maxRatio
        )
    }
}
