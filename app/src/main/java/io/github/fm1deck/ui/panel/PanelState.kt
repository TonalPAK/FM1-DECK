package io.github.fm1deck.ui.panel

import android.graphics.Bitmap
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Firmware UI state snapshot (FM1_CAP_UI_STATE).
 */
data class FwUiState(
    val layer: Int = 0,
    val page: Int = 0,
    val pageTitle: String = "",
    val knobLabels: List<String> = emptyList(),
    val knobValues: IntArray = IntArray(0)
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is FwUiState) return false
        return layer == other.layer &&
                page == other.page &&
                pageTitle == other.pageTitle &&
                knobLabels == other.knobLabels &&
                knobValues.contentEquals(other.knobValues)
    }

    override fun hashCode(): Int {
        var result = layer
        result = 31 * result + page
        result = 31 * result + pageTitle.hashCode()
        result = 31 * result + knobLabels.hashCode()
        result = 31 * result + knobValues.contentHashCode()
        return result
    }
}

/**
 * Snapshot of live firmware state for panel rendering.
 * Minimal invalidation: separate state variables for LEDs, breath, screen frame, and UI state.
 */
class PanelState {
    // 8 LED cells from nativeLeds (bitmasks for buttons and keys)
    var leds by mutableStateOf(IntArray(8))

    // 0.7 Hz sinusoidal breath modulation (0.0 .. 1.0)
    var breathLevel by mutableFloatStateOf(0f)

    // 240x240 ARGB_8888 bitmap for the LCD display (matches JniFirmware AndroidBitmap_lockPixels)
    val screenBitmap: Bitmap = Bitmap.createBitmap(240, 240, Bitmap.Config.ARGB_8888)

    // Incremented whenever nativeScreenInto writes a new frame
    var screenFrameCount by mutableIntStateOf(0)

    // Current firmware UI state (layer, page, knob labels)
    var fwUiState by mutableStateOf<FwUiState?>(null)

    // The rack (AudioEngine): the callback's live load and recent peak (shares of the budget), per slot the load
    // and the output peak, all from nativeRackStatus
    var dspLoad by mutableFloatStateOf(0f)
    var dspPeak by mutableFloatStateOf(0f)
    var slotLoads by mutableStateOf(FloatArray(0))
    var slotLevels by mutableStateOf(FloatArray(0))

    /** Set when the panel moves to another rack slot: the next poll takes that slot's screen whatever its frame. */
    @Volatile
    var screenResync = false

    // Button label overrides from firmware (e.g. SLOOP has SEL instead of SCL)
    var buttonNames by mutableStateOf(SkinLayout.ROW0_BUTTON_LABELS + SkinLayout.ROW1_BUTTON_LABELS)

    // Active knob activity tracking (role -> timestamp) for outer ring glow
    var activeKnobRole by mutableStateOf<Int?>(null)

    // Bitmask of which knob roles are currently touched (bit 1L shl role)
    var touchedKnobsMask by mutableLongStateOf(0L)

    // Set of knob roles currently touched
    var touchedKnobs by mutableStateOf<Set<Int>>(emptySet())

    // Timestamp (System.nanoTime) of last rotary step sent per knob (for 60 ms pulse indicator)
    var knobLastStepNanos by mutableStateOf(LongArray(8))

    // Visual angle in degrees per knob (+15 deg per step sent)
    var knobVisualAngle by mutableStateOf(FloatArray(8))

    // Alias for knobVisualAngle matching P2.1 specification
    val knobAngle get() = knobVisualAngle

    // Timestamp (System.nanoTime) of release per knob (for 150 ms glow fade-out)
    var knobReleaseNanos by mutableStateOf(LongArray(8))

    fun isKnobTouched(role: Int): Boolean = touchedKnobs.contains(role)

    fun setKnobTouched(role: Int) {
        touchedKnobs = touchedKnobs + role
        touchedKnobsMask = touchedKnobsMask or (1L shl role)
        activeKnobRole = role
    }

    fun setKnobReleased(role: Int, nowNanos: Long = System.nanoTime()) {
        touchedKnobs = touchedKnobs - role
        touchedKnobsMask = touchedKnobsMask and (1L shl role).inv()
        val updatedRelease = knobReleaseNanos.clone()
        if (role in updatedRelease.indices) {
            updatedRelease[role] = nowNanos
        }
        knobReleaseNanos = updatedRelease
        if (activeKnobRole == role) {
            activeKnobRole = touchedKnobs.firstOrNull()
        }
    }

    fun updateKnobStep(role: Int, steps: Int, nowNanos: Long = System.nanoTime()) {
        val updatedAngle = knobVisualAngle.clone()
        if (role in updatedAngle.indices) {
            updatedAngle[role] = updatedAngle[role] + steps * 15f
        }
        knobVisualAngle = updatedAngle

        val updatedLastStep = knobLastStepNanos.clone()
        if (role in updatedLastStep.indices) {
            updatedLastStep[role] = nowNanos
        }
        knobLastStepNanos = updatedLastStep
    }

    fun isButtonLit(b: Int): Boolean = (leds.getOrElse(0) { 0 } ushr b) and 1 != 0
    fun isButtonPlayGreen(): Boolean = (leds.getOrElse(0) { 0 } ushr 14) and 1 != 0
    fun isButtonDim(b: Int): Boolean = (leds.getOrElse(2) { 0 } ushr b) and 1 != 0
    fun isButtonBreathing(b: Int): Boolean = (leds.getOrElse(4) { 0 } ushr b) and 1 != 0

    fun isKeyLit(k: Int): Boolean = (leds.getOrElse(1) { 0 } ushr k) and 1 != 0
    fun isKeyDim(k: Int): Boolean = (leds.getOrElse(3) { 0 } ushr k) and 1 != 0
    fun isKeyBreathing(k: Int): Boolean = (leds.getOrElse(5) { 0 } ushr k) and 1 != 0
    fun isKeyMid(k: Int): Boolean = (leds.getOrElse(6) { 0 } ushr k) and 1 != 0
}
