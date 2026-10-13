package io.github.fm1deck.settings

/**
 * Application settings data model (schema 1).
 * Defaults in one place per docs/SETTINGS.md.
 */
data class AppSettings(
    val schema: Int = CURRENT_SCHEMA,
    val activeSkinId: String = DEFAULT_SKIN_ID,
    val activePaletteId: String = DEFAULT_PALETTE_ID,
    val framesOverride: String = FRAMES_OVERRIDE_SKIN,
    val keyboardFraction: Float = DEFAULT_KEYBOARD_FRACTION,
    val screenPixelated: Boolean = DEFAULT_SCREEN_PIXELATED,
    val knobSensitivityDp: Float = DEFAULT_KNOB_SENSITIVITY_DP,
    val fineModeOnLongPress: Boolean = DEFAULT_FINE_MODE,
    val glissando: Boolean = DEFAULT_GLISSANDO,
    val hapticsMode: String = HAPTICS_LIGHT,
    val masterAdc: Int = DEFAULT_MASTER_ADC,
    val activeFwId: String = DEFAULT_FW_ID,
    /** Master bus values by parameter key (MasterBusParams.h); missing keys = defaults. */
    val masterValues: Map<String, Float> = emptyMap(),
    /** Name of the preset the values came from (shown; edits keep it until another preset is chosen). */
    val masterPreset: String = DEFAULT_MASTER_PRESET,
    /** Master bus oversampling factor 1 / 2 / 4 / 8 (SETTINGS.md §3). */
    val quality: Int = DEFAULT_QUALITY,
    /** Settings → Master: the animated signal scope (can be switched off). */
    val masterScope: Boolean = true,
    /** Ableton Link: a shared start lands on a multiple of this many beats (1 / 2 / 4 / 8). Link itself starts off. */
    val linkQuantum: Int = 4
) {
    companion object {
        const val CURRENT_SCHEMA = 1
        const val DEFAULT_SKIN_ID = "classic"
        const val DEFAULT_PALETTE_ID = "graphite"

        const val FRAMES_OVERRIDE_SKIN = "skin"
        const val FRAMES_OVERRIDE_ON = "on"
        const val FRAMES_OVERRIDE_ALL = "all"
        const val FRAMES_OVERRIDE_OFF = "off"
        const val FRAMES_OVERRIDE_NONE = "none"

        const val DEFAULT_KEYBOARD_FRACTION = 0.36f
        const val DEFAULT_SCREEN_PIXELATED = false
        const val DEFAULT_KNOB_SENSITIVITY_DP = 6.0f
        const val DEFAULT_FINE_MODE = true
        const val DEFAULT_GLISSANDO = false

        const val HAPTICS_OFF = "off"
        const val HAPTICS_LIGHT = "light"
        const val HAPTICS_MEDIUM = "medium"

        const val DEFAULT_MASTER_ADC = 700
        const val DEFAULT_FW_ID = "jiant"
        const val DEFAULT_MASTER_PRESET = "Default"
        const val DEFAULT_QUALITY = 2
        val QUALITIES = listOf(1, 2, 4, 8)
    }
}
