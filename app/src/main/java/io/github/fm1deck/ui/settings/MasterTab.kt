package io.github.fm1deck.ui.settings

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.fm1deck.engine.MasterControl
import io.github.fm1deck.engine.MasterMeter
import io.github.fm1deck.engine.MasterParam
import io.github.fm1deck.engine.MasterPreset
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sqrt
import kotlin.math.tanh

private val CardBg = Color(0xFF1D1D1F)
private val Tray = Color(0xFF141415)
private val Part = Color(0xFF2A2A2D)
private val Edge = Color(0xFF3A3A3E)
private val Label = Color(0xFFD6D6D8)
private val Dim = Color(0xFF8A8A8E)
private val Lit = Color(0xFFFFF4D6)
private val Fill = Color(0xFFB8B2A0)
private val Accent = Color(0xFF79B8FF)
private val Green = Color(0xFF5FE08A)
private val Warn = Color(0xFFFF453A)
private val ScopeIn = Color(0xFF4A4A50)

/** Human names for the parameter keys; units come from the table. */
private val NAMES = mapOf(
    "BYPASS" to "Bypass", "INPUT_GAIN" to "Input", "OUTPUT_GAIN" to "Output", "MIX" to "Mix", "AUTO_GAIN" to "Auto-gain",
    "SAT_DRIVE" to "Drive", "SAT_SHAPE" to "Shape", "SAT_SYMMETRY" to "Symmetry", "SAT_DYNAMICS" to "Dynamics",
    "SAT_DYN_SPEED" to "Speed", "SAT_TILT" to "Tilt", "SAT_MIX" to "Mix",
    "SPC_CEILING" to "Ceiling", "SPC_SHAPE" to "Shape", "SPC_SPREAD" to "Spread", "SPC_BANDS" to "Bands",
    "SPC_PEAK_MIX" to "Peak return", "SPC_PEAK_SENS" to "Peak sense",
    "LIM_CEILING" to "Ceiling", "LIM_LOOKAHEAD" to "Look-ahead", "LIM_RELEASE" to "Release",
    "LIM_AUTO_RELEASE" to "Auto release", "LIM_TRUE_PEAK" to "True peak", "LIM_LINK" to "Stereo link",
    "CLIP_SHAPE" to "Shape", "CLIP_POSITION" to "Position"
)

/** The chain, in signal order: group, chip label, page title, enable key (null = always on). */
private data class Stage(val group: String, val chip: String, val title: String, val enableKey: String?)

private val CHAIN = listOf(
    Stage("GLOBAL", "I/O", "Input · Output", null),
    Stage("SAT", "SAT", "Dynamic saturation", "SAT_ENABLE"),
    Stage("SPECTRAL", "SPEC", "Spectral clipper", "SPC_ENABLE"),
    Stage("LIMITER", "LIMIT", "Look-ahead limiter", "LIM_ENABLE"),
    Stage("CLIPPER", "CLIP", "Final clipper", "CLIP_ENABLE")
)

/** Keys shown in the header instead of the I/O page. */
private val HEADER_KEYS = setOf("BYPASS", "AUTO_GAIN")

private fun formatValue(p: MasterParam, v: Float): String = when {
    p.key == "SPC_BANDS" -> listOf("4", "6", "8").getOrElse(v.toInt()) { "4" }
    p.key == "CLIP_POSITION" -> if (v >= 0.5f) "before lim" else "after lim"
    p.unit == "x" -> "${(v * 100).toInt()} %"
    p.unit == "dB" -> String.format("%+.1f dB", v)
    p.unit == "ms" -> if (v < 10) String.format("%.1f ms", v) else "${v.toInt()} ms"
    else -> String.format("%.2f", v)
}

/** Scope history kept on the UI side: entries of (in dB, out dB, GR dB), about 2 s at 64-frame blocks. */
private const val HIST = 2048

/**
 * Settings → Master (SETTINGS.md §4), compact: a header with bypass / auto-gain / A-B / presets,
 * then two halves — the signal scope with meters on the left, the effect chain and the selected stage's controls
 * (two per row) on the right. The scope can be switched off; it runs only while this tab is shown.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun MasterTab(
    initialValues: Map<String, Float>,
    presetName: String,
    builtInPresets: List<MasterPreset>,
    userPresets: List<MasterPreset>,
    scopeOn: Boolean,
    onScopeChanged: (Boolean) -> Unit,
    onCommit: (values: Map<String, Float>, presetName: String) -> Unit,
    onSaveUserPreset: (MasterPreset) -> Unit
) {
    val table = remember { MasterControl.params }
    val values = remember { mutableStateMapOf<String, Float>().apply { putAll(MasterControl.resolve(table, initialValues)) } }
    var preset by remember { mutableStateOf(presetName) }
    val slots = remember { mutableStateMapOf<String, Map<String, Float>>() }
    var activeSlot by remember { mutableStateOf("A") }
    var selected by rememberSaveable { mutableStateOf("SAT") }

    fun set(key: String, v: Float, commit: Boolean) {
        values[key] = v
        // Stepped parameters change latency or the signal path (a short re-prime fade): send them once, at the end
        val stepped = table.firstOrNull { it.key == key }?.stepped == true
        if (commit || !stepped) MasterControl.send(key, v)
        if (commit) onCommit(values.toMap(), preset)
    }

    fun loadState(state: Map<String, Float>, name: String) {
        values.putAll(state)
        for ((k, v) in state) MasterControl.send(k, v)
        preset = name
        onCommit(values.toMap(), preset)
    }

    // ---- meters (snapshot state: each poll is a new array, so readers recompose) and the scope history ---------
    var meters by remember { mutableStateOf(FloatArray(MasterMeter.COUNT) { -120f }) }
    val hist = remember { FloatArray(HIST * MasterControl.SCOPE_FIELDS) { -120f } }
    var histHead by remember { mutableLongStateOf(0L) }  // entries written in total; also the redraw trigger
    val scopeOnNow by rememberUpdatedState(scopeOn)

    DisposableEffect(Unit) { onDispose { MasterControl.setScopeEnabled(false) } }
    LaunchedEffect(scopeOn) {
        MasterControl.setScopeEnabled(scopeOn)
        val buf = FloatArray(1 + 1024 * MasterControl.SCOPE_FIELDS)
        var since = MasterControl.readScope(Long.MAX_VALUE, buf)  // start from now
        var lastMeters = 0L
        while (isActive) {
            if (scopeOnNow) {
                val now = withFrameNanos { it }
                since = MasterControl.readScope(since, buf)
                val count = buf[0].toInt()
                var h = histHead
                for (i in 0 until count) {
                    val dst = ((h % HIST).toInt()) * MasterControl.SCOPE_FIELDS
                    for (f in 0 until MasterControl.SCOPE_FIELDS) hist[dst + f] = buf[1 + i * MasterControl.SCOPE_FIELDS + f]
                    h++
                }
                histHead = if (count > 0) h else histHead
                if (now - lastMeters > 33_000_000L) {
                    meters = FloatArray(MasterMeter.COUNT).also { MasterControl.readMeters(it) }
                    lastMeters = now
                }
            } else {
                meters = FloatArray(MasterMeter.COUNT).also { MasterControl.readMeters(it) }
                delay(33)
            }
        }
    }

    val compact = LocalSettingsCompact.current
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = if (compact) 0.dp else 12.dp, vertical = if (compact) 0.dp else 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        // ---- header (upright: wraps onto a second line) --------------------------------------------------------
        val header: @Composable () -> Unit = {
            Pill("BYPASS", values["BYPASS"] == 1f, warn = true) { set("BYPASS", if (values["BYPASS"] == 1f) 0f else 1f, true) }
            Pill("AUTO-GAIN", values["AUTO_GAIN"] == 1f) { set("AUTO_GAIN", if (values["AUTO_GAIN"] == 1f) 0f else 1f, true) }
            if (!compact) Spacer(Modifier.width(4.dp))
            for (slot in listOf("A", "B")) {
                Pill(slot, activeSlot == slot) {
                    if (activeSlot != slot) {
                        slots[activeSlot] = values.toMap()
                        activeSlot = slot
                        slots[slot]?.let { loadState(it, preset) }
                    }
                }
            }
            if (!compact) Spacer(Modifier.width(4.dp))
            PresetMenu(
                current = preset,
                presets = builtInPresets + userPresets,
                onPick = { p -> loadState(MasterControl.applyPreset(table, p), p.name) },
                onSave = { name ->
                    onSaveUserPreset(MasterPreset(name, values.toMap(), builtIn = false))
                    preset = name
                    onCommit(values.toMap(), preset)
                }
            )
            if (!compact) Spacer(Modifier.weight(1f))
            Pill("SCOPE", scopeOn) { onScopeChanged(!scopeOn) }
        }
        if (compact) {
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) { header() }
        } else {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) { header() }
        }

        // left: what happens to the signal
        val signal: @Composable (Modifier) -> Unit = { mod ->
            Column(
                modifier = mod
                    .clip(RoundedCornerShape(10.dp))
                    .background(Tray)
                    .padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Box(modifier = Modifier.weight(1f).fillMaxWidth()) {
                    if (scopeOn) {
                        Scope(hist, { histHead }, values["LIM_CEILING"] ?: -0.3f)
                    } else {
                        Text(
                            "Scope off",
                            color = Dim, fontSize = 11.sp,
                            modifier = Modifier.align(Alignment.Center)
                        )
                    }
                }
                if (scopeOn) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                        Legend(Accent, "in")
                        Legend(Fill, "out")
                        Legend(Warn, "gain reduction")
                        Legend(Dim, "ceiling")
                    }
                }
                MeterBlock(meters)
            }
        }
        // right: the chain and the selected stage
        val chain: @Composable (Modifier) -> Unit = { mod ->
            Column(modifier = mod, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ChainStrip(selected, values, meters, onSelect = { selected = it })
                val stage = CHAIN.first { it.group == selected }
                StagePage(stage, table, values, meters, ::set)
            }
        }

        // ---- two halves: side by side, or stacked when upright -----------------------------------------------------
        if (compact) {
            signal(Modifier.weight(0.42f).fillMaxWidth())
            chain(Modifier.weight(0.58f).fillMaxWidth())
        } else {
            Row(modifier = Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                signal(Modifier.weight(0.47f).fillMaxHeight())
                chain(Modifier.weight(0.53f).fillMaxHeight())
            }
        }
    }
}

// ---- header pieces ------------------------------------------------------------------------------------------------

@Composable
private fun Pill(text: String, on: Boolean, warn: Boolean = false, small: Boolean = false, onClick: () -> Unit) {
    val bg = when {
        on && warn -> Warn
        on -> Lit
        else -> Part
    }
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(bg)
            .border(1.dp, Edge, RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = if (small) 8.dp else 10.dp, vertical = if (small) 4.dp else 6.dp)
    ) {
        Text(
            text,
            color = if (on) CardBg else Label,
            fontSize = if (small) 10.sp else 11.sp,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1
        )
    }
}

@Composable
private fun PresetMenu(
    current: String,
    presets: List<MasterPreset>,
    onPick: (MasterPreset) -> Unit,
    onSave: (String) -> Unit
) {
    var open by remember { mutableStateOf(false) }
    var saveName by remember { mutableStateOf("") }
    Box {
        Row(
            modifier = Modifier
                .clip(RoundedCornerShape(6.dp))
                .background(Part)
                .border(1.dp, Edge, RoundedCornerShape(6.dp))
                .clickable { open = true }
                .padding(horizontal = 10.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("PRESET ", color = Dim, fontSize = 10.sp)
            Text(current, color = Lit, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
            Text("  ▾", color = Dim, fontSize = 11.sp)
        }
        DropdownMenu(
            expanded = open,
            onDismissRequest = { open = false },
            modifier = Modifier.background(CardBg)
        ) {
            for (p in presets) {
                DropdownMenuItem(
                    text = {
                        Text(
                            p.name + if (p.builtIn) "" else "  ·  user",
                            color = if (p.name == current) Lit else Label,
                            fontSize = 12.sp,
                            fontWeight = if (p.name == current) FontWeight.Bold else FontWeight.Normal
                        )
                    },
                    onClick = { onPick(p); open = false },
                    modifier = Modifier.height(34.dp)
                )
            }
            Row(
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                BasicTextField(
                    value = saveName,
                    onValueChange = { saveName = it.take(24) },
                    singleLine = true,
                    textStyle = TextStyle(color = Label, fontSize = 12.sp),
                    cursorBrush = SolidColor(Lit),
                    modifier = Modifier
                        .width(120.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(Tray)
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    decorationBox = { inner ->
                        if (saveName.isEmpty()) Text("save as…", color = Dim, fontSize = 12.sp)
                        inner()
                    }
                )
                Pill("SAVE", false, small = true) {
                    val name = saveName.trim()
                    if (name.isNotEmpty()) {
                        onSave(name)
                        saveName = ""
                        open = false
                    }
                }
            }
        }
    }
}

// ---- left half: scope and meters ----------------------------------------------------------------------------------

/**
 * dB → 0..1 of the half height: the square root of the amplitude with +6 dB of headroom at the top (0 dBFS = 0.71,
 * −12 dB = 0.35), so the waveform reads as a waveform and an input going over the ceiling shows above the output.
 */
private fun ampFrac(db: Float): Float = if (db <= -90f) 0f else 10f.pow((db - 6f) / 40f).coerceIn(0f, 1f)

/** Meter bars: linear in dB, −48..0. */
private fun barFrac(db: Float): Float = ((db + 48f) / 48f).coerceIn(0f, 1f)

/** Gain reduction scale: 0..−12 dB over the top 45 % of the height. */
private const val GR_RANGE_DB = 12f

/**
 * The signal over the last ~2 s, newest at the right: the input as a grey mirrored waveform, the output in cream over
 * it (where they differ, the peaks were pressed down), gain reduction in red from the top, the ceiling dashed.
 */
@Composable
private fun Scope(hist: FloatArray, headOf: () -> Long, ceilingDb: Float) {
    val inTop = remember { Path() }
    val outTop = remember { Path() }
    val grPath = remember { Path() }
    val grLine = remember { Path() }
    Canvas(modifier = Modifier.fillMaxSize()) {
        val head = headOf()  // state read in the draw phase: only the canvas redraws on new entries
        val w = size.width
        val h = size.height
        val cy = h / 2f
        val half = h / 2f - 2f
        val cols = max(1, (w / 2f).toInt())          // one column per 2 px
        val perCol = HIST.toFloat() / cols
        val oldest = head - HIST

        // grid: −6 / −12 / −24 dB
        for (db in floatArrayOf(-6f, -12f, -24f)) {
            val d = ampFrac(db) * half
            drawLine(Color(0xFF222225), Offset(0f, cy - d), Offset(w, cy - d))
            drawLine(Color(0xFF222225), Offset(0f, cy + d), Offset(w, cy + d))
        }
        drawLine(Color(0xFF2A2A2E), Offset(0f, cy), Offset(w, cy))

        inTop.reset(); outTop.reset(); grPath.reset(); grLine.reset()
        var anyGr = false
        val inY = FloatArray(cols)
        val outY = FloatArray(cols)
        for (c in 0 until cols) {
            var inDb = -120f
            var outDb = -120f
            var gr = 0f
            val a = (c * perCol).toInt()
            val b = max(a + 1, ((c + 1) * perCol).toInt())
            for (k in a until b) {
                val idx = oldest + k
                if (idx < 0) continue
                val o = (idx % HIST).toInt() * MasterControl.SCOPE_FIELDS
                inDb = max(inDb, hist[o])
                outDb = max(outDb, hist[o + 1])
                gr = minOf(gr, hist[o + 2])
            }
            inY[c] = ampFrac(inDb) * half
            outY[c] = ampFrac(outDb) * half
            val x = c * 2f
            val gy = (-gr / GR_RANGE_DB).coerceIn(0f, 1f) * h * 0.45f
            if (c == 0) { grPath.moveTo(0f, 0f); grLine.moveTo(0f, gy) } else grLine.lineTo(x, gy)
            grPath.lineTo(x, gy)
            if (gy > 0.5f) anyGr = true
        }
        mirrored(inTop, inY, cy)
        mirrored(outTop, outY, cy)
        // output filled; the input over it as a contour, so pressed-down peaks show as the gap between them
        drawPath(inTop, ScopeIn.copy(alpha = 0.6f))
        drawPath(outTop, Fill.copy(alpha = 0.8f))
        drawPath(inTop, Accent.copy(alpha = 0.9f), style = Stroke(width = 1.3f))
        if (anyGr) {
            grPath.lineTo((cols - 1) * 2f, 0f)
            grPath.close()
            drawPath(grPath, Warn.copy(alpha = 0.35f))
            drawPath(grLine, Warn, style = Stroke(width = 1.5f))
        }

        // ceiling
        val cd = ampFrac(ceilingDb) * half
        val dash = PathEffect.dashPathEffect(floatArrayOf(6f, 6f))
        drawLine(Dim.copy(alpha = 0.8f), Offset(0f, cy - cd), Offset(w, cy - cd), pathEffect = dash)
        drawLine(Dim.copy(alpha = 0.8f), Offset(0f, cy + cd), Offset(w, cy + cd), pathEffect = dash)
    }
}

@Composable
private fun Legend(color: Color, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(width = 10.dp, height = 3.dp).background(color))
        Text(" $text", color = Dim, fontSize = 9.sp)
    }
}

/** A filled shape mirrored around cy from per-column half heights (2 px per column). */
private fun mirrored(path: Path, ys: FloatArray, cy: Float) {
    if (ys.isEmpty()) return
    path.moveTo(0f, cy - ys[0])
    for (c in 1 until ys.size) path.lineTo(c * 2f, cy - ys[c])
    for (c in ys.size - 1 downTo 0) path.lineTo(c * 2f, cy + ys[c])
    path.close()
}

@Composable
private fun MeterBlock(m: FloatArray) {
    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        LevelBar("IN", m[MasterMeter.IN_PEAK_L], m[MasterMeter.IN_PEAK_R])
        LevelBar("OUT", m[MasterMeter.OUT_PEAK_L], m[MasterMeter.OUT_PEAK_R])
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            val tp = m[MasterMeter.OUT_TRUE_PEAK]
            Readout("TP", if (tp < -90f) "−∞" else String.format("%.1f", tp), tp > -0.1f)
            val lufs = m[MasterMeter.OUT_LUFS_S]
            Readout("LUFS", if (lufs < -69f) "−∞" else String.format("%.1f", lufs), false)
            Readout("DSP", String.format("%.0f %%", m[MasterMeter.DSP_LOAD]), m[MasterMeter.DSP_LOAD] > 60f)
            Readout("LAT", "${MasterControl.latencyFrames()}", false)
        }
    }
}

@Composable
private fun Readout(label: String, value: String, hot: Boolean) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label ", color = Dim, fontSize = 9.sp)
        Text(value, color = if (hot) Warn else Label, fontSize = 10.sp)
    }
}

@Composable
private fun LevelBar(label: String, l: Float, r: Float) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = Dim, fontSize = 9.sp, modifier = Modifier.width(26.dp))
        Canvas(modifier = Modifier.weight(1f).height(9.dp)) {
            val bh = size.height / 2f - 0.5f
            for ((i, db) in listOf(l, r).withIndex()) {
                val y = i * (bh + 1f)
                drawRect(Part, topLeft = Offset(0f, y), size = Size(size.width, bh))
                val f = barFrac(db)
                drawRect(
                    if (db > -0.3f) Warn else if (db > -6f) Lit else Fill,
                    topLeft = Offset(0f, y),
                    size = Size(size.width * f, bh)
                )
            }
        }
        Text(
            if (max(l, r) < -90f) "−∞" else String.format("%.1f", max(l, r)),
            color = Dim, fontSize = 9.sp,
            modifier = Modifier.width(34.dp).padding(start = 4.dp)
        )
    }
}

// ---- right half: chain and stage page -----------------------------------------------------------------------------

/** The chain I/O → SAT → SPEC → LIMIT → CLIP: tap a chip to open it; the dot shows whether the stage is on. */
@Composable
private fun ChainStrip(
    selected: String,
    values: Map<String, Float>,
    m: FloatArray,
    onSelect: (String) -> Unit
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        CHAIN.forEachIndexed { i, st ->
            if (i > 0) Text("›", color = Edge, fontSize = 14.sp, modifier = Modifier.padding(horizontal = 2.dp))
            val on = st.enableKey == null || (values[st.enableKey] ?: 1f) >= 0.5f
            val isSel = st.group == selected
            // live activity 0..1: saturation amount, GR / 6 dB
            val act = when (st.group) {
                "SAT" -> m[MasterMeter.SAT_AMOUNT]
                "SPECTRAL" -> -m[MasterMeter.SPC_GR] / 6f
                "LIMITER" -> -m[MasterMeter.LIM_GR] / 6f
                "CLIPPER" -> -m[MasterMeter.CLIP_GR] / 6f
                else -> 0f
            }.coerceIn(0f, 1f)
            Column(
                modifier = Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(7.dp))
                    .background(if (isSel) Color(0xFF3A3934) else Part)
                    .border(1.dp, if (isSel) Lit else Edge, RoundedCornerShape(7.dp))
                    .clickable { onSelect(st.group) }
                    .padding(horizontal = 6.dp, vertical = 5.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (st.enableKey != null) {
                        Box(Modifier.size(7.dp).clip(CircleShape).background(if (on) Green else Edge))
                        Spacer(Modifier.width(5.dp))
                    }
                    Text(
                        st.chip,
                        color = if (on) (if (isSel) Lit else Label) else Dim,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                }
                Spacer(Modifier.height(3.dp))
                Canvas(modifier = Modifier.fillMaxWidth().height(3.dp)) {
                    drawRoundRect(Tray, size = size, cornerRadius = CornerRadius(2f))
                    if (on && act > 0.005f) {
                        drawRoundRect(
                            if (st.group == "SAT") Fill else Warn,
                            size = Size(size.width * act, size.height),
                            cornerRadius = CornerRadius(2f)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun StagePage(
    stage: Stage,
    table: List<MasterParam>,
    values: Map<String, Float>,
    m: FloatArray,
    set: (String, Float, Boolean) -> Unit
) {
    val on = stage.enableKey == null || (values[stage.enableKey] ?: 1f) >= 0.5f
    Column(
        modifier = Modifier
            .fillMaxSize()
            .clip(RoundedCornerShape(10.dp))
            .background(CardBg)
            .border(1.dp, Color(0xFF2C2C2E), RoundedCornerShape(10.dp))
            .padding(horizontal = 10.dp, vertical = 8.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stage.title, color = if (on) Label else Dim, fontSize = 12.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            val status = when (stage.group) {
                "SAT" -> "${(m[MasterMeter.SAT_AMOUNT] * 100).toInt()} %"
                "SPECTRAL" -> String.format("GR %.1f dB", m[MasterMeter.SPC_GR])
                "LIMITER" -> String.format("GR %.1f dB", m[MasterMeter.LIM_GR])
                "CLIPPER" -> String.format("GR %.1f dB", m[MasterMeter.CLIP_GR])
                else -> ""
            }
            if (status.isNotEmpty()) Text(status, color = Dim, fontSize = 10.sp, modifier = Modifier.padding(end = 8.dp))
            if (stage.enableKey != null) {
                Pill(if (on) "ON" else "OFF", on, small = true) { set(stage.enableKey, if (on) 0f else 1f, true) }
            }
        }
        val params = table.filter { it.group == stage.group && it.key != stage.enableKey && it.key !in HEADER_KEYS }
        // two per row; the saturator's transfer curve takes the first cell
        val cells: List<(@Composable () -> Unit)> = buildList {
            if (stage.group == "SAT") add { SaturationCurve(values, m[MasterMeter.IN_RMS]) }
            for (p in params) add { ParamCell(p, values[p.key] ?: p.default, on, set) }
        }
        for (row in cells.chunked(2)) {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                for (cell in row) Box(Modifier.weight(1f)) { cell() }
                if (row.size == 1) Spacer(Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun ParamCell(p: MasterParam, value: Float, enabled: Boolean, set: (String, Float, Boolean) -> Unit) {
    val name = NAMES[p.key] ?: p.key
    if (p.isSwitch && p.key != "CLIP_POSITION") {
        Row(modifier = Modifier.height(38.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(name, color = if (enabled) Label else Dim, fontSize = 11.sp, modifier = Modifier.weight(1f))
            Pill(if (value >= 0.5f) "ON" else "OFF", value >= 0.5f, small = true) { set(p.key, if (value >= 0.5f) 0f else 1f, true) }
        }
        return
    }
    if (p.stepped && p.key != "LIM_LOOKAHEAD") {
        Column(modifier = Modifier.height(38.dp)) {
            Text(name, color = if (enabled) Label else Dim, fontSize = 11.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                val count = (p.max - p.min).toInt() + 1
                for (i in 0 until count) {
                    val v = p.min + i
                    Pill(formatValue(p, v), abs(value - v) < 0.5f, small = true) { set(p.key, v, true) }
                }
            }
        }
        return
    }
    MiniFader(p, name, value, enabled, set)
}

/**
 * A thin fader: name and value on one line, the track below. Drag anywhere (relative, so it never jumps), double
 * tap for the default. Bipolar ranges fill from zero.
 */
@Composable
private fun MiniFader(p: MasterParam, name: String, value: Float, enabled: Boolean, set: (String, Float, Boolean) -> Unit) {
    val current by rememberUpdatedState(value)
    val setNow by rememberUpdatedState(set)  // the gesture outlives recompositions: always call the latest
    Column(modifier = Modifier.height(38.dp)) {
        Row {
            Text(name, color = if (enabled) Label else Dim, fontSize = 11.sp, modifier = Modifier.weight(1f), maxLines = 1)
            Text(formatValue(p, value), color = Dim, fontSize = 10.sp, maxLines = 1)
        }
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(20.dp)
                .pointerInput(p.key) {
                    detectTapGestures(onDoubleTap = { setNow(p.key, p.default, true) })
                }
                .pointerInput(p.key) {
                    var v = current
                    detectHorizontalDragGestures(
                        onDragStart = { v = current },
                        onDragEnd = { setNow(p.key, v, true) },
                        onDragCancel = { setNow(p.key, v, true) }
                    ) { change, dx ->
                        change.consume()
                        v = (v + dx / size.width * (p.max - p.min)).coerceIn(p.min, p.max)
                        setNow(p.key, v, false)
                    }
                }
        ) {
            drawFader(p, value, enabled)
        }
    }
}

private fun DrawScope.drawFader(p: MasterParam, value: Float, enabled: Boolean) {
    val cy = size.height / 2f
    val th = 4.dp.toPx()
    drawRoundRect(Part, topLeft = Offset(0f, cy - th / 2f), size = Size(size.width, th), cornerRadius = CornerRadius(th / 2f))
    fun x(v: Float) = (v - p.min) / (p.max - p.min) * size.width
    val from = if (p.min < 0f && p.max > 0f) x(0f) else 0f
    val to = x(value)
    val col = if (enabled) Fill else Edge
    drawRoundRect(
        col,
        topLeft = Offset(minOf(from, to), cy - th / 2f),
        size = Size(abs(to - from), th),
        cornerRadius = CornerRadius(th / 2f)
    )
    val tw = 3.dp.toPx()
    val tH = 14.dp.toPx()
    drawRoundRect(
        if (enabled) Lit else Dim,
        topLeft = Offset((to - tw / 2f).coerceIn(0f, size.width - tw), cy - tH / 2f),
        size = Size(tw, tH),
        cornerRadius = CornerRadius(tw / 2f)
    )
}

/**
 * The saturator's static transfer curve for the current drive, shape and symmetry (with the level compensation),
 * and the live operating point at the input RMS.
 */
@Composable
private fun SaturationCurve(values: Map<String, Float>, inRmsDb: Float) {
    val drive = 10f.pow((values["SAT_DRIVE"] ?: 6f) / 20f)
    val shape = values["SAT_SHAPE"] ?: 0.6f
    val b = 0.3f * (values["SAT_SYMMETRY"] ?: 0f)
    fun curve(x: Float): Float {
        val hard = x.coerceIn(-1f, 1f)
        val th = tanh(x)
        val alg = x / sqrt(1f + x * x)
        return if (shape <= 0.5f) {
            val w = shape * 2f; (1 - w) * hard + w * th
        } else {
            val w = shape * 2f - 1f; (1 - w) * th + w * alg
        }
    }
    val comp = (curve(drive * 0.25f) / 0.25f).takeIf { it > 1e-6f } ?: 1f
    val fb = curve(b)
    Canvas(
        modifier = Modifier
            .fillMaxWidth()
            .height(38.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(Tray)
    ) {
        val w = size.width
        val h = size.height
        fun px(x: Float) = (x + 1.5f) / 3f * w
        fun py(y: Float) = h - (y + 1.5f) / 3f * h
        drawLine(Color(0xFF2C2C2E), Offset(0f, py(0f)), Offset(w, py(0f)))
        drawLine(Color(0xFF3A3A3C), Offset(px(-1.5f), py(-1.5f)), Offset(px(1.5f), py(1.5f)))
        val path = Path()
        for (i in 0..80) {
            val x = -1.5f + 3f * i / 80f
            val y = (curve(drive * x + b) - fb) / comp
            if (i == 0) path.moveTo(px(x), py(y)) else path.lineTo(px(x), py(y))
        }
        drawPath(path, Lit, style = Stroke(width = 1.5f))
        val xin = 10f.pow(inRmsDb / 20f) * sqrt(2f)
        if (xin > 1e-3f && xin < 1.5f) {
            val y = (curve(drive * xin + b) - fb) / comp
            drawCircle(Accent, radius = 3.5f, center = Offset(px(xin), py(y)))
        }
    }
}
