package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.text.TextMeasurer
import io.github.fm1deck.ui.panel.BlockGeometry
import io.github.fm1deck.ui.panel.PanelElement
import io.github.fm1deck.ui.panel.PanelState
import io.github.fm1deck.ui.panel.skin.Palette

/**
 * The twelve buttons in their dark tray (with grooves between them). States as the emulator's drawLights():
 * lit = cream face and dark ink, PLAY green, dim = warm grey face, breath = dim .. ~80 % of lit, held = cream outline.
 */
object ButtonsBlockRenderer {

    fun draw(
        scope: DrawScope,
        block: BlockGeometry,
        buttons: List<PanelElement.Button>,
        hasFrame: Boolean,
        palette: Palette,
        panelState: PanelState,
        heldButtonsMask: Int,
        u: Float,
        textMeasurer: TextMeasurer
    ) {
        if (hasFrame) PanelStyle.drawFrame(scope, block.bounds, u, palette)
        PanelStyle.drawTray(scope, block.bounds, minOf(10f * u, block.bounds.height * 0.12f), palette)

        val breathPeak = if ((panelState.leds.getOrElse(7) { 0 }) != 0) 0.7f else 1f // DIM LO: the lower peak
        for (i in buttons.indices) {
            val b = buttons[i]
            if (b.index > 11) continue // OCT is drawn by the system block

            val playGreen = b.index == 10 && panelState.isButtonPlayGreen()
            val lit = panelState.isButtonLit(b.index) && !playGreen
            val face = when {
                playGreen -> palette.playGreen
                lit -> palette.ledLit
                panelState.isButtonBreathing(b.index) ->
                    lerpColor(palette.ledDim, palette.ledBreath, breathPeak * panelState.breathLevel)
                panelState.isButtonDim(b.index) -> palette.ledDim
                else -> palette.button
            }
            val ink = when {
                lit || playGreen -> palette.panel
                b.index == 11 -> palette.rec
                else -> palette.buttonLabel
            }
            val held = (heldButtonsMask and (1 shl b.index)) != 0
            if (b.index == 10) {
                PanelStyle.drawButton(scope, textMeasurer, b.rect, "PLAY", "STOP", face, ink, held, palette, u)
            } else {
                val label = panelState.buttonNames.getOrElse(b.index) { b.label }
                PanelStyle.drawButton(scope, textMeasurer, b.rect, label, null, face, ink, held, palette, u)
            }
        }
    }
}
