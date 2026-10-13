package io.github.fm1deck.ui.panel

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import io.github.fm1deck.engine.NativeBridge
import kotlin.math.PI
import kotlin.math.cos

/**
 * Display-rate poller for panel LEDs, breath phase, firmware screen bitmap, and UI state.
 * Uses withFrameNanos (choreographer tick) with minimal invalidations.
 */
@Composable
fun rememberPanelState(active: Boolean = true): PanelState {
    val state = remember { PanelState() }

    DisposableEffect(active) {
        if (active) {
            runCatching { NativeBridge.nativeSetScreenWanted(true) }
        }
        onDispose {
            runCatching { NativeBridge.nativeSetScreenWanted(false) }
        }
    }

    LaunchedEffect(active) {
        if (!active) return@LaunchedEffect

        val ledsBuffer = IntArray(8)
        var lastScreenFrame = 0
        val uiStateInts = IntArray(16)
        val uiStateLabels = Array<String?>(4) { null }

        while (true) {
            withFrameNanos { frameNanos ->
                // 1. LEDs polling
                runCatching { NativeBridge.nativeLeds(ledsBuffer) }
                if (!ledsBuffer.contentEquals(state.leds)) {
                    state.leds = ledsBuffer.clone()
                }

                // 2. Breath modulation: the firmware LED breath period, 1.13 s (hal/fm1_input.h, as the browser emulator)
                val tSec = frameNanos / 1_000_000_000.0
                val breath = (0.5 - 0.5 * cos(2.0 * PI * tSec / 1.13)).toFloat()
                state.breathLevel = breath

                // 3. Screen bitmap blit
                val newFrame = runCatching {
                    NativeBridge.nativeScreenInto(state.screenBitmap, lastScreenFrame)
                }.getOrDefault(lastScreenFrame)

                if (newFrame != lastScreenFrame) {
                    lastScreenFrame = newFrame
                    state.screenFrameCount = newFrame
                }

                // 4. UI State polling (if firmware supports it)
                val hasUi = runCatching {
                    NativeBridge.nativeUiState(uiStateInts, uiStateLabels)
                }.getOrDefault(false)

                if (hasUi) {
                    val layer = uiStateInts[0]
                    val page = uiStateInts[1]
                    val labels = uiStateLabels.map { it ?: "" }
                    state.fwUiState = FwUiState(
                        layer = layer,
                        page = page,
                        pageTitle = "",
                        knobLabels = labels
                    )
                } else if (state.fwUiState != null) {
                    state.fwUiState = null
                }
            }
        }
    }

    return state
}
