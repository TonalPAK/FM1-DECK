package io.github.fm1deck.settings

import org.json.JSONObject
import java.io.File

/**
 * Persists and loads AppSettings to/from filesDir/settings.json using org.json.
 * Defaults for missing or unknown fields per docs/SETTINGS.md.
 */
object SettingsStore {

    fun fromJson(jsonStr: String): AppSettings {
        val root = try {
            JSONObject(jsonStr)
        } catch (_: Exception) {
            return AppSettings()
        }

        val rawSkinId = root.optString("activeSkinId", AppSettings.DEFAULT_SKIN_ID)
        val rawFramesOverride = root.optString("framesOverride", AppSettings.FRAMES_OVERRIDE_SKIN)

        val (migratedSkinId, migratedFramesOverride) = when {
            rawSkinId == "classic-framed" -> Pair(AppSettings.DEFAULT_SKIN_ID, AppSettings.FRAMES_OVERRIDE_ON)
            rawSkinId in io.github.fm1deck.ui.panel.skin.SkinRepository.BUILT_IN_IDS -> Pair(rawSkinId, rawFramesOverride)
            else -> Pair(AppSettings.DEFAULT_SKIN_ID, rawFramesOverride)
        }

        return AppSettings(
            schema = root.optInt("schema", AppSettings.CURRENT_SCHEMA),
            activeSkinId = migratedSkinId,
            activePaletteId = root.optString("activePaletteId", AppSettings.DEFAULT_PALETTE_ID),
            framesOverride = migratedFramesOverride,
            keyboardFraction = root.optDouble("keyboardFraction", AppSettings.DEFAULT_KEYBOARD_FRACTION.toDouble()).toFloat()
                .coerceIn(0.30f, 0.45f),
            screenPixelated = root.optBoolean("screenPixelated", AppSettings.DEFAULT_SCREEN_PIXELATED),
            knobSensitivityDp = root.optDouble("knobSensitivityDp", AppSettings.DEFAULT_KNOB_SENSITIVITY_DP.toDouble()).toFloat()
                .coerceIn(3.0f, 12.0f),
            fineModeOnLongPress = root.optBoolean("fineModeOnLongPress", AppSettings.DEFAULT_FINE_MODE),
            glissando = root.optBoolean("glissando", AppSettings.DEFAULT_GLISSANDO),
            hapticsMode = root.optString("hapticsMode", AppSettings.HAPTICS_LIGHT),
            masterAdc = root.optInt("masterAdc", AppSettings.DEFAULT_MASTER_ADC).coerceIn(0, 1023),
            activeFwId = root.optString("activeFwId", AppSettings.DEFAULT_FW_ID),
            masterValues = root.optJSONObject("master")?.let { m ->
                buildMap { for (k in m.keys()) put(k, m.optDouble(k, 0.0).toFloat()) }
            } ?: emptyMap(),
            masterPreset = root.optString("masterPreset", AppSettings.DEFAULT_MASTER_PRESET),
            quality = root.optInt("quality", AppSettings.DEFAULT_QUALITY).let { q ->
                if (q in AppSettings.QUALITIES) q else AppSettings.DEFAULT_QUALITY
            },
            masterScope = root.optBoolean("masterScope", true),
            linkQuantum = root.optInt("linkQuantum", 4).let { q -> if (q in listOf(1, 2, 4, 8)) q else 4 }
        )
    }

    fun toJson(settings: AppSettings): String {
        val root = JSONObject()
        root.put("schema", settings.schema)
        root.put("activeSkinId", settings.activeSkinId)
        root.put("activePaletteId", settings.activePaletteId)
        root.put("framesOverride", settings.framesOverride)
        root.put("keyboardFraction", settings.keyboardFraction.toDouble())
        root.put("screenPixelated", settings.screenPixelated)
        root.put("knobSensitivityDp", settings.knobSensitivityDp.toDouble())
        root.put("fineModeOnLongPress", settings.fineModeOnLongPress)
        root.put("glissando", settings.glissando)
        root.put("hapticsMode", settings.hapticsMode)
        root.put("masterAdc", settings.masterAdc)
        root.put("activeFwId", settings.activeFwId)
        val master = JSONObject()
        for ((k, v) in settings.masterValues.toSortedMap()) master.put(k, v.toDouble())
        root.put("master", master)
        root.put("masterPreset", settings.masterPreset)
        root.put("quality", settings.quality)
        root.put("masterScope", settings.masterScope)
        root.put("linkQuantum", settings.linkQuantum)
        return root.toString(2)
    }

    fun load(file: File): AppSettings {
        if (!file.exists()) return AppSettings()
        return try {
            val json = file.readText()
            fromJson(json)
        } catch (_: Exception) {
            AppSettings()
        }
    }

    fun save(file: File, settings: AppSettings): Boolean {
        return try {
            val parent = file.parentFile
            if (parent != null && !parent.exists()) {
                parent.mkdirs()
            }
            val tmp = File(file.parentFile, "${file.name}.tmp")
            val json = toJson(settings)
            tmp.writeText(json)
            if (file.exists()) {
                file.delete()
            }
            tmp.renameTo(file)
        } catch (_: Exception) {
            false
        }
    }
}
