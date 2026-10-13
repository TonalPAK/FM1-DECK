package io.github.fm1deck.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.fm1deck.engine.MasterControl
import io.github.fm1deck.engine.NativeBridge
import io.github.fm1deck.settings.AppSettings
import kotlinx.coroutines.delay

/**
 * Settings → Audio (SETTINGS.md §3), the parts that exist now: the master bus quality factor, what the stream got,
 * the callback load and the bus latency. Rate / buffer / render mode choices follow in P5.
 */
@Composable
fun AudioTab(quality: Int, onQuality: (Int) -> Unit) {
    var info by remember { mutableStateOf(IntArray(9)) }
    var cpu by remember { mutableStateOf(FloatArray(7)) }
    var latency by remember { mutableStateOf(0) }
    LaunchedEffect(Unit) {
        while (true) {
            runCatching { info = NativeBridge.nativeStreamInfo() }
            runCatching { cpu = NativeBridge.nativeCpuLoad() }
            runCatching { latency = MasterControl.latencyFrames() }
            delay(1000)
        }
    }
    val rate = info.getOrElse(0) { 48000 }.coerceAtLeast(1)

    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(if (LocalSettingsCompact.current) 2.dp else 16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Text("Quality", color = Color(0xFFD6D6D8), fontWeight = FontWeight.Bold, fontSize = 14.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            for (q in AppSettings.QUALITIES) {
                val on = q == quality
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (on) Color(0xFFFFF4D6) else Color(0xFF2A2A2D))
                        .border(1.dp, Color(0xFF3A3A3E), RoundedCornerShape(6.dp))
                        .clickable { onQuality(q) }
                        .padding(horizontal = 16.dp, vertical = 8.dp)
                ) {
                    Text("×$q", color = if (on) Color(0xFF1D1D1F) else Color(0xFFD6D6D8), fontWeight = FontWeight.SemiBold)
                }
            }
        }
        Text(
            "Oversampling of the master bus saturation and final clipper. Higher = cleaner highs at drive, more CPU. " +
                "The firmware itself always runs at 44.1 kHz.",
            color = Color(0xFF8A8A8E), fontSize = 12.sp
        )

        Text("Stream", color = Color(0xFFD6D6D8), fontWeight = FontWeight.Bold, fontSize = 14.sp)
        InfoRow("Sample rate", "$rate Hz")
        InfoRow("Burst / buffer", "${info.getOrElse(1) { 0 }} / ${info.getOrElse(2) { 0 }} frames " +
            String.format("(%.1f ms)", info.getOrElse(2) { 0 } * 1000f / rate))
        InfoRow("API / sharing", (if (info.getOrElse(8) { 0 } == 0) "AAudio" else "OpenSL ES") + " · " +
            (if (info.getOrElse(6) { 0 } == 1) "exclusive" else "shared"))
        InfoRow("XRuns", "${info.getOrElse(7) { 0 }}")
        InfoRow("Master bus latency", "$latency frames " + String.format("(%.2f ms)", latency * 1000f / rate))

        Text("CPU (audio callback)", color = Color(0xFFD6D6D8), fontWeight = FontWeight.Bold, fontSize = 14.sp)
        InfoRow("p50 / p99 / max", String.format(
            "%.0f / %.0f / %.0f %% of the budget",
            cpu.getOrElse(4) { 0f } * 100f, cpu.getOrElse(5) { 0f } * 100f, cpu.getOrElse(6) { 0f } * 100f
        ))
        Box(
            modifier = Modifier
                .clip(RoundedCornerShape(6.dp))
                .background(Color(0xFF2A2A2D))
                .clickable { NativeBridge.nativeResetCpuMetrics() }
                .padding(horizontal = 12.dp, vertical = 6.dp)
        ) { Text("Reset", color = Color(0xFFD6D6D8), fontSize = 12.sp) }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row {
        Text(label, color = Color(0xFF8A8A8E), fontSize = 12.sp, modifier = Modifier.width(if (LocalSettingsCompact.current) 128.dp else 170.dp))
        Text(value, color = Color(0xFFD6D6D8), fontSize = 12.sp)
    }
}
