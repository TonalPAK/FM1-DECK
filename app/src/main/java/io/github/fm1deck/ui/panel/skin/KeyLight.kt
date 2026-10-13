package io.github.fm1deck.ui.panel.skin

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp

/**
 * The key LEDs' colour (Settings → Skin): the palette's own (a warm white) or one that stands out from the grey
 * keybed, so a running step or a lit scale reads at a glance. Dim / mid / breath levels are the same colour, darker.
 */
object KeyLight {
    const val DEFAULT = "palette"

    /** id to colour, in the order Settings shows them; [DEFAULT] keeps the palette's. */
    val COLORS: List<Pair<String, Color?>> = listOf(
        DEFAULT to null,
        "red" to Color(0xFFFF4D4D),
        "orange" to Color(0xFFFF8A3D),
        "amber" to Color(0xFFFFC94A),
        "green" to Color(0xFF4CE07A),
        "cyan" to Color(0xFF3FD4E8),
        "blue" to Color(0xFF4D8DFF),
        "violet" to Color(0xFFA66BFF),
        "pink" to Color(0xFFFF5FB8)
    )

    fun color(id: String): Color? = COLORS.firstOrNull { it.first == id }?.second

    /** [palette] with its key slit colours in [id]'s. */
    fun apply(palette: Palette, id: String): Palette {
        val c = color(id) ?: return palette
        return palette.copy(
            keyLit = c,
            keySlitBreath = c,
            keySlitMid = lerp(palette.keyBar, c, 0.55f),
            keySlitDim = lerp(palette.keyBar, c, 0.30f)
        )
    }
}
