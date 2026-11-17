package io.github.fm1deck.fw

import io.github.fm1deck.settings.AppSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class FirmwareCatalogTest {

    @Test
    fun allTwentyEightFirmwaresHaveUniqueMetadata() {
        assertEquals(28, FirmwareCatalog.builtins.size)
        val ids = FirmwareCatalog.builtins.map { it.id }
        val sonames = FirmwareCatalog.builtins.map { it.soname }
        val shorts = FirmwareCatalog.builtins.map { it.shortName }
        assertEquals(28, ids.toSet().size)
        assertEquals(28, sonames.toSet().size)
        assertEquals(28, shorts.toSet().size)

        for (fw in FirmwareCatalog.builtins) {
            assertTrue("Missing name for ${fw.id}", fw.displayName.isNotBlank())
            assertTrue("Missing version for ${fw.id}", fw.version.isNotBlank())
            assertTrue("Missing author for ${fw.id}", fw.author.isNotBlank())
            assertTrue("Missing description for ${fw.id}", fw.description.isNotBlank())
            assertNotNull(FirmwareCatalog.findById(fw.id))
        }
    }

    @Test
    fun allThumbnailsExistInAssets() {
        val assetsRoot = listOf(
            File("src/main/assets"),
            File("app/src/main/assets")
        ).firstOrNull { it.isDirectory }
        assertNotNull("Could not find assets dir", assetsRoot)
        for (fw in FirmwareCatalog.builtins) {
            val f = File(assetsRoot, fw.thumbAsset)
            assertTrue("Missing thumbnail asset ${fw.thumbAsset}", f.isFile && f.length() > 0)
        }
    }

    @Test
    fun rackSupportsFourSlots() {
        assertEquals(4, AppSettings.RACK_SLOTS)
        val s = AppSettings()
            .withSlot(0, "mutable")
            .withSlot(1, "fmini")
            .withSlot(2, "obervave")
            .withSlot(3, "coralflower")
        assertEquals(listOf("mutable", "fmini", "obervave", "coralflower"), s.rack)
    }
}
