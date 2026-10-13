package io.github.fm1deck.ui.panel

import android.view.HapticFeedbackConstants
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
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
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.fm1deck.engine.LinkController
import io.github.fm1deck.engine.LinkStatus
import io.github.fm1deck.fw.FirmwareCatalog
import io.github.fm1deck.settings.AppSettings
import io.github.fm1deck.ui.panel.render.PanelStyle
import io.github.fm1deck.ui.panel.skin.Palette
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.roundToInt
import kotlin.math.sin

/** Load colours of the DSP meter: the label grey up to 70 %, amber to 90 %, then the REC red. */
private val AMBER = Color(0xFFD9A441)

private fun loadColor(palette: Palette, load: Float): Color = when {
    load >= 0.9f -> palette.rec
    load >= 0.7f -> AMBER
    else -> palette.buttonLabel
}

/** An output peak (linear) on a 48 dB scale, 0..1: what the level bars and the tremble show. */
private fun levelFraction(peak: Float): Float =
    if (peak <= 0.0001f) 0f else ((20f * log10(peak) + 48f) / 48f).coerceIn(0f, 1f)

/** The rack's marks on the panel canvas: the DSP meter under the settings dot and the drawer's tab. */
object RackChrome {

    /** A short bar under the settings dot: the callback's load, 0..100 % of its budget, with the recent peak. */
    fun drawDspMeter(scope: DrawScope, geometry: PanelGeometry, palette: Palette, load: Float, peak: Float) = with(scope) {
        val u = geometry.u
        val gear = geometry.gear
        val w = 11f * u
        val h = 1.6f * u
        val left = gear.center.x - gear.radius
        val top = gear.center.y + gear.radius + 2.4f * u
        val r = CornerRadius(h / 2f, h / 2f)
        drawRoundRect(palette.frame, Offset(left, top), Size(w, h), r)
        val fill = load.coerceIn(0f, 1f)
        if (fill > 0.005f) {
            drawRoundRect(loadColor(palette, load).copy(alpha = 0.85f), Offset(left, top), Size(maxOf(h, w * fill), h), r)
        }
        val p = peak.coerceIn(0f, 1f)
        if (p > 0.01f) {
            val x = left + w * p
            drawLine(loadColor(palette, peak), Offset(x, top - 0.6f * u), Offset(x, top + h + 0.6f * u), 0.8f * u)
        }
    }

    /**
     * The drawer's tab in the top-right corner (its touch square is the settings target's mirror, 28..40 dp): a
     * pill against the right edge with a triangle a fingertip can find, and one dot per slot under it (the focused
     * one bright, a filled one dim, an empty one a ring). Open: the pill lights and the triangle points back.
     */
    fun drawTab(
        scope: DrawScope,
        geometry: PanelGeometry,
        palette: Palette,
        rack: List<String>,
        focus: Int,
        open: Boolean
    ) = with(scope) {
        val tab = geometry.rackTab
        if (tab.width <= 0f) return@with
        val s = tab.width
        val left = tab.left + s * 0.14f
        val top = tab.top + s * 0.10f
        val bottom = tab.bottom - s * 0.10f
        val rad = s * 0.22f
        // The pill runs off the right edge: only its left corners show
        drawRoundRect(
            if (open) palette.ledLit else palette.button,
            Offset(left, top), Size(tab.right + rad - left, bottom - top), CornerRadius(rad, rad)
        )
        drawRoundRect(
            if (open) palette.ledLit else palette.frame,
            Offset(left, top), Size(tab.right + rad - left, bottom - top), CornerRadius(rad, rad),
            style = Stroke(width = maxOf(1f, s * 0.03f))
        )
        val ink = if (open) palette.background else palette.buttonLabel
        val cx = (left + tab.right) / 2f
        val cy = top + (bottom - top) * 0.40f
        val th = s * 0.30f
        val tw = th * 0.80f
        val tri = Path().apply {
            if (open) {
                moveTo(cx - tw / 2f, cy - th / 2f); lineTo(cx + tw / 2f, cy); lineTo(cx - tw / 2f, cy + th / 2f)
            } else {
                moveTo(cx + tw / 2f, cy - th / 2f); lineTo(cx - tw / 2f, cy); lineTo(cx + tw / 2f, cy + th / 2f)
            }
            close()
        }
        drawPath(tri, ink)
        val n = rack.size
        val dotR = s * 0.045f
        val pitch = s * 0.15f
        val dy = top + (bottom - top) * 0.80f
        for (i in 0 until n) {
            val c = Offset(cx + pitch * (i - (n - 1) / 2f), dy)
            when {
                i == focus -> drawCircle(ink, dotR, c)
                rack[i].isNotEmpty() -> drawCircle(ink.copy(alpha = 0.45f), dotR, c)
                else -> drawCircle(ink.copy(alpha = 0.45f), dotR * 0.85f, c, style = Stroke(width = dotR * 0.5f))
            }
        }
    }
}

/** What the drawer shows: the slots, or the firmware list for one slot. */
private sealed interface RackPage {
    object Slots : RackPage
    data class Pick(val slot: Int) : RackPage
}

/**
 * The rack's drawer: a column that slides in from the right edge under the tab, made for the thumb.
 * The DSP meter and the shared tempo (drag it up / down; tap: the shared PLAY / STOP), then one cell per slot with
 * its number, the firmware's three letters, the drop-down arrow (its firmware list), a level bar, its CPU share and
 * MUTE / SOLO; a playing cell trembles with its sound.
 * Tap a cell: the panel plays that slot (hold it, or tap its arrow: pick its firmware). Swipe a cell sideways: OFF.
 * The panel stays live around it; a tap on the panel's empty space, or on the tab, slides it back.
 */
@Composable
fun RackDrawer(
    open: Boolean,
    settings: AppSettings,
    palette: Palette,
    panelState: PanelState,
    topPx: Float,
    onFocus: (Int) -> Unit,
    onAssign: (slot: Int, fwId: String) -> Unit,
    onGate: (mute: Int, solo: Int) -> Unit,
    onTempo: (Double) -> Unit
) {
    val view = LocalView.current
    val density = LocalDensity.current
    var page by remember { mutableStateOf<RackPage>(RackPage.Slots) }
    LaunchedEffect(open) { if (!open) page = RackPage.Slots }

    // Tremble clock (frame time), read in the graphics layers only
    var tSec by remember { mutableFloatStateOf(0f) }
    // The shared clock, a few times a second
    var link by remember { mutableStateOf(LinkStatus()) }
    LaunchedEffect(open) {
        if (!open) return@LaunchedEffect
        launch { while (true) { link = runCatching { LinkController.status() }.getOrDefault(link); delay(120) } }
        while (true) withFrameNanos { tSec = (it / 1_000_000L % 100_000L) / 1000f }
    }

    fun haptic() {
        if (settings.hapticsMode != AppSettings.HAPTICS_OFF) view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
    }

    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopEnd) {
        AnimatedVisibility(
            visible = open,
            enter = slideInHorizontally(tween(170)) { it } + fadeIn(tween(170)),
            exit = slideOutHorizontally(tween(150)) { it } + fadeOut(tween(150)),
            modifier = Modifier.padding(top = with(density) { topPx.toDp() })
        ) {
            val shape = RoundedCornerShape(topStart = 12.dp, bottomStart = 12.dp)
            Column(
                Modifier
                    .width(168.dp)
                    .heightIn(max = 560.dp)
                    .clip(shape)
                    .background(palette.background)
                    .border(1.dp, palette.frame, shape)
                    // Taps between the cells stay in the drawer
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {}
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 7.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.weight(1f).padding(end = 7.dp)) { DspHeader(palette, panelState) }
                    Box(Modifier.width(64.dp)) {
                        TempoCell(
                            link = link,
                            shared = settings.rack.count { it.isNotEmpty() } > 1 || link.enabled,
                            palette = palette,
                            onTempo = { bpm -> LinkController.setTempo(bpm); link = link.copy(tempo = bpm) },
                            onTempoDone = { onTempo(link.tempo) },
                            onPlay = { haptic(); LinkController.togglePlay() }
                        )
                    }
                }
                when (val pg = page) {
                    RackPage.Slots -> {
                        val filled = settings.rack.count { it.isNotEmpty() }
                        for (i in settings.rack.indices) {
                            val fw = settings.rack[i]
                            val bit = 1 shl i
                            SlotCell(
                                slot = i,
                                fwId = fw,
                                focused = i == settings.rackFocus,
                                canClear = fw.isNotEmpty() && filled > 1,
                                muted = settings.rackMute and bit != 0,
                                soloed = settings.rackSolo and bit != 0,
                                palette = palette,
                                panelState = panelState,
                                tSec = { tSec },
                                onTap = {
                                    haptic()
                                    if (fw.isEmpty()) page = RackPage.Pick(i) else if (i != settings.rackFocus) onFocus(i)
                                },
                                onPick = { haptic(); page = RackPage.Pick(i) },
                                onClear = { haptic(); onAssign(i, "") },
                                onMute = { haptic(); onGate(settings.rackMute xor bit, settings.rackSolo) },
                                onSolo = { haptic(); onGate(settings.rackMute, settings.rackSolo xor bit) }
                            )
                        }
                    }
                    is RackPage.Pick -> FirmwarePicker(
                        slot = pg.slot,
                        settings = settings,
                        palette = palette,
                        onPick = { id ->
                            haptic()
                            onAssign(pg.slot, id)
                            page = RackPage.Slots
                        },
                        onBack = { page = RackPage.Slots }
                    )
                }
            }
        }
    }
}

/** The DSP meter, calmed again for the eye: the number moves at most about four times a second. */
@Composable
private fun DspHeader(palette: Palette, panelState: PanelState) {
    var shown by remember { mutableFloatStateOf(0f) }
    var peak by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(Unit) {
        while (true) {
            shown += 0.5f * (panelState.dspLoad - shown)
            peak = maxOf(panelState.dspPeak, peak * 0.9f)
            delay(250)
        }
    }
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.Bottom) {
            Text("DSP", color = palette.buttonLabelDim, fontSize = 9.sp, fontFamily = PanelStyle.dotFont)
            Text(
                "${(shown * 100f).roundToInt().coerceIn(0, 999)}%",
                color = loadColor(palette, shown),
                fontSize = 14.sp,
                fontFamily = PanelStyle.dotFont
            )
        }
        Spacer(Modifier.height(3.dp))
        Canvas(Modifier.fillMaxWidth().height(5.dp)) {
            val r = CornerRadius(size.height / 2f, size.height / 2f)
            drawRoundRect(palette.frame, cornerRadius = r)
            val f = shown.coerceIn(0f, 1f)
            if (f > 0.005f) drawRoundRect(loadColor(palette, shown), size = Size(maxOf(size.height, size.width * f), size.height), cornerRadius = r)
            val p = peak.coerceIn(0f, 1f)
            if (p > 0.01f) {
                val x = size.width * p
                drawLine(loadColor(palette, peak), Offset(x, 0f), Offset(x, size.height), 2.dp.toPx(), StrokeCap.Round)
            }
        }
    }
}

/** The shared tempo: drag up / down, 1 BPM per 8 dp; a tap starts or stops every slot together. */
@Composable
private fun TempoCell(
    link: LinkStatus,
    shared: Boolean,
    palette: Palette,
    onTempo: (Double) -> Unit,
    onTempoDone: () -> Unit,
    onPlay: () -> Unit
) {
    val density = LocalDensity.current
    val linkNow by rememberUpdatedState(link)
    val shape = RoundedCornerShape(8.dp)
    Column(
        Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(if (link.playing && shared) palette.frame else palette.button)
            .pointerInput(shared) {
                if (!shared) return@pointerInput
                var acc = 0f
                detectVerticalDragGestures(
                    onDragStart = { acc = 0f },
                    onDragEnd = { onTempoDone() },
                    onVerticalDrag = { change, dy ->
                        change.consume()
                        acc -= dy
                        val step = with(density) { 8.dp.toPx() }
                        val n = (acc / step).toInt()
                        if (n != 0) {
                            acc -= n * step
                            onTempo((linkNow.tempo.roundToInt() + n).toDouble().coerceIn(20.0, 300.0))
                        }
                    }
                )
            }
            .pointerInput(shared) { if (shared) detectTapGestures(onTap = { onPlay() }) }
            .padding(vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            if (!shared) "BPM" else if (link.playing) "▶ BPM" else "■ BPM",
            color = palette.buttonLabelDim, fontSize = 9.sp, fontFamily = PanelStyle.dotFont
        )
        Text(
            if (shared) "${link.tempo.roundToInt()}" else "—",
            color = if (shared) palette.buttonLabel else palette.buttonLabelDim,
            fontSize = 18.sp,
            fontFamily = PanelStyle.dotFont
        )
    }
}

@Composable
private fun SlotCell(
    slot: Int,
    fwId: String,
    focused: Boolean,
    canClear: Boolean,
    muted: Boolean,
    soloed: Boolean,
    palette: Palette,
    panelState: PanelState,
    tSec: () -> Float,
    onTap: () -> Unit,
    onPick: () -> Unit,
    onClear: () -> Unit,
    onMute: () -> Unit,
    onSolo: () -> Unit
) {
    val entry = FirmwareCatalog.findById(fwId)
    val shape = RoundedCornerShape(10.dp)
    val scope = rememberCoroutineScope()
    val swipe = remember(fwId) { Animatable(0f) }
    var widthPx by remember { mutableFloatStateOf(1f) }
    val clearNow by rememberUpdatedState(onClear)
    val swipeFrac = (abs(swipe.value) / (widthPx * 0.40f)).coerceIn(0f, 1f)

    Box(Modifier.fillMaxWidth().onSizeChanged { widthPx = it.width.toFloat().coerceAtLeast(1f) }) {
        // Behind the cell while it is swiped: OFF, lit once a release would switch the slot off
        if (swipe.value != 0f) {
            Box(
                Modifier.matchParentSize().clip(shape).background(if (swipeFrac >= 1f) palette.rec.copy(alpha = 0.35f) else palette.frame),
                contentAlignment = Alignment.Center
            ) {
                Text("OFF", color = if (swipeFrac >= 1f) palette.buttonLabel else palette.buttonLabelDim, fontSize = 15.sp, fontFamily = PanelStyle.dotFont)
            }
        }
        Column(
            Modifier
                .fillMaxWidth()
                .graphicsLayer {
                    // The tremble: a few px sideways with the slot's own level (dB), so the playing slots show
                    val level = levelFraction(panelState.slotLevels.getOrElse(slot) { 0f })
                    translationX = swipe.value + sin(tSec() * (57f + 9f * slot)) * level * level * 1.6f * density
                    alpha = 1f - 0.5f * swipeFrac
                }
                .clip(shape)
                .background(if (focused) palette.frame else palette.button)
                .border(
                    if (focused) 1.5.dp else 1.dp,
                    if (focused) palette.buttonLabel.copy(alpha = 0.6f) else palette.frame,
                    shape
                )
                .pointerInput(fwId) { detectTapGestures(onTap = { onTap() }, onLongPress = { onPick() }) }
                .pointerInput(fwId, canClear) {
                    if (!canClear) return@pointerInput
                    detectHorizontalDragGestures(
                        onDragEnd = {
                            scope.launch {
                                if (abs(swipe.value) >= widthPx * 0.40f) {
                                    swipe.animateTo(if (swipe.value > 0) widthPx else -widthPx, tween(90))
                                    clearNow()
                                } else {
                                    swipe.animateTo(0f, tween(140))
                                }
                            }
                        },
                        onDragCancel = { scope.launch { swipe.animateTo(0f, tween(140)) } },
                        onHorizontalDrag = { change, dx ->
                            change.consume()
                            scope.launch { swipe.snapTo(swipe.value + dx) }
                        }
                    )
                }
                .padding(start = 9.dp, end = 5.dp, top = 6.dp, bottom = 6.dp)
        ) {
            // The number, the firmware's three letters, and the drop-down arrow: its firmware list
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "${slot + 1}",
                    color = if (focused) palette.buttonLabel else palette.buttonLabelDim,
                    fontSize = 22.sp,
                    fontFamily = PanelStyle.dotFont
                )
                Spacer(Modifier.width(10.dp))
                Text(
                    entry?.shortName ?: "EMPTY",
                    color = if (entry == null) palette.buttonLabelDim else palette.buttonLabel.copy(alpha = if (focused) 1f else 0.75f),
                    fontSize = if (entry == null) 13.sp else 17.sp,
                    fontFamily = PanelStyle.dotFont,
                    maxLines = 1,
                    modifier = Modifier.weight(1f)
                )
                DropArrow(palette, onPick)
            }
            if (entry != null) {
                Spacer(Modifier.height(5.dp))
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Canvas(Modifier.fillMaxWidth().height(5.dp)) {
                            val level = levelFraction(panelState.slotLevels.getOrElse(slot) { 0f })
                            val r = CornerRadius(size.height / 2f, size.height / 2f)
                            drawRoundRect(palette.background, cornerRadius = r)
                            if (level > 0.003f) {
                                drawRoundRect(
                                    palette.buttonLabel.copy(alpha = if (muted) 0.25f else if (focused) 0.9f else 0.6f),
                                    size = Size(maxOf(size.height, size.width * level), size.height),
                                    cornerRadius = r
                                )
                            }
                        }
                        Spacer(Modifier.height(3.dp))
                        val load = panelState.slotLoads.getOrElse(slot) { 0f }
                        Text(
                            "CPU ${(load * 100f).roundToInt()}%",
                            color = if (load >= 0.7f) loadColor(palette, load) else palette.buttonLabelDim,
                            fontSize = 9.sp,
                            fontFamily = PanelStyle.dotFont
                        )
                    }
                    Spacer(Modifier.width(6.dp))
                    GateButton("M", muted, AMBER, palette, onMute)
                    Spacer(Modifier.width(4.dp))
                    GateButton("S", soloed, palette.ledLit, palette, onSolo)
                }
            }
        }
    }
}

/** The slot's drop-down: a big, plain arrow on its own plate, a fingertip wide. */
@Composable
private fun DropArrow(palette: Palette, onClick: () -> Unit) {
    Box(
        Modifier
            .width(40.dp)
            .height(32.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(palette.background)
            .pointerInput(Unit) { detectTapGestures(onTap = { onClick() }) },
        contentAlignment = Alignment.Center
    ) {
        Canvas(Modifier.width(16.dp).height(10.dp)) {
            val path = Path().apply {
                moveTo(0f, 0f); lineTo(size.width, 0f); lineTo(size.width / 2f, size.height); close()
            }
            drawPath(path, palette.buttonLabel)
        }
    }
}

/** MUTE / SOLO: a small key, lit in its colour while on. */
@Composable
private fun GateButton(label: String, on: Boolean, lit: Color, palette: Palette, onClick: () -> Unit) {
    Box(
        Modifier
            .width(32.dp)
            .height(28.dp)
            .clip(RoundedCornerShape(7.dp))
            .background(if (on) lit else palette.background)
            .pointerInput(Unit) { detectTapGestures(onTap = { onClick() }) },
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = if (on) palette.background else palette.buttonLabelDim, fontSize = 13.sp, fontFamily = PanelStyle.dotFont)
    }
}

@Composable
private fun FirmwarePicker(
    slot: Int,
    settings: AppSettings,
    palette: Palette,
    onPick: (String) -> Unit,
    onBack: () -> Unit
) {
    val current = settings.rack.getOrElse(slot) { "" }
    val filled = settings.rack.count { it.isNotEmpty() }
    Text("SLOT ${slot + 1}", color = palette.buttonLabelDim, fontSize = 10.sp, fontFamily = PanelStyle.dotFont)
    for (fw in FirmwareCatalog.builtins) {
        val elsewhere = settings.rack.indexOf(fw.id).takeIf { it >= 0 && it != slot }
        PickRow(
            label = fw.displayName.uppercase(),
            sub = if (elsewhere != null) "IN SLOT ${elsewhere + 1}" else null,
            selected = fw.id == current,
            palette = palette,
            onClick = { if (fw.id != current) onPick(fw.id) else onBack() }
        )
    }
    // A slot can be switched off while another one still plays
    if (current.isNotEmpty() && filled > 1) {
        PickRow(label = "OFF", sub = "SLOT EMPTY", selected = false, palette = palette, onClick = { onPick("") })
    }
    PickRow(label = "‹ BACK", sub = null, selected = false, palette = palette, onClick = onBack)
}

@Composable
private fun PickRow(label: String, sub: String?, selected: Boolean, palette: Palette, onClick: () -> Unit) {
    val shape = RoundedCornerShape(8.dp)
    Column(
        Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(if (selected) palette.frame else palette.button)
            .border(if (selected) 1.5.dp else 0.dp, if (selected) palette.buttonLabel.copy(alpha = 0.6f) else Color.Transparent, shape)
            .clickable(onClick = onClick)
            .padding(horizontal = 7.dp, vertical = 7.dp)
    ) {
        Text(label, color = palette.buttonLabel, fontSize = 15.sp, fontFamily = PanelStyle.dotFont, maxLines = 1)
        if (sub != null) Text(sub, color = palette.buttonLabelDim, fontSize = 8.sp, fontFamily = PanelStyle.dotFont, maxLines = 1)
    }
}
