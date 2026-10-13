package io.github.fm1deck.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class SettingsStoreTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    @Test
    fun testMissingFileLoadsDefaults() {
        val nonExistent = File(tempFolder.root, "settings.json")
        val settings = SettingsStore.load(nonExistent)

        assertEquals(AppSettings.DEFAULT_SKIN_ID, settings.activeSkinId)
        assertEquals(AppSettings.DEFAULT_PALETTE_ID, settings.activePaletteId)
        assertEquals(AppSettings.DEFAULT_MASTER_ADC, settings.masterAdc)
        assertEquals(AppSettings.DEFAULT_KEYBOARD_FRACTION, settings.keyboardFraction, 0.001f)
    }

    @Test
    fun testPartialJsonUsesDefaultsForMissingFields() {
        val json = """
            {
              "schema": 1,
              "activeSkinId": "knobs-top"
            }
        """.trimIndent()

        val settings = SettingsStore.fromJson(json)
        assertEquals("knobs-top", settings.activeSkinId)
        assertEquals(AppSettings.DEFAULT_PALETTE_ID, settings.activePaletteId)
        assertEquals(AppSettings.DEFAULT_MASTER_ADC, settings.masterAdc)
    }

    @Test
    fun testUnknownFieldsSafelyIgnored() {
        val json = """
            {
              "schema": 1,
              "unknownField": 12345,
              "activeSkinId": "screen-center"
            }
        """.trimIndent()

        val settings = SettingsStore.fromJson(json)
        assertEquals("screen-center", settings.activeSkinId)
    }

    @Test
    fun testSaveAndLoadRoundTrip() {
        val file = File(tempFolder.root, "settings.json")
        val original = AppSettings(
            activeSkinId = "classic",
            activePaletteId = "graphite",
            keyboardFraction = 0.40f,
            screenPixelated = true,
            knobSensitivityDp = 8.5f,
            masterAdc = 850
        )

        val saved = SettingsStore.save(file, original)
        assertTrue(saved)
        assertTrue(file.exists())

        val loaded = SettingsStore.load(file)
        assertEquals(original.activeSkinId, loaded.activeSkinId)
        assertEquals(original.activePaletteId, loaded.activePaletteId)
        assertEquals(original.keyboardFraction, loaded.keyboardFraction, 0.001f)
        assertEquals(original.screenPixelated, loaded.screenPixelated)
        assertEquals(original.knobSensitivityDp, loaded.knobSensitivityDp, 0.001f)
        assertEquals(original.masterAdc, loaded.masterAdc)
    }

    @Test
    fun testMigrationClassicFramedToClassicWithFramesOn() {
        val json = """
            {
              "schema": 1,
              "activeSkinId": "classic-framed",
              "framesOverride": "skin"
            }
        """.trimIndent()

        val settings = SettingsStore.fromJson(json)
        assertEquals("classic", settings.activeSkinId)
        assertEquals("on", settings.framesOverride)
    }

    @Test
    fun testUnknownSkinIdFallsBackToClassic() {
        val json = """
            {
              "schema": 1,
              "activeSkinId": "non-existent-imported-skin",
              "framesOverride": "none"
            }
        """.trimIndent()

        val settings = SettingsStore.fromJson(json)
        assertEquals("classic", settings.activeSkinId)
        assertEquals("none", settings.framesOverride)
    }
}
