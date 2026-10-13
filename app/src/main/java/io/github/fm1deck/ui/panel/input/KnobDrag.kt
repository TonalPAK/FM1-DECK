package io.github.fm1deck.ui.panel.input

import kotlin.math.abs

/**
 * Pure Kotlin knob drag gesture processor (no Android framework dependencies).
 *
 * Implements FM1 DECK knob physics:
 * - Both-axes projected distance: d = (-dy) + dx (up and right are clockwise).
 *   A finger at a screen edge continues turning the knob by moving sideways.
 * - Fractional remainder is preserved across move events without precision loss.
 * - Sensitivity in dp per step (scaled by density).
 * - Fine mode: 4x distance per step (1/4 rotary steps) when held still >= 400 ms at the start.
 * - Integer steps only; release mid-step emits no partial step.
 * - Pointer remains captured until released (onUp/onCancel).
 */
class KnobDrag(
    val sensitivityDp: Float = DEFAULT_SENSITIVITY_DP,
    val density: Float = 1.0f,
    val fineModeEnabled: Boolean = true,
    val fineThresholdMs: Long = DEFAULT_FINE_THRESHOLD_MS
) {
    companion object {
        const val DEFAULT_SENSITIVITY_DP = 6.0f
        const val DEFAULT_FINE_THRESHOLD_MS = 400L
        const val FINE_MULTIPLIER = 4.0f
        const val MOVEMENT_SLOP_PX = 0.5f
    }

    var downX: Float = 0f
        private set
    var downY: Float = 0f
        private set
    var downTimeMs: Long = 0L
        private set

    var lastX: Float = 0f
        private set
    var lastY: Float = 0f
        private set

    /** Cumulative distance remainder in pixels. */
    var accumulator: Float = 0f
        private set

    /** True if fine mode is currently active (divides steps by 4). */
    var isFineMode: Boolean = false
        private set

    /** True once the pointer has moved beyond the initial stationary threshold. */
    var hasMoved: Boolean = false
        private set

    /** True while a drag gesture is in progress (between onDown and onUp/onCancel). */
    var isDragging: Boolean = false
        private set

    /**
     * Initializes a knob drag gesture on touch down.
     */
    fun onDown(x: Float, y: Float, timeMs: Long = 0L) {
        downX = x
        downY = y
        downTimeMs = timeMs
        lastX = x
        lastY = y
        accumulator = 0f
        isFineMode = false
        hasMoved = false
        isDragging = true
    }

    /**
     * Processes pointer movement.
     *
     * @return Number of discrete rotary steps to emit (+ clockwise, - counter-clockwise).
     */
    fun onMove(x: Float, y: Float, timeMs: Long = 0L): Int {
        if (!isDragging) return 0

        val dx = x - lastX
        val dy = y - lastY
        lastX = x
        lastY = y

        if (!hasMoved) {
            val totalDx = x - downX
            val totalDy = y - downY
            val totalDist = abs(totalDx) + abs(totalDy)
            if (totalDist > MOVEMENT_SLOP_PX) {
                hasMoved = true
                if (fineModeEnabled && (timeMs - downTimeMs >= fineThresholdMs)) {
                    isFineMode = true
                }
            }
        }

        // Projected distance: up (-dy > 0) is clockwise, right (dx > 0) is clockwise
        val deltaD = -dy + dx
        accumulator += deltaD

        val stepPx = sensitivityDp * density * (if (isFineMode) FINE_MULTIPLIER else 1.0f)
        if (stepPx <= 0f) return 0

        val steps = (accumulator / stepPx).toInt()
        if (steps != 0) {
            accumulator -= steps * stepPx
        }
        return steps
    }

    /**
     * Finishes the gesture on pointer release.
     *
     * Per FM1 DECK spec, releasing mid-step discards the fractional remainder
     * and never emits a partial step.
     *
     * @return 0 (no partial steps emitted).
     */
    fun onUp(timeMs: Long = 0L): Int {
        isDragging = false
        return 0
    }

    /**
     * Cancels the gesture.
     */
    fun onCancel() {
        isDragging = false
    }

    /**
     * Programmatically overrides fine mode state (for tests and special interactions).
     */
    fun enableFineMode() {
        isFineMode = true
    }

    /**
     * Programmatically disables fine mode state.
     */
    fun disableFineMode() {
        isFineMode = false
    }
}
