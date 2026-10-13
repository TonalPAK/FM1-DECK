package io.github.fm1deck.engine

import android.content.Context
import android.net.wifi.WifiManager
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** One reading of the Link session (NativeBridge.nativeLinkStatus). */
data class LinkStatus(
    val enabled: Boolean = false,
    val peers: Int = 0,
    val tempo: Double = 120.0,
    val playing: Boolean = false,
    val beat: Double = 0.0,
    val quantum: Int = 4,
    val latencyMs: Double = 0.0
) {
    companion object {
        fun from(v: DoubleArray) = LinkStatus(
            enabled = v[0] >= 0.5, peers = v[1].toInt(), tempo = v[2], playing = v[3] >= 0.5,
            beat = v[4], quantum = v[5].toInt(), latencyMs = v[6]
        )
    }
}

/**
 * Ableton Link from the app side (docs/LINK.md): switching it on is the user's action (the network policy); it is
 * not restored at launch. Android drops Wi-Fi multicast to save power, and Link finds its peers by multicast, so a
 * multicast lock is held while Link is on.
 */
object LinkController {
    /** Observable for the panel (the settings dot shows Link). */
    var enabled by mutableStateOf(false)
        private set

    private var multicastLock: WifiManager.MulticastLock? = null

    fun setEnabled(context: Context, on: Boolean) {
        if (on) acquireMulticast(context) else releaseMulticast()
        NativeBridge.nativeLinkEnable(on)
        enabled = on
    }

    fun setTempo(bpm: Double) = NativeBridge.nativeLinkSetTempo(bpm.coerceIn(20.0, 999.0))
    fun setQuantum(beats: Int) = NativeBridge.nativeLinkSetQuantum(beats)
    fun togglePlay() = NativeBridge.nativeLinkTogglePlay()

    private val buf = DoubleArray(7)
    fun status(): LinkStatus {
        NativeBridge.nativeLinkStatus(buf)
        return LinkStatus.from(buf)
    }

    private fun acquireMulticast(context: Context) {
        if (multicastLock?.isHeld == true) return
        val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return
        multicastLock = wifi.createMulticastLock("fm1deck-link").apply {
            setReferenceCounted(false)
            acquire()
        }
    }

    private fun releaseMulticast() {
        multicastLock?.let { if (it.isHeld) it.release() }
        multicastLock = null
    }
}
