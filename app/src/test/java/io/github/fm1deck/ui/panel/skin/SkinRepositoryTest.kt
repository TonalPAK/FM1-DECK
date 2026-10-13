package io.github.fm1deck.ui.panel.skin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File

class SkinRepositoryTest {

    private fun findSkinsDir(): File {
        val candidates = listOf(
            File("src/main/assets/skins"),
            File("app/src/main/assets/skins"),
            File("../app/src/main/assets/skins")
        )
        return candidates.firstOrNull { it.exists() && it.isDirectory }
            ?: throw IllegalStateException("Could not find assets/skins directory")
    }

    @Test
    fun testAllBuiltInSkinsLoadAndValidate() {
        val skinsDir = findSkinsDir()
        val skins = SkinRepository.loadFromDirectory(skinsDir)

        assertEquals("Must load exactly 7 built-in skins", 7, skins.size)

        val ids = skins.map { it.id }
        for (expectedId in SkinRepository.BUILT_IN_IDS) {
            assertTrue("Expected skin $expectedId to be loaded", ids.contains(expectedId))
        }

        // Check sorted by order
        for (i in 0 until skins.size - 1) {
            assertTrue(
                "Skins should be sorted by order: ${skins[i].order} <= ${skins[i + 1].order}",
                skins[i].order <= skins[i + 1].order
            )
        }

        for (skin in skins) {
            assertEquals("Schema must be 1", 1, skin.schema)
            assertTrue("Name en must be non-blank", skin.name.en.isNotBlank())
            assertEquals("Padding size must be 4", 4, skin.panel.padding.size)
            // A tall portrait screen needs a smaller share for the same key height
            val range = if (skin.isPortrait) 0.15f..0.40f else 0.3f..0.45f
            assertTrue("keyboardFraction in range", skin.panel.keyboardFraction in range)
            assertTrue("minKeyboardDp positive", skin.panel.minKeyboardDp > 0f)
            assertTrue("Palette non-blank", skin.palette.isNotBlank())
        }
    }

    @Test
    fun testRejectsInvalidSchema() {
        val json = """
            {
              "schema": 2,
              "id": "bad-schema",
              "order": 1,
              "name": { "en": "Bad" },
              "panel": {
                "padding": [0, 0, 0, 0],
                "gap": 10,
                "keyboardFraction": 0.36,
                "minKeyboardDp": 112,
                "top": { "type": "row", "gap": 10, "children": [
                  { "block": "system", "weight": 1 },
                  { "block": "buttons", "weight": 1 },
                  { "block": "knobs", "weight": 1 },
                  { "block": "screen", "weight": 1 }
                ]}
              },
              "frames": { "system": true, "buttons": true, "knobs": true, "screen": true },
              "palette": "graphite"
            }
        """.trimIndent()

        try {
            SkinRepository.parseSkin(json)
            fail("Should have thrown IllegalArgumentException on unsupported schema")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message?.contains("Unsupported skin schema") == true)
        }
    }

    @Test
    fun testRejectsMissingBlock() {
        val json = """
            {
              "schema": 1,
              "id": "missing-block",
              "order": 1,
              "name": { "en": "Bad" },
              "panel": {
                "padding": [0, 0, 0, 0],
                "gap": 10,
                "keyboardFraction": 0.36,
                "minKeyboardDp": 112,
                "top": { "type": "row", "gap": 10, "children": [
                  { "block": "system", "weight": 1 },
                  { "block": "buttons", "weight": 1 },
                  { "block": "knobs", "weight": 1 }
                ]}
              },
              "frames": { "system": true, "buttons": true, "knobs": true, "screen": true },
              "palette": "graphite"
            }
        """.trimIndent()

        try {
            SkinRepository.parseSkin(json)
            fail("Should have thrown IllegalArgumentException on missing 'screen' block")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message?.contains("screen") == true)
        }
    }

    @Test
    fun testRejectsDuplicateBlock() {
        val json = """
            {
              "schema": 1,
              "id": "duplicate-block",
              "order": 1,
              "name": { "en": "Bad" },
              "panel": {
                "padding": [0, 0, 0, 0],
                "gap": 10,
                "keyboardFraction": 0.36,
                "minKeyboardDp": 112,
                "top": { "type": "row", "gap": 10, "children": [
                  { "block": "system", "weight": 1 },
                  { "block": "buttons", "weight": 1 },
                  { "block": "knobs", "weight": 1 },
                  { "block": "screen", "weight": 1 },
                  { "block": "screen", "weight": 1 }
                ]}
              },
              "frames": { "system": true, "buttons": true, "knobs": true, "screen": true },
              "palette": "graphite"
            }
        """.trimIndent()

        try {
            SkinRepository.parseSkin(json)
            fail("Should have thrown IllegalArgumentException on duplicate 'screen' block")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message?.contains("found 2") == true)
        }
    }
}
