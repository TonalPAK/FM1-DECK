package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.font.FontWeight
import io.github.fm1deck.ui.panel.BlockGeometry
import io.github.fm1deck.ui.panel.PanelElement
import io.github.fm1deck.ui.panel.PanelState
import io.github.fm1deck.ui.panel.Rect
import io.github.fm1deck.ui.panel.skin.Palette

/**
 * MASTER / SELECT / PRESETS / ALGORITHM straight on the panel body, OCT- / OCT+ on their own dark island
 * (the emulator's layout of this block).
 */
object SystemBlockRenderer {

    fun draw(
        scope: DrawScope,
        block: BlockGeometry,
        masterPot: PanelElement.MasterPot,
        systemKnobs: List<PanelElement.Knob>,
        octButtons: List<PanelElement.Button>,
        octTray: Rect,
        hasFrame: Boolean,
        palette: Palette,
        panelState: PanelState,
        masterAdc: Int,
        heldButtonsMask: Int,
        u: Float,
        textMeasurer: TextMeasurer,
        fnRect: Rect = Rect(0f, 0f, 0f, 0f),
        fnOn: Boolean = false
    ) {
        if (hasFrame) PanelStyle.drawFrame(scope, block.bounds, u, palette)
        val now = System.nanoTime()

        // MASTER: a pot, its line always shown, -135..+135 degrees
        val masterWell = masterPot.knobRadius
        val masterTouch = panelState.knobTouchAlpha(MASTER_ROLE, now)
        PanelStyle.drawKnob(
            scope = scope,
            center = masterPot.knobCenter.toOffset(),
            wellRadius = masterWell,
            palette = palette,
            touchAlpha = masterTouch,
            pulsing = false,
            angleDeg = -135f + 270f * masterAdc.coerceIn(0, 1023) / 1023f,
            indicatorAlpha = 1f
        )
        drawKnobLabel(scope, textMeasurer, "MASTER", masterPot.knobCenter.x, masterPot.knobCenter.y, masterWell, palette, u)

        // SELECT, PRESETS, ALGORITHM: endless encoders, the line shows only while touched
        for (i in systemKnobs.indices) {
            val k = systemKnobs[i]
            PanelStyle.drawKnob(
                scope = scope,
                center = k.knobCenter.toOffset(),
                wellRadius = k.knobRadius,
                palette = palette,
                touchAlpha = panelState.knobTouchAlpha(k.role, now),
                pulsing = panelState.isKnobPulsing(k.role, now),
                angleDeg = panelState.knobAngleDeg(k.role),
                indicatorAlpha = 0f
            )
            drawKnobLabel(scope, textMeasurer, k.defaultLabel, k.knobCenter.x, k.knobCenter.y, k.knobRadius, palette, u)
        }

        // F (latch): a mini button in the style of the panel's, lit while latching
        if (fnRect.width > 0f) {
            PanelStyle.drawButton(
                scope, textMeasurer, fnRect, "F", null,
                if (fnOn) palette.ledLit else palette.ledDim,
                if (fnOn) palette.panel else palette.buttonLabel, false, palette, u
            )
        }

        // OCT island
        PanelStyle.drawTray(scope, octTray, minOf(9f * u, octTray.height * 0.3f), palette)
        for (i in octButtons.indices) {
            val b = octButtons[i]
            val held = (heldButtonsMask and (1 shl b.index)) != 0
            val lit = panelState.isButtonLit(b.index)
            val face = when {
                lit -> palette.ledLit
                panelState.isButtonBreathing(b.index) -> lerpColor(palette.ledDim, palette.ledBreath, panelState.breathLevel)
                panelState.isButtonDim(b.index) -> palette.ledDim
                else -> palette.ledDim // OCT is always available: the emulator shows it dim
            }
            PanelStyle.drawButton(
                scope, textMeasurer, b.rect, b.label, null, face,
                if (lit) palette.panel else palette.buttonLabel, held, palette, u
            )
        }
    }

    internal fun drawKnobLabel(
        scope: DrawScope,
        textMeasurer: TextMeasurer,
        text: String,
        cx: Float,
        cy: Float,
        wellRadius: Float,
        palette: Palette,
        u: Float
    ) {
        val px = PanelStyle.knobLabelPx(scope, wellRadius)
        val style = PanelStyle.textStyle(scope, PanelStyle.dotFont, px, palette.buttonLabel, FontWeight.Normal)
        PanelStyle.drawLabelAbove(scope, textMeasurer, text, cx, cy - wellRadius - 1.5f * u, style)
    }
}
