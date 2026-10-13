package io.github.fm1deck.ui.panel

import android.view.HapticFeedbackConstants
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.sp
import io.github.fm1deck.engine.LinkController
import io.github.fm1deck.engine.NativeBridge
import io.github.fm1deck.settings.AppSettings
import io.github.fm1deck.ui.panel.input.KnobDrag
import io.github.fm1deck.ui.panel.render.ButtonsBlockRenderer
import io.github.fm1deck.ui.panel.render.KeyboardRenderer
import io.github.fm1deck.ui.panel.render.KnobsBlockRenderer
import io.github.fm1deck.ui.panel.render.MASTER_ROLE
import io.github.fm1deck.ui.panel.render.ScreenBlockRenderer
import io.github.fm1deck.ui.panel.render.SystemBlockRenderer
import io.github.fm1deck.ui.panel.render.toOffset
import io.github.fm1deck.ui.panel.render.toSize
import io.github.fm1deck.ui.panel.skin.BlockId
import io.github.fm1deck.ui.panel.skin.Palette
import kotlin.math.abs

private sealed interface ActiveTarget {
    data class Button(val index: Int) : ActiveTarget
    data class Key(var bit: Int) : ActiveTarget
    data class Knob(val role: Int, val drag: KnobDrag) : ActiveTarget
    data class MasterPot(var lastY: Float) : ActiveTarget
    data class Screen(val downTimeMs: Long) : ActiveTarget
    /** A finger whose press only toggled a latch (F on): it holds nothing itself. */
    object Latch : ActiveTarget
}

/**
 * Unified panel surface drawing the entire FM1 DECK canvas with multi-touch input.
 */
@Composable
fun PanelSurface(
    geometry: PanelGeometry,
    palette: Palette,
    settings: AppSettings,
    panelState: PanelState,
    onGearClick: () -> Unit,
    onMasterChange: (Int) -> Unit,
    modifier: Modifier = Modifier,
    onRackClick: () -> Unit = {},
    /** The rack drawer is out (the tab lights). */
    rackOpen: Boolean = false,
    /** A press on the panel's empty space (no control there): closes the rack drawer. */
    onEmptyTap: () -> Unit = {}
) {
    val view = LocalView.current
    val density = LocalDensity.current.density
    val textMeasurer = rememberTextMeasurer()
    val screenImageBitmap = remember(panelState.screenFrameCount) { panelState.screenBitmap.asImageBitmap() }

    // Multi-touch held masks
    var heldButtonsMask by remember { mutableIntStateOf(0) }
    var heldKeysMask by remember { mutableIntStateOf(0) }
    var masterAdc by remember { mutableIntStateOf(settings.masterAdc) }

    // F (latch): while on, a button press toggles that button held; F again releases them all.
    // For one-handed play: latch a layer button, then turn an encoder with the same thumb.
    var fnOn by remember { mutableStateOf(false) }
    var latchedMask by remember { mutableIntStateOf(0) }

    // Hidden debug overlay toggle (3-finger tap on Screen block)
    var showDebugOverlay by remember { mutableStateOf(false) }

    val settingsNow by rememberUpdatedState(settings)

    // Another rack slot took the panel: nothing stays held or latched, MASTER shows that slot's pot
    LaunchedEffect(settings.rackFocus, settings.activeFwId) {
        fnOn = false
        latchedMask = 0
        heldButtonsMask = 0
        heldKeysMask = 0
        masterAdc = settingsNow.masterAdc
        panelState.screenResync = true
    }
    val onMasterNow by rememberUpdatedState(onMasterChange)
    val onEmptyTapNow by rememberUpdatedState(onEmptyTap)
    val onRackClickNow by rememberUpdatedState(onRackClick)
    val systemKnobs = remember(geometry) { geometry.knobs.filter { it.role in 0..2 } }
    val octButtons = remember(geometry) { geometry.buttons.filter { it.index in 12..13 } }

    fun frameFor(block: BlockGeometry): Boolean = when (settings.framesOverride) {
        AppSettings.FRAMES_OVERRIDE_ON, AppSettings.FRAMES_OVERRIDE_ALL -> true
        AppSettings.FRAMES_OVERRIDE_OFF, AppSettings.FRAMES_OVERRIDE_NONE -> false
        else -> block.hasFrame
    }

    fun triggerHaptic() {
        if (settingsNow.hapticsMode != AppSettings.HAPTICS_OFF) {
            view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        }
    }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            // Keyed on the geometry only: a settings change (MASTER's own volume is one) must not restart the
            // gesture loop, or every finger on the panel is dropped mid-drag. Settings are read through setNow.
            .pointerInput(geometry) {
                awaitPointerEventScope {
                    val activePointers = mutableMapOf<PointerId, ActiveTarget>()
                    val screenTouchTimes = mutableListOf<Long>()

                    while (true) {
                        val event = awaitPointerEvent()

                        for (change in event.changes) {
                            val id = change.id
                            if (!change.previousPressed && change.pressed) {
                                val target = geometry.hitTest(change.position.x, change.position.y)
                                when (target) {
                                    is PanelTarget.Key -> {
                                        activePointers[id] = ActiveTarget.Key(target.bit)
                                        triggerHaptic()
                                    }
                                    is PanelTarget.Button -> {
                                        if (fnOn) {
                                            latchedMask = latchedMask xor (1 shl target.index)
                                            activePointers[id] = ActiveTarget.Latch
                                            triggerHaptic()
                                        } else {
                                            activePointers[id] = ActiveTarget.Button(target.index)
                                        }
                                    }
                                    is PanelTarget.Fn -> {
                                        fnOn = !fnOn
                                        if (!fnOn) latchedMask = 0
                                        activePointers[id] = ActiveTarget.Latch
                                        triggerHaptic()
                                    }
                                    is PanelTarget.Knob -> {
                                        val drag = KnobDrag(
                                            sensitivityDp = settingsNow.knobSensitivityDp,
                                            density = density,
                                            fineModeEnabled = settingsNow.fineModeOnLongPress
                                        )
                                        drag.onDown(
                                            x = change.position.x,
                                            y = change.position.y,
                                            timeMs = System.currentTimeMillis()
                                        )
                                        activePointers[id] = ActiveTarget.Knob(role = target.role, drag = drag)
                                        panelState.setKnobTouched(target.role)
                                    }
                                    is PanelTarget.MasterPot -> {
                                        activePointers[id] = ActiveTarget.MasterPot(lastY = change.position.y)
                                        panelState.setKnobTouched(MASTER_ROLE)
                                    }
                                    is PanelTarget.Gear -> {
                                        onGearClick()
                                    }
                                    is PanelTarget.RackTab -> {
                                        onRackClickNow()
                                        triggerHaptic()
                                    }
                                    is PanelTarget.Screen -> {
                                        val now = System.currentTimeMillis()
                                        screenTouchTimes.add(now)
                                        activePointers[id] = ActiveTarget.Screen(now)

                                        // Check for 3-finger simultaneous tap on screen block
                                        val screenPointers = activePointers.values.filterIsInstance<ActiveTarget.Screen>()
                                        if (screenPointers.size >= 3) {
                                            showDebugOverlay = !showDebugOverlay
                                            triggerHaptic()
                                        }
                                    }
                                    null -> onEmptyTapNow()
                                }
                                change.consume()
                            } else if (change.pressed) {
                                // Movement / drag
                                when (val active = activePointers[id]) {
                                    is ActiveTarget.Knob -> {
                                        val steps = active.drag.onMove(
                                            x = change.position.x,
                                            y = change.position.y,
                                            timeMs = System.currentTimeMillis()
                                        )
                                        if (steps != 0) {
                                            NativeBridge.nativeEnc(active.role, steps)
                                            panelState.updateKnobStep(active.role, steps)
                                        }
                                        change.consume()
                                    }
                                    is ActiveTarget.MasterPot -> {
                                        val dy = active.lastY - change.position.y
                                        val stepPx = 3f * density
                                        if (abs(dy) >= stepPx) {
                                            val deltaAdc = (dy / stepPx * 15f).toInt()
                                            val newAdc = (masterAdc + deltaAdc).coerceIn(0, 1023)
                                            if (newAdc != masterAdc) {
                                                masterAdc = newAdc
                                                NativeBridge.nativeMaster(newAdc)
                                                active.lastY = change.position.y
                                            }
                                        }
                                        change.consume()
                                    }
                                    is ActiveTarget.Key -> {
                                        if (settingsNow.glissando) {
                                            val newTarget = geometry.hitTest(change.position.x, change.position.y)
                                            if (newTarget is PanelTarget.Key && newTarget.bit != active.bit) {
                                                active.bit = newTarget.bit
                                                triggerHaptic()
                                            }
                                        }
                                        change.consume()
                                    }
                                    else -> change.consume()
                                }
                            } else if (!change.pressed) {
                                val active = activePointers.remove(id)
                                if (active is ActiveTarget.MasterPot) onMasterNow(masterAdc)  // saved once, on release
                                if (active is ActiveTarget.Knob) {
                                    active.drag.onUp(System.currentTimeMillis())
                                    val stillTouched = activePointers.values
                                        .filterIsInstance<ActiveTarget.Knob>()
                                        .any { it.role == active.role }
                                    if (!stillTouched) {
                                        panelState.setKnobReleased(active.role)
                                    }
                                }
                                change.consume()
                            }
                        }

                        // Recompute button and key masks
                        var newButtons = latchedMask
                        var newKeys = 0

                        for (active in activePointers.values) {
                            when (active) {
                                is ActiveTarget.Button -> newButtons = newButtons or (1 shl active.index)
                                is ActiveTarget.Key -> newKeys = newKeys or (1 shl active.bit)
                                else -> {}
                            }
                        }

                        // Sync any dropped/cancelled knob touches
                        val activeKnobRoles = activePointers.values.mapNotNullTo(HashSet()) {
                            when (it) {
                                is ActiveTarget.Knob -> it.role
                                is ActiveTarget.MasterPot -> MASTER_ROLE
                                else -> null
                            }
                        }
                        for (role in panelState.touchedKnobs - activeKnobRoles) {
                            panelState.setKnobReleased(role)
                        }

                        if (newButtons != heldButtonsMask || newKeys != heldKeysMask) {
                            heldButtonsMask = newButtons
                            heldKeysMask = newKeys
                            NativeBridge.nativePanel(heldButtonsMask, heldKeysMask)
                        }
                    }
                }
            }
    ) {
        val u = geometry.u

        // 1. Panel background
        drawRect(palette.background)

        // 2. Top area blocks
        val sysBlock = geometry.blocks[BlockId.SYSTEM]
        if (sysBlock != null) {
            SystemBlockRenderer.draw(
                scope = this,
                block = sysBlock,
                masterPot = geometry.masterPot,
                systemKnobs = systemKnobs,
                octButtons = octButtons,
                octTray = geometry.octTray,
                hasFrame = frameFor(sysBlock),
                palette = palette,
                panelState = panelState,
                masterAdc = masterAdc,
                heldButtonsMask = heldButtonsMask,
                u = u,
                textMeasurer = textMeasurer,
                fnRect = geometry.fnRect,
                fnOn = fnOn
            )
        }

        val btnBlock = geometry.blocks[BlockId.BUTTONS]
        if (btnBlock != null) {
            val hasFrame = frameFor(btnBlock)
            ButtonsBlockRenderer.draw(
                scope = this,
                block = btnBlock,
                buttons = geometry.buttons,
                hasFrame = hasFrame,
                palette = palette,
                panelState = panelState,
                heldButtonsMask = heldButtonsMask,
                u = u,
                textMeasurer = textMeasurer
            )
        }

        val knobsBlock = geometry.blocks[BlockId.KNOBS]
        if (knobsBlock != null) {
            val hasFrame = frameFor(knobsBlock)
            KnobsBlockRenderer.draw(
                scope = this,
                block = knobsBlock,
                knobs = geometry.knobs,
                hasFrame = hasFrame,
                palette = palette,
                panelState = panelState,
                u = u,
                textMeasurer = textMeasurer
            )
        }

        val screenBlock = geometry.blocks[BlockId.SCREEN]
        if (screenBlock != null) {
            val hasFrame = frameFor(screenBlock)
            // Read screenFrameCount to trigger draw when screen changes
            @Suppress("UNUSED_VARIABLE")
            val frameCount = panelState.screenFrameCount

            ScreenBlockRenderer.draw(
                scope = this,
                screen = geometry.screen,
                hasFrame = hasFrame,
                palette = palette,
                imageBitmap = screenImageBitmap,
                pixelated = settings.screenPixelated,
                u = u
            )
        }

        // 3. Separators: a hairline in the frame colour
        for (sep in geometry.separators) {
            drawLine(
                color = palette.frame,
                start = sep.start.toOffset(),
                end = sep.end.toOffset(),
                strokeWidth = 1f * u
            )
        }

        // 4. Keyboard
        KeyboardRenderer.draw(
            scope = this,
            keyboardRect = geometry.keyboardRect,
            keys = geometry.keys,
            palette = palette,
            panelState = panelState,
            heldKeysMask = heldKeysMask,
            u = u,
            textMeasurer = textMeasurer,
            hintDrawer = null, // Hook for P4
            beatMarks = settings.keyBeatMarks
        )

        // 5. Settings: a small dot in the top-left corner; green while Ableton Link is on. Under it the DSP meter.
        drawCircle(
            color = if (LinkController.enabled) Color(0xFF5FE08A) else palette.buttonLabel.copy(alpha = 0.85f),
            radius = geometry.gear.radius,
            center = geometry.gear.center.toOffset()
        )
        RackChrome.drawDspMeter(this, geometry, palette, panelState.dspLoad, panelState.dspPeak)
        RackChrome.drawTab(this, geometry, palette, settings.rack, settings.rackFocus, rackOpen)

        // 6. Hidden Debug Overlay (T10)
        if (showDebugOverlay) {
            val cpuMetrics = runCatching { NativeBridge.nativeCpuLoad() }.getOrNull()
            val p50 = cpuMetrics?.getOrNull(0) ?: 0f
            val p99 = cpuMetrics?.getOrNull(1) ?: 0f
            val maxUs = cpuMetrics?.getOrNull(2) ?: 0f
            val budget = cpuMetrics?.getOrNull(3) ?: 2000f
            val xruns = cpuMetrics?.getOrNull(5)?.toInt() ?: 0

            val overlayText = "CPU: p50=${p50.toInt()}µs p99=${p99.toInt()}µs max=${maxUs.toInt()}µs/${budget.toInt()}µs\nxruns=$xruns  frame=${panelState.screenFrameCount}"
            val layout = textMeasurer.measure(
                text = overlayText,
                style = TextStyle(
                    color = Color.Yellow,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold
                )
            )

            val overlayBg = geometry.topAreaRect
            drawRect(
                color = Color.Black.copy(alpha = 0.75f),
                topLeft = Offset(overlayBg.left + 8f * u, overlayBg.top + 8f * u),
                size = androidx.compose.ui.geometry.Size(
                    layout.size.width + 16f * u,
                    layout.size.height + 16f * u
                )
            )
            drawText(
                layout,
                topLeft = Offset(overlayBg.left + 16f * u, overlayBg.top + 16f * u)
            )
        }
    }
}
