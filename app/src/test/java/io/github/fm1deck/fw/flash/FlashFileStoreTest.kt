package io.github.fm1deck.fw.flash

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class FlashFileStoreTest {
    @get:Rule
    val tempFolder = TemporaryFolder()

    @Test
    fun testRoundTrip() {
        val store = FlashFileStore(tempFolder.root)
        val data = ByteArray(1024) { (it % 256).toByte() }
        val ok = store.saveNor("test", data, 1024)
        assertTrue(ok)

        val loaded = store.loadNor("test", 1024)
        assertNotNull(loaded)
        assertArrayEquals(data, loaded)
    }

    @Test
    fun testWrongSizeRejected() {
        val store = FlashFileStore(tempFolder.root)
        val data = ByteArray(512)

        // Attempting to save with expectedSize mismatch
        val ok = store.saveNor("test", data, 1024)
        assertFalse(ok)

        // Loading non-matching size returns null
        assertNull(store.loadNor("test", 1024))
    }

    @Test
    fun testFailedWriteKeepsPreviousFileAndCleansTmp() {
        val store = FlashFileStore(tempFolder.root)
        val original = ByteArray(1024) { 0x42.toByte() }
        assertTrue(store.saveNor("test", original, 1024))

        // Corrupted / bad write with wrong size fails
        val bad = ByteArray(500)
        assertFalse(store.saveNor("test", bad, 1024))

        // Previous file must remain intact
        val loaded = store.loadNor("test", 1024)
        assertNotNull(loaded)
        assertArrayEquals(original, loaded)

        // No tmp file left behind
        val tmpFile = File(store.getNorFile("test").parentFile, "nor.bin.tmp")
        assertFalse(tmpFile.exists())
    }
}
