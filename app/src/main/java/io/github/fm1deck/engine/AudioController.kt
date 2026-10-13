package io.github.fm1deck.engine

import io.github.fm1deck.fw.FirmwareCatalog
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

    /** Native rack slots (AudioEngine::kSlots); the settings use the first [io.github.fm1deck.settings.AppSettings.RACK_SLOTS]. */
    val nativeSlots: Int by lazy { NativeBridge.nativeRackSlots() }

    /**
     * Brings the native rack to [rack] (firmware id per slot, "" = empty), MASTER pot per slot in [masters]:
     * a slot whose firmware changes saves its flash first. A firmware runs in one slot at most (one module, one
     * state), so every slot that lets go is emptied before any loads.
     */
    fun syncRack(rack: List<String>, masters: List<Int>, flashStore: FlashStore): Future<Unit> {
        return executor.submit<Unit> {
            val n = minOf(rack.size, nativeSlots)
            val want = Array(n) { i -> rack[i].takeIf { id -> id.isNotEmpty() && rack.indexOf(id) == i } ?: "" }
            for (i in 0 until n) {
                val current = NativeBridge.nativeFwActiveId(i)
                if (current.isEmpty() || current == want[i]) continue
                flashStore.saveActiveNow(i, current)
                NativeBridge.nativeFwUnload(i)
            }
            for (i in n until nativeSlots) {
                val current = NativeBridge.nativeFwActiveId(i)
                if (current.isNotEmpty()) {
                    flashStore.saveActiveNow(i, current)
                    NativeBridge.nativeFwUnload(i)
                }
            }
            for (i in 0 until n) {
                val id = want[i]
                if (id.isEmpty() || NativeBridge.nativeFwLoaded(i)) continue
                val fw = FirmwareCatalog.findById(id) ?: continue
                val nor = flashStore.loadNor(id, flashStore.getModuleNorSize(i))
                NativeBridge.nativeFwLoadNative(i, id, fw.soname, nor, 0L, masters.getOrElse(i) { 700 })
                flashStore.resetWriteCounter(i)
            }
        }
    }

    /** Saves every slot whose flash changed (the 2 s autosave). */
    fun checkAndAutosave(rack: List<String>, flashStore: FlashStore): Future<Unit> {
        return executor.submit<Unit> {
            for (i in 0 until minOf(rack.size, nativeSlots)) flashStore.checkAndAutosave(i, rack[i])
        }
    }

    /** Saves every slot now (on stop), ahead of whatever is queued after it. */
    fun saveAll(rack: List<String>, flashStore: FlashStore): Future<Unit> {
        return executor.submit<Unit> {
            for (i in 0 until minOf(rack.size, nativeSlots)) flashStore.saveActiveNow(i, rack[i])
        }
    }

    /** Rack meters: live load, live peak, then per slot loaded / load / level. */
    fun readRack(out: FloatArray) = NativeBridge.nativeRackStatus(out)

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
