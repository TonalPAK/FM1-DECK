package io.github.fm1deck.ui.panel.input

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KnobDragTest {

    @Test
    fun verticalOnly60dpGivesNSteps() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Drag 60 dp vertically up (dy = -60 -> d = 60 dp)
        val stepsUp = drag.onMove(x = 100f, y = 40f, timeMs = 50L)
        assertEquals(10, stepsUp)

        // Drag 60 dp vertically down (dy = +60 -> d = -60 dp)
        val stepsDown = drag.onMove(x = 100f, y = 100f, timeMs = 100L)
        assertEquals(-10, stepsDown)
    }

    @Test
    fun horizontalOnly60dpGivesSameNSteps() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Drag 60 dp horizontally right (dx = 60 -> d = 60 dp)
        val stepsRight = drag.onMove(x = 160f, y = 100f, timeMs = 50L)
        assertEquals(10, stepsRight)

        // Drag 60 dp horizontally left (dx = -60 -> d = -60 dp)
        val stepsLeft = drag.onMove(x = 100f, y = 100f, timeMs = 100L)
        assertEquals(-10, stepsLeft)
    }

    @Test
    fun verticalAndHorizontal60dpAreEqual() {
        val dragVertical = KnobDrag(sensitivityDp = 6f, density = 1f)
        dragVertical.onDown(x = 100f, y = 100f, timeMs = 0L)
        val verticalSteps = dragVertical.onMove(x = 100f, y = 40f, timeMs = 50L)

        val dragHorizontal = KnobDrag(sensitivityDp = 6f, density = 1f)
        dragHorizontal.onDown(x = 100f, y = 100f, timeMs = 0L)
        val horizontalSteps = dragHorizontal.onMove(x = 160f, y = 100f, timeMs = 50L)

        assertEquals("Vertical and horizontal steps must be equal", verticalSteps, horizontalSteps)
        assertEquals(10, verticalSteps)
    }

    @Test
    fun diagonalUpLeftCancels() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Drag diagonally up-left (dy = -30, dx = -30 -> d = -(-30) + (-30) = 0)
        val steps = drag.onMove(x = 70f, y = 70f, timeMs = 50L)
        assertEquals(0, steps)
        assertEquals(0f, drag.accumulator, 0.0001f)

        // Drag diagonally down-right (dy = +30, dx = +30 -> d = -(30) + 30 = 0)
        val stepsOpposite = drag.onMove(x = 100f, y = 100f, timeMs = 100L)
        assertEquals(0, stepsOpposite)
        assertEquals(0f, drag.accumulator, 0.0001f)
    }

    @Test
    fun oneDpMovesX60GiveSameStepsAsSingle60dpMove() {
        // Test with non-multiple sensitivity (7 dp) to rigorously verify remainder handling
        val sensitivity = 7.0f

        val dragSingle = KnobDrag(sensitivityDp = sensitivity, density = 1f)
        dragSingle.onDown(x = 100f, y = 100f, timeMs = 0L)
        val singleSteps = dragSingle.onMove(x = 100f, y = 40f, timeMs = 50L) // 60 dp total

        val dragIncremental = KnobDrag(sensitivityDp = sensitivity, density = 1f)
        dragIncremental.onDown(x = 100f, y = 100f, timeMs = 0L)
        var incrementalSteps = 0
        for (i in 1..60) {
            incrementalSteps += dragIncremental.onMove(
                x = 100f,
                y = 100f - i.toFloat(),
                timeMs = 10L + i
            )
        }

        assertEquals(
            "60 individual 1 dp moves must produce identical steps as one 60 dp move",
            singleSteps,
            incrementalSteps
        )
        assertEquals(8, singleSteps)
        assertEquals(dragSingle.accumulator, dragIncremental.accumulator, 0.0001f)
        assertEquals(4.0f, dragIncremental.accumulator, 0.0001f) // 60 - 8*7 = 4 dp remainder
    }

    @Test
    fun fineModeDividesByFour() {
        val sensitivity = 6f
        val distance = 240f

        // Normal mode (rapid movement, started at 50 ms < 400 ms)
        val dragNormal = KnobDrag(sensitivityDp = sensitivity, density = 1f)
        dragNormal.onDown(x = 100f, y = 100f, timeMs = 0L)
        val normalSteps = dragNormal.onMove(x = 100f, y = 100f - distance, timeMs = 50L)
        assertFalse(dragNormal.isFineMode)
        assertEquals(40, normalSteps)

        // Fine mode (finger held still >= 400 ms at start, movement starts at 500 ms)
        val dragFine = KnobDrag(sensitivityDp = sensitivity, density = 1f)
        dragFine.onDown(x = 100f, y = 100f, timeMs = 0L)
        val fineSteps = dragFine.onMove(x = 100f, y = 100f - distance, timeMs = 500L)
        assertTrue(dragFine.isFineMode)
        assertEquals(10, fineSteps)

        assertEquals("Fine mode must divide steps by 4", normalSteps / 4, fineSteps)
    }

    @Test
    fun fineModeActivatesAfterHoldingStillWithStationaryEvents() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Stationary touch events (< slop threshold)
        drag.onMove(x = 100f, y = 100f, timeMs = 100L)
        drag.onMove(x = 100.1f, y = 100.1f, timeMs = 250L)
        drag.onMove(x = 100f, y = 100f, timeMs = 380L)
        assertFalse(drag.hasMoved)
        assertFalse(drag.isFineMode)

        // Drag begins after 400 ms
        val steps = drag.onMove(x = 100f, y = 40f, timeMs = 450L) // 60 dp move
        assertTrue(drag.hasMoved)
        assertTrue(drag.isFineMode)
        // 60 dp / (6 * 4) = 2 steps
        assertEquals(2, steps)
    }

    @Test
    fun earlyMovementPreventsFineModeEvenIfDragLastsLonger() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Movement starts early (at 50 ms)
        val earlySteps = drag.onMove(x = 100f, y = 90f, timeMs = 50L) // 10 dp move
        assertTrue(drag.hasMoved)
        assertFalse(drag.isFineMode)
        assertEquals(1, earlySteps) // 10 / 6 = 1 step

        // Drag continues after 500 ms; must remain in normal mode
        val laterSteps = drag.onMove(x = 100f, y = 30f, timeMs = 550L) // 60 dp move
        assertFalse("Fine mode must not activate if moved early", drag.isFineMode)
        assertEquals(10, laterSteps)
    }

    @Test
    fun releaseMidStepDoesNotSendPartialStep() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Move 3 dp (half step for 6 dp sensitivity)
        val moveSteps = drag.onMove(x = 100f, y = 97f, timeMs = 50L)
        assertEquals(0, moveSteps)
        assertEquals(3.0f, drag.accumulator, 0.0001f)

        // Release mid-step
        val upSteps = drag.onUp(timeMs = 100L)
        assertEquals("Release must not emit partial steps", 0, upSteps)
        assertFalse(drag.isDragging)
    }

    @Test
    fun capturePersistsBeyondNormalBounds() {
        val drag = KnobDrag(sensitivityDp = 6f, density = 1f)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Move pointer far across the screen
        val steps1 = drag.onMove(x = 500f, y = 100f, timeMs = 50L) // +400 dx -> +66 steps
        assertTrue(steps1 > 0)

        val steps2 = drag.onMove(x = 1000f, y = -200f, timeMs = 100L) // +500 dx, -300 dy -> +800 -> +133 steps
        assertTrue(steps2 > 0)
        assertTrue(drag.isDragging)

        drag.onUp(timeMs = 150L)
        assertFalse(drag.isDragging)
    }

    @Test
    fun densityScalesStepDistanceCorrectly() {
        val density = 2.5f
        val drag = KnobDrag(sensitivityDp = 6f, density = density)
        drag.onDown(x = 100f, y = 100f, timeMs = 0L)

        // Step px is 6 * 2.5 = 15 px
        // Move 60 px vertically up
        val steps = drag.onMove(x = 100f, y = 40f, timeMs = 50L)
        assertEquals(4, steps) // 60 / 15 = 4 steps
    }
}
