package io.github.fm1deck.ui.panel.render

import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import io.github.fm1deck.ui.panel.PanelElement
import io.github.fm1deck.ui.panel.skin.Palette

/** The LCD in a near-black bezel (the emulator's #0b0b0c, corner 18/256 of the side). */
object ScreenBlockRenderer {
    fun draw(
        scope: DrawScope,
        screen: PanelElement.Screen,
        hasFrame: Boolean,
        palette: Palette,
        imageBitmap: ImageBitmap,
        pixelated: Boolean,
        u: Float
    ) {
        if (hasFrame) PanelStyle.drawFrame(scope, screen.frameRect, u, palette)
        val r = screen.frameRect.width * (18f / 256f)
        scope.drawRoundRect(
            color = palette.screenFrame,
            topLeft = screen.frameRect.toOffset(),
            size = screen.frameRect.toSize(),
            cornerRadius = CornerRadius(r, r)
        )
        scope.drawRect(color = palette.screenLcdBg, topLeft = screen.lcdRect.toOffset(), size = screen.lcdRect.toSize())
        scope.drawImage(
            image = imageBitmap,
            dstOffset = IntOffset(screen.lcdRect.left.toInt(), screen.lcdRect.top.toInt()),
            dstSize = IntSize(screen.lcdRect.width.toInt(), screen.lcdRect.height.toInt()),
            filterQuality = if (pixelated) FilterQuality.None else FilterQuality.Low
        )
    }
}
