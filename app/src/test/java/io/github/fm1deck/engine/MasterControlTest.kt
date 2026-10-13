package io.github.fm1deck.engine

import io.github.fm1deck.settings.AppSettings
import io.github.fm1deck.settings.SettingsStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class MasterControlTest {

    private fun repoFile(path: String): File =
        listOf(File(path), File("app/$path"), File("../app/$path")).firstOrNull { it.exists() }
            ?: throw IllegalStateException("not found: $path")

    /** The parameter table straight from MasterBusParams.h (the same X-macro the native side compiles). */
    private fun headerTable(): List<MasterParam> {
        val text = repoFile("src/main/cpp/dsp/MasterBusParams.h").readText()
        val row = Regex("""X\((\d+),\s*(\w+),\s*(\w+),\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*"([^"]*)",\s*([CS])\)""")
        return row.findAll(text).map { m ->
            val g = m.groupValues
            MasterParam(g[1].toInt(), g[2], g[3], g[7], g[4].toFloat(), g[5].toFloat(), g[6].toFloat(), g[8] == "S")
        }.toList()
    }

    @Test
    fun parseTableReadsRowsAndNames() {
        val rows = floatArrayOf(11f, 0f, 36f, 6f, 0f, 32f, 0.5f, 5f, 1.5f, 1f)
        val names = arrayOf<String?>("SAT_DRIVE|SAT|dB", "LIM_LOOKAHEAD|LIMITER|ms")
        val t = MasterControl.parseTable(rows, names)
        assertEquals(2, t.size)
        assertEquals(MasterParam(11, "SAT_DRIVE", "SAT", "dB", 0f, 36f, 6f, false), t[0])
        assertEquals(true, t[1].stepped)
    }

    @Test
    fun headerHasTheDocumentedParameters() {
        val t = headerTable()
        assertTrue("table parsed", t.size >= 30)
        assertEquals("ids unique", t.size, t.map { it.id }.toSet().size)
        assertEquals("keys unique", t.size, t.map { it.key }.toSet().size)
        for (p in t) assertTrue("${p.key} default in range", p.default in p.min..p.max)
    }

    @Test
    fun resolveClampsAndFillsDefaults() {
        val t = headerTable()
        val r = MasterControl.resolve(t, mapOf("SAT_DRIVE" to 99f, "UNKNOWN_KEY" to 1f))
        assertEquals(t.size, r.size)
        assertEquals(36f, r["SAT_DRIVE"])
        assertEquals(t.first { it.key == "LIM_CEILING" }.default, r["LIM_CEILING"])
        assertTrue("unknown keys dropped", "UNKNOWN_KEY" !in r)
    }

    @Test
    fun builtInPresetsUseOnlyKnownKeysInRange() {
        val t = headerTable().associateBy { it.key }
        val dir = repoFile("src/main/assets/presets/master")
        val files = dir.listFiles { f -> f.name.endsWith(".json") }!!.sortedBy { it.name }
        val names = files.map { MasterPreset.fromJson(it.readText(), builtIn = true).name }
        assertEquals(listOf("Default", "Clean", "Warm", "Punch", "Loud", "Crush", "Glue"), names)
        for (f in files) {
            val p = MasterPreset.fromJson(f.readText(), builtIn = true)
            for ((k, v) in p.values) {
                val param = t[k] ?: throw AssertionError("${p.name}: unknown key $k")
                assertTrue("${p.name}: $k = $v in range", v in param.min..param.max)
            }
        }
        // Clean = everything off but the limiter (MASTER-BUS §6)
        val clean = MasterControl.applyPreset(headerTable(), MasterPreset.fromJson(files[1].readText(), true))
        assertEquals(0f, clean["SAT_ENABLE"]); assertEquals(0f, clean["SPC_ENABLE"]); assertEquals(0f, clean["CLIP_ENABLE"])
        assertEquals(1f, clean["LIM_ENABLE"])
    }

    @Test
    fun presetJsonRoundTrip() {
        val p = MasterPreset("Mine", mapOf("SAT_DRIVE" to 9.5f, "MIX" to 0.75f), builtIn = false)
        assertEquals(p, MasterPreset.fromJson(p.toJson(), builtIn = false))
    }

    @Test
    fun settingsKeepMasterValuesPresetAndQuality() {
        val s = AppSettings(masterValues = mapOf("SAT_DRIVE" to 12f, "BYPASS" to 1f), masterPreset = "Loud", quality = 4)
        val back = SettingsStore.fromJson(SettingsStore.toJson(s))
        assertEquals(s.masterValues, back.masterValues)
        assertEquals("Loud", back.masterPreset)
        assertEquals(4, back.quality)
        // An invalid quality falls back to the default
        assertEquals(AppSettings.DEFAULT_QUALITY, SettingsStore.fromJson("""{"quality": 3}""").quality)
    }
}
