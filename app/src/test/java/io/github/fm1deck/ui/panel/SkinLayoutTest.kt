package io.github.fm1deck.ui.panel

import io.github.fm1deck.ui.panel.skin.BlockId
import io.github.fm1deck.ui.panel.skin.SkinRepository
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.abs

class SkinLayoutTest {

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
    fun testLayoutOnSamsungAndPocoDimensions() {
        val skinsDir = findSkinsDir()
        val skins = SkinRepository.loadFromDirectory(skinsDir)
        assertEquals("Must load 6 skins", 6, skins.size)

        // Samsung Galaxy M52: 914 x 411 dp @ 2.625
        val samsungDensity = 2.625f
        val samsungW = 914f * samsungDensity
        val samsungH = 411f * samsungDensity

        // POCO C75: 820 x 360 dp @ 2.0
        val pocoDensity = 2.0f
        val pocoW = 820f * pocoDensity
        val pocoH = 360f * pocoDensity

        for (skin in skins) {
            // Verify Samsung
            val gSamsung = SkinLayout.calculate(skin, samsungW, samsungH, samsungDensity)
            verifyGeometry(gSamsung, samsungW, samsungH, samsungDensity, isPoco = false)
            assertTrue(
                "Samsung LCD size ${gSamsung.screen.lcdRect.width} must be >= 244e77d baseline (542.89 px)",
                gSamsung.screen.lcdRect.width >= 542.89f
            )

            // Verify POCO
            val gPoco = SkinLayout.calculate(skin, pocoW, pocoH, pocoDensity)
            verifyGeometry(gPoco, pocoW, pocoH, pocoDensity, isPoco = true)
            assertTrue(
                "POCO LCD size ${gPoco.screen.lcdRect.width} must be >= 244e77d baseline (362.30 px)",
                gPoco.screen.lcdRect.width >= 362.30f
            )
        }
    }

    private fun verifyGeometry(
        g: PanelGeometry,
        widthPx: Float,
        heightPx: Float,
        density: Float,
        isPoco: Boolean
    ) {
        // 1. Keyboard spans full width minus small symmetric margin (2 * u)
        val expectedKbWidth = widthPx - 4f * g.u
        assertEquals("Keyboard must span full width minus 4*u", expectedKbWidth, g.keyboardRect.width, 0.5f)
        assertEquals("Keyboard left margin must be 2*u", 2f * g.u, g.keyboardRect.left, 0.5f)
        assertEquals("Keyboard right margin must be 2*u", 2f * g.u, widthPx - g.keyboardRect.right, 0.5f)

        // Top area margins must be symmetric
        val topAreaLeftMargin = g.topAreaRect.left
        val topAreaRightMargin = widthPx - g.topAreaRect.right
        assertEquals("Top area left margin must equal right margin", topAreaLeftMargin, topAreaRightMargin, 0.5f)

        // 2. Screen LCD is square (1:1 aspect ratio)
        assertEquals(
            "Screen LCD must be square",
            g.screen.lcdRect.width,
            g.screen.lcdRect.height,
            0.5f
        )
        assertTrue("Screen LCD size positive", g.screen.lcdRect.width > 50f)

        // 3. Exactly 27 keys: 16 white and 11 black
        assertEquals("Total keys must be 27", 27, g.keys.size)
        val whiteKeys = g.keys.filter { !it.isBlack }
        val blackKeys = g.keys.filter { it.isBlack }
        assertEquals("White keys count", 16, whiteKeys.size)
        assertEquals("Black keys count", 11, blackKeys.size)

        // Check chromatic order
        val expectedWhite = SkinLayout.MW_WHITE_SEMITONES
        for (i in expectedWhite.indices) {
            assertEquals("White key bit at $i", expectedWhite[i], whiteKeys[i].bit)
        }
        val expectedBlack = SkinLayout.MW_BLACK_AFTER_WHITE.map { expectedWhite[it] + 1 }
        for (i in expectedBlack.indices) {
            assertEquals("Black key bit at $i", expectedBlack[i], blackKeys[i].bit)
        }

        // 4. Check targets size >= 40 dp (except documented OCT±)
        val minTarget40Dp = 40f * density

        // Buttons 0..11 (FX..REC)
        for (i in 0..11) {
            val btn = g.buttons.first { it.index == i }
            assertTrue(
                "Button ${btn.label} width ${btn.hitRect.width} >= 40dp ($minTarget40Dp)",
                btn.hitRect.width >= minTarget40Dp - 2f // small rounding tolerance
            )
            assertTrue(
                "Button ${btn.label} height ${btn.hitRect.height} >= 40dp ($minTarget40Dp)",
                btn.hitRect.height >= minTarget40Dp - 2f
            )
        }

        // OCT buttons (stretch across system block)
        val octDn = g.buttons.first { it.index == SkinLayout.BTN_OCTDN }
        val octUp = g.buttons.first { it.index == SkinLayout.BTN_OCTUP }
        assertTrue("OCT- width >= 40dp", octDn.hitRect.width >= minTarget40Dp)
        assertTrue("OCT+ width >= 40dp", octUp.hitRect.width >= minTarget40Dp)
        // Documented exception per docs/SKINS.md §2 (OCT± height is ~30..36dp, stretches horizontally)
        assertTrue("OCT- height positive and >= 25dp", octDn.hitRect.height >= 25f * density)
        assertTrue("OCT+ height positive and >= 25dp", octUp.hitRect.height >= 25f * density)

        // Knobs (K1..K4, SELECT, ALGO, PRESET)
        for (k in g.knobs) {
            assertTrue("Knob ${k.defaultLabel} width >= 40dp", k.hitRect.width >= minTarget40Dp)
            assertTrue("Knob ${k.defaultLabel} height >= 40dp", k.hitRect.height >= minTarget40Dp)
            assertTrue("Knob radius > 0", k.knobRadius > 10f)
        }

        // Master pot
        assertTrue("Master pot width >= 40dp", g.masterPot.hitRect.width >= minTarget40Dp)
        assertTrue("Master pot height >= 40dp", g.masterPot.hitRect.height >= minTarget40Dp)

        // Settings dot: a corner target, documented exception >= 28 dp (docs/SKINS.md §2)
        assertTrue("Settings target >= 28dp", g.gear.hitRect.width >= SkinLayout.GEAR_MIN_DP * density - 0.5f)
        assertTrue("Settings target square", abs(g.gear.hitRect.width - g.gear.hitRect.height) < 0.5f)

        // White keys step >= 40dp
        for (k in whiteKeys) {
            assertTrue("White key width >= 40dp", k.hitRect.width >= minTarget40Dp)
            assertTrue("White key height >= 40dp", k.hitRect.height >= minTarget40Dp)
        }

        // 5. No overlapping elements
        // Check buttons 0..11 don't overlap
        for (i in 0..11) {
            for (j in (i + 1)..11) {
                val b1 = g.buttons.first { it.index == i }
                val b2 = g.buttons.first { it.index == j }
                assertFalse("Buttons $i and $j must not overlap", b1.rect.intersects(b2.rect))
            }
        }

        // Check white key capsules don't overlap each other
        for (i in 0 until whiteKeys.size - 1) {
            assertFalse(
                "White capsules $i and ${i + 1} must not overlap",
                whiteKeys[i].capsuleRect.intersects(whiteKeys[i + 1].capsuleRect)
            )
        }

        // Check black key capsules don't overlap each other
        for (i in 0 until blackKeys.size - 1) {
            assertFalse(
                "Black capsules $i and ${i + 1} must not overlap",
                blackKeys[i].capsuleRect.intersects(blackKeys[i + 1].capsuleRect)
            )
        }

        // Check top blocks don't overlap
        val blockList = g.blocks.values.toList()
        for (i in blockList.indices) {
            for (j in (i + 1) until blockList.size) {
                assertFalse(
                    "Blocks ${blockList[i].blockId} and ${blockList[j].blockId} must not overlap",
                    blockList[i].bounds.intersects(blockList[j].bounds)
                )
            }
        }

        assertGearClear(g, g.skinId)

        // The system block keeps its full height (no slot cut out of it): same top as the top area
        val sys = g.blocks.getValue(BlockId.SYSTEM).bounds
        assertEquals("System block starts at the top area in ${g.skinId}", g.topAreaRect.top, sys.top, 0.5f)
        // OCT- / OCT+ sit inside their island, the island at the bottom of the system block
        assertTrue("OCT island inside the system block", sys.contains(g.octTray.left, g.octTray.top) &&
            sys.contains(g.octTray.right, g.octTray.bottom))
        for (b in listOf(octDn, octUp)) {
            assertTrue("${b.label} inside its island", g.octTray.contains(b.rect.left, b.rect.top) &&
                g.octTray.contains(b.rect.right, b.rect.bottom))
        }
        // The twelve buttons sit inside the buttons tray
        val tray = g.blocks.getValue(BlockId.BUTTONS).bounds
        for (b in g.buttons) {
            if (b.index > 11) continue
            assertTrue("Button ${b.index} inside its tray", tray.contains(b.rect.left, b.rect.top) &&
                tray.contains(b.rect.right, b.rect.bottom))
        }
    }

    /**
     * The settings dot sits in the corner margin outside every block, and its 40 dp target covers no control:
     * no knob well, no button face, not the LCD, not the keybed.
     */
    private fun assertGearClear(g: PanelGeometry, label: String) {
        val gear = g.gear
        for (block in g.blocks.values) {
            assertFalse(
                "Settings dot must lie outside block ${block.blockId.jsonName} in $label",
                block.bounds.intersects(
                    Rect(gear.center.x - gear.radius, gear.center.y - gear.radius, gear.center.x + gear.radius, gear.center.y + gear.radius)
                )
            )
        }
        val hit = gear.hitRect
        fun circleHits(cx: Float, cy: Float, r: Float): Boolean {
            val nx = cx.coerceIn(hit.left, hit.right)
            val ny = cy.coerceIn(hit.top, hit.bottom)
            return (cx - nx) * (cx - nx) + (cy - ny) * (cy - ny) < r * r
        }
        for (k in g.knobs) {
            assertFalse("Settings target covers knob ${k.defaultLabel} in $label", circleHits(k.knobCenter.x, k.knobCenter.y, k.knobRadius))
        }
        assertFalse("Settings target covers MASTER in $label",
            circleHits(g.masterPot.knobCenter.x, g.masterPot.knobCenter.y, g.masterPot.knobRadius))
        for (b in g.buttons) {
            assertFalse("Settings target covers button ${b.index} in $label", hit.intersects(b.rect))
        }
        assertFalse("Settings target covers the LCD in $label", hit.intersects(g.screen.lcdRect))
        assertFalse("Settings target covers the keybed in $label", hit.intersects(g.keyboardRect))
        assertFalse("Settings target covers F in $label", hit.intersects(g.fnRect))
        assertFnPlaced(g, label)
    }

    /** F (latch) sits inside the system block, clear of MASTER's well and every other control. */
    private fun assertFnPlaced(g: PanelGeometry, label: String) {
        val f = g.fnRect
        val sys = g.blocks.getValue(BlockId.SYSTEM).bounds
        assertTrue("F has a size in $label", f.width > 4f && f.height > 4f)
        assertTrue("F on screen in $label", f.left >= 0f && f.right <= g.widthPx && f.top >= 0f)
        assertTrue("F level with the system block in $label", f.top >= sys.top - 0.5f && f.bottom <= sys.bottom + 0.5f)
        for (blk in g.blocks.values) if (blk.blockId != BlockId.SYSTEM)
            assertFalse("F overlaps block ${blk.blockId.jsonName} in $label", f.intersects(blk.bounds))
        assertFalse("F overlaps the keybed in $label", f.intersects(g.keyboardRect))
        for (k in g.knobs) {
            val kx = k.knobCenter.x.coerceIn(f.left, f.right); val ky = k.knobCenter.y.coerceIn(f.top, f.bottom)
            val wr = k.knobRadius * 29f / 24f
            assertTrue("F clear of ${k.defaultLabel} in $label",
                (k.knobCenter.x - kx) * (k.knobCenter.x - kx) + (k.knobCenter.y - ky) * (k.knobCenter.y - ky) >= wr * wr)
        }
        val m = g.masterPot
        val well = m.knobRadius * 29f / 24f
        val nx = m.knobCenter.x.coerceIn(f.left, f.right)
        val ny = m.knobCenter.y.coerceIn(f.top, f.bottom)
        assertTrue("F clear of MASTER's well in $label",
            (m.knobCenter.x - nx) * (m.knobCenter.x - nx) + (m.knobCenter.y - ny) * (m.knobCenter.y - ny) >= well * well)
        for (b in g.buttons) assertFalse("F overlaps button ${b.index} in $label", f.intersects(b.rect))
        assertTrue("F is hit-tested as F in $label", g.hitTest(f.centerX, f.centerY) is PanelTarget.Fn)
    }

    @Test
    fun testSymmetricMarginsWithCutouts() {
        val skinsDir = findSkinsDir()
        val skins = SkinRepository.loadFromDirectory(skinsDir)

        val samsungDensity = 2.625f
        val samsungW = 914f * samsungDensity
        val samsungH = 411f * samsungDensity

        val pocoDensity = 2.0f
        val pocoW = 820f * pocoDensity
        val pocoH = 360f * pocoDensity

        val cutoutCases = listOf(
            "Cutout Left Samsung (75px)" to Insets(left = 75f, top = 0f, right = 0f, bottom = 0f),
            "Cutout Right Samsung (75px)" to Insets(left = 0f, top = 0f, right = 75f, bottom = 0f),
            "Cutout Left POCO (60px)" to Insets(left = 60f, top = 0f, right = 0f, bottom = 0f),
            "Cutout Right POCO (60px)" to Insets(left = 0f, top = 0f, right = 60f, bottom = 0f)
        )

        for (skin in skins) {
            for ((label, insets) in cutoutCases) {
                val isPoco = label.contains("POCO")
                val w = if (isPoco) pocoW else samsungW
                val h = if (isPoco) pocoH else samsungH
                val density = if (isPoco) pocoDensity else samsungDensity

                val g = SkinLayout.calculate(skin, w, h, density, insets)

                // 1. Top area left margin == right margin +- 1 px
                val topAreaLeftMargin = g.topAreaRect.left
                val topAreaRightMargin = w - g.topAreaRect.right
                assertEquals(
                    "Top area symmetric margins in $label for ${skin.id}",
                    topAreaLeftMargin,
                    topAreaRightMargin,
                    1.0f
                )

                // 2. Keyboard left margin == right margin +- 1 px
                val kbLeftMargin = g.keyboardRect.left
                val kbRightMargin = w - g.keyboardRect.right
                assertEquals(
                    "Keyboard symmetric margins in $label for ${skin.id}",
                    kbLeftMargin,
                    kbRightMargin,
                    1.0f
                )

                // 3. The settings dot stays clear of every control even with cutouts
                assertGearClear(g, "$label for ${skin.id}")
            }
        }
    }

    @Test
    fun testSettingsDotInsideRoundedCorner() {
        val skins = SkinRepository.loadFromDirectory(findSkinsDir())
        // Measured corner radii: Samsung M52 84 px, POCO C75 64 px
        for ((w, h, r) in listOf(Triple(2400f, 1080f, 84f), Triple(1640f, 720f, 64f))) {
            for (skin in skins) {
                val g = SkinLayout.calculate(skin, w, h, w / 914f, Insets(cornerRadius = r))
                val dx = r - g.gear.center.x
                val dy = r - g.gear.center.y
                val reach = kotlin.math.sqrt(dx * dx + dy * dy) + g.gear.radius
                assertTrue("Dot visible inside the ${r}px corner for ${skin.id}: $reach", reach <= r - 2f)
                assertGearClear(g, "corner $r for ${skin.id}")
            }
        }
    }

    @Test
    fun testHitTesting() {
        val skinsDir = findSkinsDir()
        val skin = SkinRepository.loadFromDirectory(skinsDir).first { it.id == "classic" }

        val density = 2.625f
        val w = 914f * density
        val h = 411f * density
        val g = SkinLayout.calculate(skin, w, h, density)

        // Hit gear
        val hitGear = g.hitTest(g.gear.center.x, g.gear.center.y)
        assertEquals(PanelTarget.Gear, hitGear)

        // Hit master pot
        val hitMaster = g.hitTest(g.masterPot.knobCenter.x, g.masterPot.knobCenter.y)
        assertEquals(PanelTarget.MasterPot, hitMaster)

        // Hit Button 0 (FX)
        val btn0 = g.buttons.first { it.index == 0 }
        val hitBtn0 = g.hitTest(btn0.rect.centerX, btn0.rect.centerY)
        assertEquals(PanelTarget.Button(0), hitBtn0)

        // Hit first white key (F3 = bit 0)
        val w0 = g.keys.first { it.bit == 0 }
        val hitW0 = g.hitTest(w0.capsuleRect.centerX, w0.capsuleRect.centerY)
        assertEquals(PanelTarget.Key(0), hitW0)

        // Hit first black key (F#3 = bit 1)
        val b1 = g.keys.first { it.bit == 1 }
        val hitB1 = g.hitTest(b1.capsuleRect.centerX, b1.capsuleRect.centerY)
        assertEquals(PanelTarget.Key(1), hitB1)

        // Hit screen
        val hitScreen = g.hitTest(g.screen.lcdRect.centerX, g.screen.lcdRect.centerY)
        assertEquals(PanelTarget.Screen, hitScreen)
    }
}
