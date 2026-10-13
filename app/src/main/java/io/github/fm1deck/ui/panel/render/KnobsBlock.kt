package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.text.TextMeasurer
import io.github.fm1deck.ui.panel.BlockGeometry
import io.github.fm1deck.ui.panel.PanelElement
import io.github.fm1deck.ui.panel.PanelState
import io.github.fm1deck.ui.panel.skin.Palette

/** KNOB 1..4 (roles 3..6) straight on the panel body, as the emulator. */
object KnobsBlockRenderer {

    fun draw(
        scope: DrawScope,
        block: BlockGeometry,
        knobs: List<PanelElement.Knob>,
        hasFrame: Boolean,
        palette: Palette,
        panelState: PanelState,
        u: Float,
        textMeasurer: TextMeasurer
    ) {
        if (hasFrame) PanelStyle.drawFrame(scope, block.bounds, u, palette)
        val now = System.nanoTime()
        val labels = panelState.fwUiState?.knobLabels

        for (i in knobs.indices) {
            val k = knobs[i]
            if (k.role < 3 || k.role > 6) continue
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
            val custom = labels?.getOrNull(k.role - 3)
            val text = if (!custom.isNullOrBlank()) custom else k.defaultLabel
            SystemBlockRenderer.drawKnobLabel(scope, textMeasurer, text, k.knobCenter.x, k.knobCenter.y, k.knobRadius, palette, u)
        }
    }
}
