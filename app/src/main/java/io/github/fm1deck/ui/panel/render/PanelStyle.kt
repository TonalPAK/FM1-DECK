package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import io.github.fm1deck.R
import io.github.fm1deck.ui.panel.Rect
import io.github.fm1deck.ui.panel.skin.Palette
import kotlin.math.cos
import kotlin.math.sin

/**
 * The one panel style for every skin: the browser emulator's drawing (upstream felucca `web/emu/index.html`), which
 * the reference screenshots show. Body colour everywhere, dark trays (#141415) under the buttons, OCT, keys and around
 * every knob, flat caps with a light edge, DotGothic16 knob labels, semibold sans button labels.
 * Sizes are ours (from u and the layout); proportions are the emulator's.
 */
object PanelStyle {
    /** Knob labels: DotGothic16 (OFL-1.1), as the emulator. */
    val dotFont = FontFamily(Font(R.font.dotgothic16))
    val sansFont: FontFamily = FontFamily.SansSerif

    // The emulator's knob: a tray ring r + 5 around a cap of r = 24 with a 2 px edge; MASTER's line from r - 5 to r - 14
    const val WELL_RATIO = 29f / 24f
    private const val EDGE_RATIO = 2f / 24f
    private const val IND_OUTER = 19f / 24f
    private const val IND_INNER = 10f / 24f
    private const val IND_WIDTH = 3f / 24f

    private val styleCache = HashMap<Long, TextStyle>(32)

    /** A cached label style; size in px (converted to sp for this scope's density and font scale). */
    fun textStyle(scope: DrawScope, family: FontFamily, sizePx: Float, color: Color, weight: FontWeight): TextStyle {
        val familyBits = if (family === dotFont) 1L else 0L
        val key = (color.toArgb().toLong() shl 24) xor ((sizePx * 8f).toLong() shl 2) xor familyBits xor
            (weight.weight.toLong() shl 56)
        return styleCache.getOrPut(key) {
            TextStyle(
                color = color,
                fontSize = (sizePx / (scope.density * scope.fontScale)).sp,
                fontFamily = family,
                fontWeight = weight,
                textAlign = TextAlign.Center,
                letterSpacing = 0.04.em
            )
        }
    }

    /** px for a size given in sp, clamped. */
    fun clampPx(scope: DrawScope, px: Float, minSp: Float, maxSp: Float): Float {
        val k = scope.density * scope.fontScale
        return px.coerceIn(minSp * k, maxSp * k)
    }

    fun drawTray(scope: DrawScope, rect: Rect, radius: Float, palette: Palette) {
        scope.drawRoundRect(
            color = palette.buttonTray,
            topLeft = rect.toOffset(),
            size = rect.toSize(),
            cornerRadius = CornerRadius(radius, radius)
        )
    }

    /** The "block frames" setting: a thin lighter outline a little outside the block. */
    fun drawFrame(scope: DrawScope, rect: Rect, u: Float, palette: Palette) {
        val o = 4f * u
        scope.drawRoundRect(
            color = palette.frame,
            topLeft = Offset(rect.left - o, rect.top - o),
            size = Size(rect.width + 2f * o, rect.height + 2f * o),
            cornerRadius = CornerRadius(10f * u, 10f * u),
            style = Stroke(width = 1.4f * u)
        )
    }

    /** A centred label whose baseline-ish bottom sits at [bottom]. */
    fun drawLabelAbove(
        scope: DrawScope,
        textMeasurer: TextMeasurer,
        text: String,
        centerX: Float,
        bottom: Float,
        style: TextStyle
    ) {
        val layout = TextLayoutCache.getOrMeasure(textMeasurer, text, style)
        scope.drawText(layout, topLeft = Offset(centerX - layout.size.width / 2f, bottom - layout.size.height))
    }

    /**
     * A knob as the emulator draws it, plus our touch feedback ("light up the moment it is touched", like
     * Drambo): a soft glow around the well, the well's ring and the cap edge in the glow colour, and the position line.
     *
     * @param wellRadius outer radius of the dark ring; the cap is wellRadius / WELL_RATIO
     * @param angleDeg indicator angle, 0 = 12 o'clock, clockwise
     * @param indicatorAlpha 1 = always shown (MASTER), else shown with the touch
     */
    fun drawKnob(
        scope: DrawScope,
        center: Offset,
        wellRadius: Float,
        palette: Palette,
        touchAlpha: Float,
        pulsing: Boolean,
        angleDeg: Float,
        indicatorAlpha: Float
    ) {
        val capR = wellRadius / WELL_RATIO
        if (touchAlpha > 0f) {
            val haloR = wellRadius * 1.45f
            scope.drawCircle(
                brush = Brush.radialGradient(
                    0f to palette.knobGlow.copy(alpha = 0.30f * touchAlpha),
                    (wellRadius / haloR) to palette.knobGlow.copy(alpha = 0.16f * touchAlpha),
                    1f to Color.Transparent,
                    center = center,
                    radius = haloR
                ),
                radius = haloR,
                center = center
            )
        }
        // The dark well
        scope.drawCircle(color = palette.knobRim, radius = wellRadius, center = center)
        if (touchAlpha > 0f) {
            scope.drawCircle(
                color = palette.knobGlow.copy(alpha = 0.85f * touchAlpha),
                radius = wellRadius - capR * 0.06f,
                center = center,
                style = Stroke(width = capR * 0.10f)
            )
        }
        // The cap and its light edge
        val edgeW = capR * EDGE_RATIO
        scope.drawCircle(color = palette.knobBody, radius = capR, center = center)
        val edgeColor = if (touchAlpha > 0f) lerpColor(palette.knobRing, palette.knobGlow, 0.6f * touchAlpha) else palette.knobRing
        scope.drawCircle(color = edgeColor, radius = capR - edgeW / 2f, center = center, style = Stroke(width = edgeW))

        val a = maxOf(indicatorAlpha, touchAlpha)
        if (a > 0f) {
            val rad = Math.toRadians(angleDeg.toDouble() - 90.0)
            val c = cos(rad).toFloat()
            val s = sin(rad).toFloat()
            val color = if (pulsing) palette.knobGlow else palette.knobIndicator
            scope.drawLine(
                color = color.copy(alpha = a),
                start = Offset(center.x + capR * IND_INNER * c, center.y + capR * IND_INNER * s),
                end = Offset(center.x + capR * IND_OUTER * c, center.y + capR * IND_OUTER * s),
                strokeWidth = capR * IND_WIDTH * (if (pulsing) 1.4f else 1f),
                cap = StrokeCap.Round
            )
        }
    }

    /** Knob label size in px: the emulator's 16 px for a cap of 24, clamped to 9..13 sp. */
    fun knobLabelPx(scope: DrawScope, wellRadius: Float): Float =
        clampPx(scope, wellRadius / WELL_RATIO * (16f / 24f), 9f, 13f)

    /**
     * A panel button as the emulator: rounded face with a 1.5 px edge, a faint drop shadow, held = lit-colour outline.
     */
    fun drawButton(
        scope: DrawScope,
        textMeasurer: TextMeasurer,
        rect: Rect,
        label: String,
        sub: String?,
        face: Color,
        ink: Color,
        held: Boolean,
        palette: Palette,
        u: Float
    ) {
        val short = minOf(rect.width, rect.height)
        val r = minOf(8f * u, short * 0.17f)
        val radius = CornerRadius(r, r)
        // Faint shadow under the face
        scope.drawRoundRect(
            color = palette.buttonBottomShadow.copy(alpha = 0.55f),
            topLeft = Offset(rect.left, rect.top + 1.2f * u),
            size = rect.toSize(),
            cornerRadius = radius
        )
        scope.drawRoundRect(color = face, topLeft = rect.toOffset(), size = rect.toSize(), cornerRadius = radius)
        val edgeW = if (held) 2.2f * u else maxOf(1f, 0.9f * u)
        val inset = edgeW / 2f
        scope.drawRoundRect(
            color = if (held) palette.ledLit else palette.buttonTopHighlight,
            topLeft = Offset(rect.left + inset, rect.top + inset),
            size = Size(rect.width - edgeW, rect.height - edgeW),
            cornerRadius = CornerRadius(r - inset, r - inset),
            style = Stroke(width = edgeW)
        )

        // Labels: one size for every button, OCT included, as the emulator (12.6 px; PLAY / STOP 10.8 + 9.9)
        if (sub == null) {
            val px = clampPx(scope, 11.5f * u, 8.5f, 13f)
            val style = textStyle(scope, sansFont, px, ink, FontWeight.SemiBold)
            val layout = TextLayoutCache.getOrMeasure(textMeasurer, label, style)
            scope.drawText(
                layout,
                topLeft = Offset(rect.centerX - layout.size.width / 2f, rect.centerY - layout.size.height / 2f)
            )
        } else {
            val px1 = clampPx(scope, 11.5f * u * (10.8f / 12.6f), 7.5f, 11.5f)
            val px2 = clampPx(scope, 11.5f * u * (9.9f / 12.6f), 7f, 10.5f)
            val l1 = TextLayoutCache.getOrMeasure(textMeasurer, label, textStyle(scope, sansFont, px1, ink, FontWeight.SemiBold))
            val l2 = TextLayoutCache.getOrMeasure(textMeasurer, sub, textStyle(scope, sansFont, px2, ink, FontWeight.SemiBold))
            val total = l1.size.height + l2.size.height * 0.85f
            val top = rect.centerY - total / 2f
            scope.drawText(l1, topLeft = Offset(rect.centerX - l1.size.width / 2f, top))
            scope.drawText(l2, topLeft = Offset(rect.centerX - l2.size.width / 2f, top + l1.size.height * 0.85f))
        }
    }
}
