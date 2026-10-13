package io.github.fm1deck.engine

import org.json.JSONObject

/** One row of FM1_MASTER_PARAMS (MasterBusParams.h), read at runtime: no hand-copied mirror. */
data class MasterParam(
    val id: Int,
    val key: String,
    val group: String,
    val unit: String,
    val min: Float,
    val max: Float,
    val default: Float,
    val stepped: Boolean
) {
    fun clamp(v: Float): Float = v.coerceIn(min, max)
    val isSwitch: Boolean get() = stepped && min == 0f && max == 1f
}

/** Meter indices of FM1_MASTER_METERS. */
object MasterMeter {
    const val IN_PEAK_L = 0
    const val IN_PEAK_R = 1
    const val IN_RMS = 2
    const val SAT_AMOUNT = 3
    const val SPC_GR = 4
    const val LIM_GR = 5
    const val CLIP_GR = 6
    const val OUT_PEAK_L = 7
    const val OUT_PEAK_R = 8
    const val OUT_TRUE_PEAK = 9
    const val OUT_LUFS_S = 10
    const val DSP_LOAD = 11
    const val COUNT = 12
}

/** A saved state of the bus: values by key name (robust to future parameters), MASTER-BUS.md §6. */
data class MasterPreset(val name: String, val values: Map<String, Float>, val builtIn: Boolean) {
    fun toJson(): String {
        val v = JSONObject()
        for ((k, x) in values.toSortedMap()) v.put(k, x.toDouble())
        return JSONObject().put("schema", 1).put("name", name).put("values", v).toString(2)
    }

    companion object {
        fun fromJson(json: String, builtIn: Boolean): MasterPreset {
            val root = JSONObject(json)
            val name = root.getString("name")
            val v = root.optJSONObject("values") ?: JSONObject()
            val values = buildMap { for (k in v.keys()) put(k, v.getDouble(k).toFloat()) }
            return MasterPreset(name, values, builtIn)
        }
    }
}

/**
 * The master bus seen from Kotlin: the parameter table, the current values by key, and sending them to native.
 * Pure parts (parsing, preset resolution) are host-testable; native calls are confined to [sendAll] / [send].
 */
object MasterControl {
    /** Parses nativeMasterParamTable's rows and names. */
    fun parseTable(rows: FloatArray, names: Array<String?>): List<MasterParam> =
        names.indices.mapNotNull { i ->
            val parts = names[i]?.split('|') ?: return@mapNotNull null
            if (parts.size < 3 || rows.size < i * 5 + 5) return@mapNotNull null
            MasterParam(
                id = rows[i * 5].toInt(),
                key = parts[0],
                group = parts[1],
                unit = parts[2],
                min = rows[i * 5 + 1],
                max = rows[i * 5 + 2],
                default = rows[i * 5 + 3],
                stepped = rows[i * 5 + 4] >= 0.5f
            )
        }

    val params: List<MasterParam> by lazy {
        val count = NativeBridge.nativeMasterParamCount()
        val names = arrayOfNulls<String>(count)
        val rows = NativeBridge.nativeMasterParamTable(names)
        parseTable(rows, names)
    }

    fun byKey(key: String): MasterParam? = params.firstOrNull { it.key == key }

    /**
     * The full state for a set of stored values: every parameter, the stored value where present (clamped),
     * the default otherwise.
     */
    fun resolve(table: List<MasterParam>, stored: Map<String, Float>): Map<String, Float> =
        table.associate { p -> p.key to (stored[p.key]?.let { p.clamp(it) } ?: p.default) }

    /** A preset applied as a complete state: listed keys from the preset, the rest at their defaults. */
    fun applyPreset(table: List<MasterParam>, preset: MasterPreset): Map<String, Float> = resolve(table, preset.values)

    /** Sends every value and the quality factor (the native bus keeps them across stream restarts). */
    fun sendAll(values: Map<String, Float>, quality: Int) {
        for (p in params) NativeBridge.nativeMasterSet(p.id, values[p.key] ?: p.default)
        NativeBridge.nativeMasterSetQuality(quality)
    }

    fun send(key: String, value: Float) {
        val p = byKey(key) ?: return
        NativeBridge.nativeMasterSet(p.id, p.clamp(value))
    }

    fun readMeters(out: FloatArray) = NativeBridge.nativeMasterMeters(out)
    fun latencyFrames(): Int = NativeBridge.nativeMasterLatency()
    fun readScope(since: Long, out: FloatArray): Long = NativeBridge.nativeMasterScope(since, out)
    fun setScopeEnabled(on: Boolean) = NativeBridge.nativeMasterScopeEnable(on)

    /** Fields per scope entry (MasterBus::kScopeFields). */
    const val SCOPE_FIELDS = 3
}
