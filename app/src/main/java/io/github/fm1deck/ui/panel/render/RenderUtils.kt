package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp
import io.github.fm1deck.ui.panel.PanelState
import io.github.fm1deck.ui.panel.Point
import io.github.fm1deck.ui.panel.Rect

fun Rect.toOffset(): Offset = Offset(left, top)
fun Rect.toSize(): Size = Size(width, height)
fun Point.toOffset(): Offset = Offset(x, y)

/**
 * Linearly interpolates between two Colors without allocation.
 */
fun lerpColor(start: Color, stop: Color, fraction: Float): Color {
    val f = fraction.coerceIn(0f, 1f)
    return Color(
        red = start.red + (stop.red - start.red) * f,
        green = start.green + (stop.green - start.green) * f,
        blue = start.blue + (stop.blue - start.blue) * f,
        alpha = start.alpha + (stop.alpha - start.alpha) * f
    )
}

/**
 * Computes font sizes in px from `u` with clamp (min/max), NOT sp * u.
 */
fun clampedFontSize(
    u: Float,
    baseUnits: Float,
    minSp: Float,
    maxSp: Float,
    density: Float = 1f
): TextUnit {
    val px = baseUnits * u
    val effectiveDensity = if (density > 0f) density else 1f
    val spValue = px / effectiveDensity
    return spValue.coerceIn(minSp, maxSp).sp
}

/**
 * Zero-allocation LRU cache for TextLayoutResult per (text, style) per MINOR-1.
 */
object TextLayoutCache {
    private data class CacheKey(val text: String, val style: TextStyle)
    private val cache = LinkedHashMap<CacheKey, TextLayoutResult>(64, 0.75f, true)
    private const val MAX_ENTRIES = 128

    fun getOrMeasure(
        textMeasurer: TextMeasurer,
        text: String,
        style: TextStyle
    ): TextLayoutResult {
        val key = CacheKey(text, style)
        var result = cache[key]
        if (result == null) {
            result = textMeasurer.measure(text, style)
            if (cache.size >= MAX_ENTRIES) {
                val oldest = cache.keys.iterator().next()
                cache.remove(oldest)
            }
            cache[key] = result
        }
        return result
    }

    fun clear() {
        cache.clear()
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Knob feedback: lit while touched, 150 ms fade after release, 60 ms pulse per step. Role 7 = MASTER.
// ---------------------------------------------------------------------------------------------------------------
const val MASTER_ROLE = 7
private const val FADE_NS = 150_000_000L
private const val PULSE_NS = 60_000_000L

fun PanelState.knobTouchAlpha(role: Int, nowNanos: Long): Float {
    if (isKnobTouched(role)) return 1f
    val released = knobReleaseNanos.getOrElse(role) { 0L }
    if (released == 0L) return 0f
    val dt = nowNanos - released
    return if (dt in 0L until FADE_NS) 1f - dt.toFloat() / FADE_NS else 0f
}

fun PanelState.isKnobPulsing(role: Int, nowNanos: Long): Boolean {
    val step = knobLastStepNanos.getOrElse(role) { 0L }
    return step != 0L && nowNanos - step in 0L until PULSE_NS
}

fun PanelState.knobAngleDeg(role: Int): Float = knobVisualAngle.getOrElse(role) { 0f }
