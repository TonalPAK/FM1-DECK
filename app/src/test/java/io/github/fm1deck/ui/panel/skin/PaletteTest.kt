package io.github.fm1deck.ui.panel.skin

import androidx.compose.ui.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.pow

class PaletteTest {

    private fun findPalettesFile(): File {
        val candidates = listOf(
            File("src/main/assets/skins/palettes.json"),
            File("app/src/main/assets/skins/palettes.json"),
            File("../app/src/main/assets/skins/palettes.json")
        )
        return candidates.firstOrNull { it.exists() && it.isFile }
            ?: throw IllegalStateException("Could not find palettes.json")
    }

    private fun relativeLuminance(color: Color): Double {
        fun channelLuminance(c: Float): Double {
            return if (c <= 0.04045f) {
                c / 12.92
            } else {
                ((c + 0.055) / 1.055).pow(2.4)
            }
        }
        val r = channelLuminance(color.red)
        val g = channelLuminance(color.green)
        val b = channelLuminance(color.blue)
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    }

    private fun contrastRatio(c1: Color, c2: Color): Double {
        val l1 = relativeLuminance(c1)
        val l2 = relativeLuminance(c2)
        val maxL = maxOf(l1, l2)
        val minL = minOf(l1, l2)
        return (maxL + 0.05) / (minL + 0.05)
    }

    @Test
    fun testAllThreePalettesLoadAndMatchTokens() {
        val file = findPalettesFile()
        val palettes = PaletteRepository.loadFromFile(file)

        val requiredPalettes = listOf("graphite", "slate", "sand")
        for (id in requiredPalettes) {
            assertTrue("Must contain palette '$id'", palettes.containsKey(id))
            val p = palettes[id]!!

            assertNotNull(p.nameEn)
            assertNotNull(p.background)
            assertNotNull(p.panel)
            assertNotNull(p.frame)
            assertNotNull(p.button)
            assertNotNull(p.buttonPressed)
            assertNotNull(p.buttonTopHighlight)
            assertNotNull(p.buttonBottomShadow)
            assertNotNull(p.buttonTray)
            assertNotNull(p.buttonLabel)
            assertNotNull(p.buttonLabelDim)
            assertNotNull(p.ledLit)
            assertNotNull(p.ledDim)
            assertNotNull(p.ledBreath)
            assertNotNull(p.select)
            assertNotNull(p.rec)
            assertNotNull(p.playGreen)
            assertNotNull(p.keyBody)
            assertNotNull(p.keyInner)
            assertNotNull(p.keyWhite)
            assertNotNull(p.keyBlack)
            assertNotNull(p.keyBorder)
            assertNotNull(p.keyBar)
            assertNotNull(p.keyLit)
            assertNotNull(p.knobBody)
            assertNotNull(p.knobRim)
            assertNotNull(p.knobRing)
            assertNotNull(p.knobIndicator)
            assertNotNull(p.knobGlow)
            assertNotNull(p.screenFrame)
            assertNotNull(p.screenLcdBg)
            assertNotNull(p.hintText)
        }
    }

    @Test
    fun testContrastRatiosSatisfyWcagStandard() {
        val file = findPalettesFile()
        val palettes = PaletteRepository.loadFromFile(file)

        for (id in listOf("graphite", "slate", "sand")) {
            val p = palettes[id]!!

            // Button label vs button background must be >= 4.5:1
            val labelContrast = contrastRatio(p.buttonLabel, p.button)
            assertTrue(
                "Palette $id label contrast $labelContrast must be >= 4.5:1",
                labelContrast >= 4.5
            )

            // Dim (available) buttons are the common state: label vs ledDim face
            val dimContrast = contrastRatio(p.buttonLabel, p.ledDim)
            assertTrue("Palette $id dim contrast $dimContrast must be >= 4.5:1", dimContrast >= 4.5)

            // Lit buttons draw their label in the panel colour on ledLit (and on PLAY's green)
            val litContrast = contrastRatio(p.panel, p.ledLit)
            assertTrue("Palette $id lit contrast $litContrast must be >= 4.5:1", litContrast >= 4.5)
            val greenContrast = contrastRatio(p.panel, p.playGreen)
            assertTrue("Palette $id green contrast $greenContrast must be >= 4.5:1", greenContrast >= 4.5)

            // REC's red label on a plain button stays readable (large-text level)
            val recContrast = contrastRatio(p.rec, p.button)
            assertTrue("Palette $id REC contrast $recContrast must be >= 3:1", recContrast >= 3.0)
        }
    }

    /** Every palette writes every token itself, so none silently falls back to graphite. */
    @Test
    fun testEveryPaletteDefinesEveryToken() {
        val root = org.json.JSONObject(findPalettesFile().readText()).getJSONObject("palettes")
        val tokens = root.getJSONObject("graphite").keys().asSequence().toSet()
        for (id in listOf("graphite", "slate", "sand")) {
            val obj = root.getJSONObject(id)
            assertEquals("Palette $id token set", tokens, obj.keys().asSequence().toSet())
        }
        for (t in listOf("keySlitDim", "keySlitMid", "keySlitBreath", "buttonTray", "knobGlow")) {
            assertTrue("Token $t present", t in tokens)
        }
    }

    /** The built-in fallback equals the shipped graphite palette. */
    @Test
    fun testBuiltInGraphiteMatchesAsset() {
        val shipped = PaletteRepository.loadFromFile(findPalettesFile()).getValue("graphite")
        assertEquals(PaletteRepository.GRAPHITE, shipped)
    }

    @Test
    fun testHexColorParsing() {
        val c6 = PaletteRepository.parseColorHex("#2A2A2A")
        val c8 = PaletteRepository.parseColorHex("#FF2A2A2A")
        assertEquals(c6, c8)
    }
}
