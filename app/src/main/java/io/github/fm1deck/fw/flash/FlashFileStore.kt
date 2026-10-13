package io.github.fm1deck.fw.flash

import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * Low-level atomic file storage for SPI NOR flash images.
 * Factored out for deterministic host unit testing without Android Context, Log, or JNI dependencies.
 */
class FlashFileStore(
    val baseDir: File,
    private val logger: ((String) -> Unit)? = null
) {
    fun getNorFile(id: String): File {
        val dir = File(baseDir, "flash/$id")
        if (!dir.exists()) dir.mkdirs()
        return File(dir, "nor.bin")
    }

    fun loadNor(id: String, expectedSize: Int): ByteArray? {
        val file = getNorFile(id)
        if (!file.exists() || file.length() != expectedSize.toLong()) {
            return null
        }
        return try {
            file.readBytes()
        } catch (e: Exception) {
            logger?.invoke("Failed to read NOR file for $id: ${e.message}")
            null
        }
    }

    fun saveNor(id: String, data: ByteArray, expectedSize: Int): Boolean {
        if (data.size != expectedSize) {
            logger?.invoke("Invalid NOR image size for $id: ${data.size} != $expectedSize")
            return false
        }
        val target = getNorFile(id)
        val tmp = File(target.parentFile, "nor.bin.tmp")

        return try {
            FileOutputStream(tmp).use { fos ->
                fos.write(data)
                fos.flush()
                fos.fd.sync()
            }
            Files.move(tmp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            logger?.invoke("Saved NOR flash for $id (${data.size} bytes)")
            true
        } catch (e: Exception) {
            logger?.invoke("Failed to write NOR flash for $id: ${e.message}")
            if (tmp.exists()) tmp.delete()
            false
        }
    }
}
