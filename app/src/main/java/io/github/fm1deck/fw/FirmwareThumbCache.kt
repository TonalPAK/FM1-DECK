package io.github.fm1deck.fw

import android.content.res.AssetManager
import android.graphics.BitmapFactory
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.concurrent.ConcurrentHashMap

object FirmwareThumbCache {
    private val cache = ConcurrentHashMap<String, ImageBitmap>()

    fun getCached(assetPath: String): ImageBitmap? = cache[assetPath]

    suspend fun load(assets: AssetManager, assetPath: String): ImageBitmap? {
        cache[assetPath]?.let { return it }
        return withContext(Dispatchers.IO) {
            cache[assetPath]?.let { return@withContext it }
            runCatching {
                assets.open(assetPath).use { stream ->
                    BitmapFactory.decodeStream(stream)?.asImageBitmap()
                }
            }.getOrNull()?.also { bmp ->
                cache[assetPath] = bmp
            }
        }
    }
}

@Composable
fun rememberFirmwareThumb(assetPath: String): ImageBitmap? {
    val context = LocalContext.current
    val bitmap by produceState<ImageBitmap?>(
        initialValue = FirmwareThumbCache.getCached(assetPath),
        key1 = assetPath
    ) {
        if (value == null) {
            value = FirmwareThumbCache.load(context.assets, assetPath)
        }
    }
    return bitmap
}
