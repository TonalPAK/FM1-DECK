package io.github.fm1deck.ui.settings

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.fm1deck.engine.LinkController
import io.github.fm1deck.engine.LinkStatus
import kotlinx.coroutines.isActive
import kotlin.math.floor

private val LCard = Color(0xFF1D1D1F)
private val LPart = Color(0xFF2A2A2D)
private val LEdge = Color(0xFF3A3A3E)
private val LLabel = Color(0xFFD6D6D8)
private val LDim = Color(0xFF8A8A8E)
private val LLit = Color(0xFFFFF4D6)
private val LGreen = Color(0xFF5FE08A)

/**
 * Settings → MIDI: Ableton Link (docs/LINK.md) — tempo, beat phase and start/stop shared with the other devices
 * on the same Wi-Fi. USB / BLE MIDI follow in P9.
 */
@Composable
fun LinkTab(quantum: Int, onQuantumChanged: (Int) -> Unit) {
    val context = LocalContext.current
    var st by remember { mutableStateOf(LinkController.status()) }
    val taps = remember { mutableStateListOf<Long>() }

    // ~30 Hz while the tab is shown: the beat lights and the peers
    LaunchedEffect(Unit) {
        var last = 0L
        while (isActive) {
            val now = withFrameNanos { it }
            if (now - last > 33_000_000L) {
                st = LinkController.status()
                last = now
            }
        }
    }

    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(4.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("ABLETON LINK", color = LDim, fontSize = 11.sp, fontWeight = FontWeight.Bold)

        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            LinkPill(if (st.enabled) "LINK ON" else "LINK OFF", st.enabled, big = true) {
                LinkController.setEnabled(context, !st.enabled)
                st = LinkController.status()
            }
            Column {
                Text(peersText(st), color = if (st.enabled && st.peers > 0) LGreen else LLabel, fontSize = 13.sp)
                Text(
                    if (st.enabled) "Same Wi-Fi as the other phone; Link on there too."
                    else "Shares tempo, beat and start/stop with phones and apps on this Wi-Fi.",
                    color = LDim, fontSize = 11.sp
                )
            }
        }

        // ---- tempo, beat, transport ---------------------------------------------------------------------------
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(10.dp))
                .background(LCard)
                .border(1.dp, Color(0xFF2C2C2E), RoundedCornerShape(10.dp))
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            LinkPill("−1", false) { LinkController.setTempo(st.tempo - 1.0) }
            TempoReadout(st, enabled = st.enabled)
            LinkPill("+1", false) { LinkController.setTempo(st.tempo + 1.0) }
            LinkPill("TAP", false) {
                val now = System.nanoTime()
                if (taps.isNotEmpty() && now - taps.last() > 2_000_000_000L) taps.clear()
                taps.add(now)
                if (taps.size > 5) taps.removeAt(0)
                if (taps.size >= 2) {
                    val avgNs = (taps.last() - taps.first()).toDouble() / (taps.size - 1)
                    LinkController.setTempo(60e9 / avgNs)
                }
            }
            Spacer(Modifier.weight(1f))
            BeatLights(st)
            LinkPill(if (st.playing) "STOP" else "PLAY", st.playing) { LinkController.togglePlay() }
        }

        // ---- quantum --------------------------------------------------------------------------------------------
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Start on", color = LLabel, fontSize = 12.sp, modifier = Modifier.width(80.dp))
            for ((q, name) in listOf(1 to "beat", 2 to "2 beats", 4 to "bar", 8 to "2 bars")) {
                LinkPill(name, quantum == q) { onQuantumChanged(q); LinkController.setQuantum(q) }
            }
        }

        Text(
            "PLAY on the panel starts and stops every linked device; a start lands on the next ${quantumName(quantum)}. " +
                "GLO+PLAY and REC+PLAY work as on the device. " +
                "Sound is aligned for this phone's output latency (${String.format("%.1f", st.latencyMs)} ms).",
            color = LDim, fontSize = 11.sp
        )

        Spacer(Modifier.height(4.dp))
        Text("USB / BLE MIDI", color = LDim, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Text("External keyboards and MIDI clock over USB and Bluetooth: coming in P9.", color = LDim, fontSize = 11.sp)
    }
}

private fun peersText(st: LinkStatus): String = when {
    !st.enabled -> "Off"
    st.peers == 0 -> "Waiting for other devices…"
    st.peers == 1 -> "Linked with 1 device"
    else -> "Linked with ${st.peers} devices"
}

private fun quantumName(q: Int) = when (q) {
    1 -> "beat"; 2 -> "2 beats"; 4 -> "bar"; 8 -> "2 bars"; else -> "$q beats"
}

/** The tempo; drag sideways to change it (1 BPM per 6 dp). */
@Composable
private fun TempoReadout(st: LinkStatus, enabled: Boolean) {
    var acc by remember { mutableStateOf(0f) }
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .width(110.dp)
            .pointerInput(Unit) {
                detectHorizontalDragGestures(onDragStart = { acc = 0f }) { change, dx ->
                    change.consume()
                    acc += dx
                    val step = 6.dp.toPx()
                    val n = (acc / step).toInt()
                    if (n != 0) {
                        acc -= n * step
                        LinkController.setTempo(LinkController.status().tempo + n)
                    }
                }
            }
    ) {
        Text(
            String.format("%.1f", st.tempo),
            color = if (enabled) LLit else LLabel,
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold
        )
        Text("BPM", color = LDim, fontSize = 10.sp)
    }
}

/** One light per beat of the quantum; the current one is lit, green while the transport plays. */
@Composable
private fun BeatLights(st: LinkStatus) {
    val q = st.quantum.coerceAtLeast(1)
    Canvas(modifier = Modifier.width((q * 14).dp).height(14.dp)) {
        val cell = size.width / q
        val phase = ((st.beat % q) + q) % q
        val cur = floor(phase).toInt()
        for (i in 0 until q) {
            val on = st.enabled && i == cur
            drawRoundRect(
                if (on) (if (st.playing) LGreen else LLit) else LPart,
                topLeft = Offset(i * cell + 2f, 2f),
                size = Size(cell - 4f, size.height - 4f),
                cornerRadius = CornerRadius(3f)
            )
        }
    }
}

@Composable
private fun LinkPill(text: String, on: Boolean, big: Boolean = false, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(if (on) LLit else LPart)
            .border(1.dp, LEdge, RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = if (big) 16.dp else 10.dp, vertical = if (big) 10.dp else 6.dp)
    ) {
        Text(text, color = if (on) LCard else LLabel, fontSize = if (big) 13.sp else 11.sp, fontWeight = FontWeight.SemiBold)
    }
}
