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
    val linkQuantum: Int = 4,
    /**
     * The rack: the firmware in each of [RACK_SLOTS] slots ("" = empty), all playing at once into the master bus.
     * [activeFwId] and [masterAdc] are the focused slot's (the one the panel plays), kept in step by [normalized].
     */
    val rack: List<String> = listOf(DEFAULT_FW_ID) + List(RACK_SLOTS - 1) { "" },
    /** Each slot's own MASTER pot (the firmware's volume), 0..1023. */
    val rackMasters: List<Int> = List(RACK_SLOTS) { DEFAULT_MASTER_ADC },
    val rackFocus: Int = 0,
    /** The key LEDs' colour (KeyLight id; "palette" = the palette's own). */
    val keyLight: String = "palette",
    /** Thin marks between every four white keys (the steps of a 16-step pattern). */
    val keyBeatMarks: Boolean = true,
    /** Mute and solo, a bit per slot. */
    val rackMute: Int = 0,
    val rackSolo: Int = 0,
    /** The rack's shared tempo (its clock runs while two or more slots are filled), BPM. */
    val rackTempo: Double = 120.0
) {
    /** The rack made whole: [RACK_SLOTS] slots, known firmwares once each, focus on a filled slot, mirrors set. */
    fun normalized(known: (String) -> Boolean = { true }): AppSettings {
        val seen = HashSet<String>()
        val slots = List(RACK_SLOTS) { i ->
            rack.getOrElse(i) { "" }.takeIf { it.isNotEmpty() && known(it) && seen.add(it) } ?: ""
        }.toMutableList()
        if (slots.all { it.isEmpty() }) slots[0] = activeFwId.takeIf(known) ?: DEFAULT_FW_ID
        val masters = List(RACK_SLOTS) { i -> rackMasters.getOrElse(i) { DEFAULT_MASTER_ADC }.coerceIn(0, 1023) }
        val focus = rackFocus.takeIf { it in slots.indices && slots[it].isNotEmpty() } ?: slots.indexOfFirst { it.isNotEmpty() }
        return copy(rack = slots, rackMasters = masters, rackFocus = focus, activeFwId = slots[focus], masterAdc = masters[focus])
    }

    /** Focus on [slot] (a filled one). */
    fun withFocus(slot: Int): AppSettings = copy(rackFocus = slot).normalized()

    /** [fwId] into [slot] ("" empties it); a firmware already in another slot moves here. */
    fun withSlot(slot: Int, fwId: String): AppSettings {
        val slots = rack.toMutableList()
        if (fwId.isNotEmpty()) for (i in slots.indices) if (slots[i] == fwId) slots[i] = ""
        if (slot in slots.indices) slots[slot] = fwId
        // A new firmware in a slot starts heard
        val clear = (1 shl slot).inv()
        return copy(rack = slots, rackMute = rackMute and clear, rackSolo = rackSolo and clear).normalized()
    }

    /** The focused slot's MASTER pot. */
    fun withFocusMaster(adc: Int): AppSettings =
        copy(rackMasters = rackMasters.toMutableList().also { if (rackFocus in it.indices) it[rackFocus] = adc })
            .normalized()

    companion object {
        /** Slots the rack offers (the native engine has room for AudioEngine::kSlots). */
        const val RACK_SLOTS = 3
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
