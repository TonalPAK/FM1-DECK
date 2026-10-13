package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import io.github.fm1deck.ui.panel.PanelElement
import io.github.fm1deck.ui.panel.PanelState
import io.github.fm1deck.ui.panel.Rect
import io.github.fm1deck.ui.panel.skin.Palette

/**
 * The keybed as the emulator: one dark tray, 27 black capsules with a light edge, each with a slit that shows the
 * firmware's key LEDs (off / dim / mid / breath / lit with a glow). Held = lit-colour outline.
 *
 * Every key carries its note name above the slit: the firmware's screen names keys by note
 * ("F", "B", "C"…), so the letter lights up with the key's LED — bright when the firmware offers the key as a
 * function, glowing with the LED while playing, faint when the key is off.
 */
object KeyboardRenderer {
    /** Key bit = semitones above F3. */
    val NOTE_NAMES = arrayOf("F", "F♯", "G", "G♯", "A", "A♯", "B", "C", "C♯", "D", "D♯", "E")
    fun noteName(bit: Int): String = NOTE_NAMES[bit % 12]

    fun draw(
        scope: DrawScope,
        keyboardRect: Rect,
        keys: List<PanelElement.Key>,
        palette: Palette,
        panelState: PanelState,
        heldKeysMask: Int,
        u: Float,
        textMeasurer: TextMeasurer? = null,
        hintDrawer: ((DrawScope, PanelElement.Key) -> Unit)? = null,
        beatMarks: Boolean = false
    ) {
        val r = keyboardRect.height * 0.07f
        PanelStyle.drawTray(scope, keyboardRect, r, palette)
        if (beatMarks) drawBeatMarks(scope, keys, palette, u)
        val breathPeak = if ((panelState.leds.getOrElse(7) { 0 }) != 0) 0.75f else 1f
        for (i in keys.indices) {
            drawKey(scope, keys[i], palette, panelState, heldKeysMask, u, breathPeak, hintDrawer)
            if (textMeasurer != null) drawNoteName(scope, textMeasurer, keys[i], palette, panelState, heldKeysMask)
        }
    }

    private fun drawKey(
        scope: DrawScope,
        key: PanelElement.Key,
        palette: Palette,
        panelState: PanelState,
        heldKeysMask: Int,
        u: Float,
        breathPeak: Float,
        hintDrawer: ((DrawScope, PanelElement.Key) -> Unit)?
    ) {
        val held = (heldKeysMask and (1 shl key.bit)) != 0
        val cap = key.capsuleRect
        val capRadius = CornerRadius(cap.width / 2f, cap.width / 2f)
        scope.drawRoundRect(color = palette.keyBody, topLeft = cap.toOffset(), size = cap.toSize(), cornerRadius = capRadius)
        val edgeW = if (held) 2.2f * u else maxOf(1f, 0.8f * u)
        val inset = edgeW / 2f
        scope.drawRoundRect(
            color = if (held) palette.ledLit else palette.keyBorder,
            topLeft = Offset(cap.left + inset, cap.top + inset),
            size = Size(cap.width - edgeW, cap.height - edgeW),
            cornerRadius = CornerRadius(cap.width / 2f - inset, cap.width / 2f - inset),
            style = Stroke(width = edgeW)
        )

        val lit = panelState.isKeyLit(key.bit)
        val slit = when {
            lit -> palette.keyLit
            panelState.isKeyBreathing(key.bit) ->
                lerpColor(palette.keyBar, palette.keySlitBreath, breathPeak * panelState.breathLevel)
            panelState.isKeyMid(key.bit) -> palette.keySlitMid
            panelState.isKeyDim(key.bit) -> palette.keySlitDim
            else -> palette.keyBar
        }
        val s = key.stripRect
        if (lit) {
            // The emulator's drop-shadow glow around a lit slit
            val gw = s.width * 3f
            val gh = s.height + s.width * 2f
            scope.drawRoundRect(
                color = palette.keyLit.copy(alpha = 0.22f),
                topLeft = Offset(s.centerX - gw / 2f, s.centerY - gh / 2f),
                size = Size(gw, gh),
                cornerRadius = CornerRadius(gw / 2f, gw / 2f)
            )
        }
        scope.drawRoundRect(
            color = slit,
            topLeft = s.toOffset(),
            size = s.toSize(),
            cornerRadius = CornerRadius(s.width / 2f, s.width / 2f)
        )
        hintDrawer?.invoke(scope, key)
    }

    /** A key that is a white one on a piano: the 16 of them are a 16-step pattern's steps on these firmwares. */
    private fun isWhite(bit: Int): Boolean = !NOTE_NAMES[bit % 12].endsWith("♯")

    /**
     * Thin marks in the gaps after every fourth white key (steps 4 | 5, 8 | 9, 12 | 13), down the white row: the
     * beats of a running pattern, read at a glance.
     */
    private fun drawBeatMarks(scope: DrawScope, keys: List<PanelElement.Key>, palette: Palette, u: Float) {
        val whites = keys.filter { isWhite(it.bit) }.sortedBy { it.capsuleRect.left }
        val color = palette.buttonLabelDim.copy(alpha = 0.55f)
        var i = 4
        while (i < whites.size) {
            val a = whites[i - 1].capsuleRect
            val b = whites[i].capsuleRect
            val x = (a.right + b.left) / 2f
            val top = minOf(a.top, b.top) + a.height * 0.12f
            val bottom = maxOf(a.bottom, b.bottom) - a.height * 0.12f
            scope.drawLine(color, Offset(x, top), Offset(x, bottom), strokeWidth = maxOf(1f, 0.9f * u))
            i += 4
        }
    }

    private fun drawNoteName(
        scope: DrawScope,
        textMeasurer: TextMeasurer,
        key: PanelElement.Key,
        palette: Palette,
        panelState: PanelState,
        heldKeysMask: Int
    ) {
        val held = (heldKeysMask and (1 shl key.bit)) != 0
        val color = when {
            held -> palette.ledLit
            panelState.isKeyLit(key.bit) -> palette.keyLit
            panelState.isKeyBreathing(key.bit) ->
                lerpColor(palette.keySlitDim, palette.buttonLabel, panelState.breathLevel)
            panelState.isKeyDim(key.bit) || panelState.isKeyMid(key.bit) -> palette.buttonLabel
            else -> palette.keyBorder.copy(alpha = 0.9f)
        }
        val cap = key.capsuleRect
        // The space above the slit is 0.4 of the capsule's width
        val px = PanelStyle.clampPx(scope, cap.width * 0.30f, 8f, 13f)
        val style = PanelStyle.textStyle(scope, PanelStyle.sansFont, px, color, FontWeight.SemiBold)
        val layout = TextLayoutCache.getOrMeasure(textMeasurer, noteName(key.bit), style)
        val slitTop = key.stripRect.top
        val y = (cap.top + slitTop) / 2f - layout.size.height / 2f
        scope.drawText(layout, topLeft = Offset(cap.centerX - layout.size.width / 2f, y))
    }
}
