package io.github.fm1deck.ui.panel.skin

import android.content.res.AssetManager
import androidx.compose.ui.graphics.Color
import org.json.JSONObject
import java.io.File

/**
 * Color tokens for skin rendering per docs/SKINS.md §6.
 */
data class Palette(
    val id: String,
    val nameEn: String,
    val background: Color,
    val panel: Color,
    val frame: Color,
    val button: Color,
    val buttonPressed: Color,
    val buttonTopHighlight: Color = button,
    val buttonBottomShadow: Color = buttonPressed,
    val buttonTray: Color = background,
    val buttonLabel: Color,
    val buttonLabelDim: Color,
    val ledLit: Color,
    val ledDim: Color,
    val ledBreath: Color,
    val select: Color,
    val rec: Color,
    val playGreen: Color,
    val keyBody: Color = Color(0xFF1F1F1F),
    val keyInner: Color = Color(0xFF141414),
    val keyWhite: Color,
    val keyBlack: Color,
    val keyBorder: Color,
    val keyBar: Color,
    val keyLit: Color,
    // Key slit levels between off (keyBar) and lit (keyLit), as the browser emulator draws them
    val keySlitDim: Color = keyBar,
    val keySlitMid: Color = keyBar,
    val keySlitBreath: Color = keyLit,
    val knobBody: Color,
    val knobRim: Color = Color(0xFF181818),
    val knobRing: Color,
    val knobIndicator: Color,
    val knobGlow: Color = select,
    val screenFrame: Color,
    val screenLcdBg: Color,
    val hintText: Color
)

object PaletteRepository {
    // Default built-in Graphite palette: the browser emulator's tokens (upstream felucca web/emu/index.html :root),
    // which the reference screenshots show. Kept equal to graphite in assets/skins/palettes.json.
    val GRAPHITE = Palette(
        id = "graphite",
        nameEn = "Graphite",
        background = parseColorHex("#1D1D1F"),
        panel = parseColorHex("#1D1D1F"),
        frame = parseColorHex("#343437"),
        button = parseColorHex("#2A2A2D"),
        buttonPressed = parseColorHex("#232325"),
        buttonTopHighlight = parseColorHex("#3A3A3E"),
        buttonBottomShadow = parseColorHex("#0B0B0C"),
        buttonTray = parseColorHex("#141415"),
        buttonLabel = parseColorHex("#D6D6D8"),
        buttonLabelDim = parseColorHex("#8A8A8E"),
        ledLit = parseColorHex("#FFF4D6"),
        ledDim = parseColorHex("#3A3934"),
        ledBreath = parseColorHex("#D8CFB6"),
        select = parseColorHex("#79B8FF"),
        rec = parseColorHex("#FF453A"),
        playGreen = parseColorHex("#5FE08A"),
        keyBody = parseColorHex("#0C0C0D"),
        keyInner = parseColorHex("#0C0C0D"),
        keyWhite = parseColorHex("#0C0C0D"),
        keyBlack = parseColorHex("#0C0C0D"),
        keyBorder = parseColorHex("#3A3A3E"),
        keyBar = parseColorHex("#3A3A3C"),
        keyLit = parseColorHex("#ECECEE"),
        keySlitDim = parseColorHex("#77777B"),
        keySlitMid = parseColorHex("#9A9A9E"),
        keySlitBreath = parseColorHex("#C8C8CA"),
        knobBody = parseColorHex("#2A2A2D"),
        knobRim = parseColorHex("#141415"),
        knobRing = parseColorHex("#3A3A3E"),
        knobIndicator = parseColorHex("#D6D6D8"),
        knobGlow = parseColorHex("#FFF4D6"),
        screenFrame = parseColorHex("#0B0B0C"),
        screenLcdBg = parseColorHex("#000000"),
        hintText = parseColorHex("#FFFFFF")
    )

    fun parseColorHex(hex: String): Color {
        val clean = hex.removePrefix("#").trim()
        val argb = when (clean.length) {
            6 -> 0xFF000000L or clean.toLong(16)
            8 -> clean.toLong(16)
            else -> 0xFF000000L
        }
        return Color(argb.toInt())
    }

    fun parsePalettes(jsonString: String): Map<String, Palette> {
        val root = JSONObject(jsonString)
        val palettesObj = root.optJSONObject("palettes") ?: return mapOf("graphite" to GRAPHITE)
        val result = mutableMapOf<String, Palette>()

        for (id in palettesObj.keys()) {
            val obj = palettesObj.getJSONObject(id)
            val nameObj = obj.optJSONObject("name")
            val nameEn = nameObj?.optString("en", id) ?: id

            fun getColor(token: String, default: Color): Color {
                if (!obj.has(token)) return default
                val hex = obj.getString(token)
                return runCatching { parseColorHex(hex) }.getOrDefault(default)
            }

            val bg = getColor("background", GRAPHITE.background)
            val btn = getColor("button", GRAPHITE.button)
            val btnPressed = getColor("buttonPressed", GRAPHITE.buttonPressed)
            val sel = getColor("select", GRAPHITE.select)
            val kw = getColor("keyWhite", GRAPHITE.keyWhite)
            val kb = getColor("keyBlack", GRAPHITE.keyBlack)

            result[id] = Palette(
                id = id,
                nameEn = nameEn,
                background = bg,
                panel = getColor("panel", GRAPHITE.panel),
                frame = getColor("frame", GRAPHITE.frame),
                button = btn,
                buttonPressed = btnPressed,
                buttonTopHighlight = getColor("buttonTopHighlight", btn),
                buttonBottomShadow = getColor("buttonBottomShadow", btnPressed),
                buttonTray = getColor("buttonTray", bg),
                buttonLabel = getColor("buttonLabel", GRAPHITE.buttonLabel),
                buttonLabelDim = getColor("buttonLabelDim", GRAPHITE.buttonLabelDim),
                ledLit = getColor("ledLit", GRAPHITE.ledLit),
                ledDim = getColor("ledDim", GRAPHITE.ledDim),
                ledBreath = getColor("ledBreath", GRAPHITE.ledBreath),
                select = sel,
                rec = getColor("rec", GRAPHITE.rec),
                playGreen = getColor("playGreen", GRAPHITE.playGreen),
                keyBody = getColor("keyBody", kw),
                keyInner = getColor("keyInner", kb),
                keyWhite = kw,
                keyBlack = kb,
                keyBorder = getColor("keyBorder", GRAPHITE.keyBorder),
                keyBar = getColor("keyBar", GRAPHITE.keyBar),
                keyLit = getColor("keyLit", GRAPHITE.keyLit),
                keySlitDim = getColor("keySlitDim", GRAPHITE.keySlitDim),
                keySlitMid = getColor("keySlitMid", GRAPHITE.keySlitMid),
                keySlitBreath = getColor("keySlitBreath", GRAPHITE.keySlitBreath),
                knobBody = getColor("knobBody", GRAPHITE.knobBody),
                knobRim = getColor("knobRim", getColor("knobRing", GRAPHITE.knobRim)),
                knobRing = getColor("knobRing", GRAPHITE.knobRing),
                knobIndicator = getColor("knobIndicator", GRAPHITE.knobIndicator),
                knobGlow = getColor("knobGlow", sel),
                screenFrame = getColor("screenFrame", GRAPHITE.screenFrame),
                screenLcdBg = getColor("screenLcdBg", GRAPHITE.screenLcdBg),
                hintText = getColor("hintText", GRAPHITE.hintText)
            )
        }

        return result
    }

    fun loadFromAssets(assetManager: AssetManager): Map<String, Palette> {
        return runCatching {
            val json = assetManager.open("skins/palettes.json").bufferedReader().use { it.readText() }
            parsePalettes(json)
        }.getOrDefault(mapOf("graphite" to GRAPHITE))
    }

    fun loadFromFile(file: File): Map<String, Palette> {
        return runCatching {
            if (!file.exists()) return mapOf("graphite" to GRAPHITE)
            parsePalettes(file.readText())
        }.getOrDefault(mapOf("graphite" to GRAPHITE))
    }
}
