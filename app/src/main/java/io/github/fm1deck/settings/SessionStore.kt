package io.github.fm1deck.settings

import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * A live session: the whole rack under a name. Its folder (filesDir/sessions/<id>) holds session.json (the rack,
 * each slot's MASTER, focus, tempo, mute / solo) and one flash image per firmware in the rack (<firmware id>.bin),
 * so loading it brings every machine back as it was saved.
 * Plain files, no Android: the control executor does the copying (AudioController.saveSession / loadSession).
 */
data class Session(
    val id: String,
    val name: String,
    val savedAt: Long,
    val rack: List<String>,
    val rackMasters: List<Int>,
    val rackFocus: Int,
    val rackTempo: Double,
    val rackMute: Int,
    val rackSolo: Int
) {
    /** These settings with the session's rack in place (skin, master bus and the rest stay as they are). */
    fun applyTo(settings: AppSettings): AppSettings = settings.copy(
        rack = rack, rackMasters = rackMasters, rackFocus = rackFocus, rackTempo = rackTempo,
        rackMute = rackMute, rackSolo = rackSolo
    ).normalized()

    fun toJson(): String = JSONObject().apply {
        put("schema", 1)
        put("name", name)
        put("savedAt", savedAt)
        put("rack", JSONArray(rack))
        put("rackMasters", JSONArray(rackMasters))
        put("rackFocus", rackFocus)
        put("rackTempo", rackTempo)
        put("rackMute", rackMute)
        put("rackSolo", rackSolo)
    }.toString(2)

    companion object {
        fun of(id: String, name: String, s: AppSettings, now: Long) = Session(
            id = id, name = name, savedAt = now, rack = s.rack,
            // the focused slot's MASTER is masterAdc (the panel writes it there)
            rackMasters = s.rackMasters.toMutableList().also { if (s.rackFocus in it.indices) it[s.rackFocus] = s.masterAdc },
            rackFocus = s.rackFocus, rackTempo = s.rackTempo, rackMute = s.rackMute, rackSolo = s.rackSolo
        )

        fun fromJson(id: String, json: String): Session? = runCatching {
            val o = JSONObject(json)
            val rack = o.getJSONArray("rack").let { a -> List(a.length()) { a.optString(it, "") } }
            val masters = o.optJSONArray("rackMasters")?.let { a -> List(a.length()) { a.optInt(it, AppSettings.DEFAULT_MASTER_ADC) } }
                ?: emptyList()
            Session(
                id = id,
                name = o.optString("name", id),
                savedAt = o.optLong("savedAt", 0L),
                rack = rack,
                rackMasters = masters,
                rackFocus = o.optInt("rackFocus", 0),
                rackTempo = o.optDouble("rackTempo", 120.0).coerceIn(20.0, 999.0),
                rackMute = o.optInt("rackMute", 0),
                rackSolo = o.optInt("rackSolo", 0)
            )
        }.getOrNull()
    }
}

class SessionStore(private val baseDir: File) {
    private val root get() = File(baseDir, "sessions")

    fun dir(id: String): File = File(root, id)

    /** Saved sessions, newest first. */
    fun list(): List<Session> =
        (root.listFiles { f -> f.isDirectory } ?: emptyArray()).mapNotNull { d ->
            val f = File(d, FILE)
            if (f.exists()) Session.fromJson(d.name, f.readText()) else null
        }.sortedByDescending { it.savedAt }

    /** The session called [name] (case-insensitive), to save over it. */
    fun findByName(name: String): Session? = list().firstOrNull { it.name.equals(name.trim(), ignoreCase = true) }

    /** A folder name for a new session (its name may hold anything; the folder only safe characters). */
    fun newId(name: String, now: Long): String {
        val safe = name.trim().replace(Regex("[^A-Za-z0-9_-]"), "_").take(24).ifEmpty { "session" }
        return "${now}_$safe"
    }

    fun writeMeta(session: Session) {
        val d = dir(session.id).apply { mkdirs() }
        val tmp = File(d, "$FILE.tmp")
        tmp.writeText(session.toJson())
        val target = File(d, FILE)
        if (target.exists()) target.delete()
        tmp.renameTo(target)
    }

    fun flashFile(id: String, fwId: String): File = File(dir(id), "$fwId.bin")

    fun delete(id: String): Boolean = dir(id).deleteRecursively()

    /** The next free "SESSION nn" name. */
    fun nextName(): String {
        val used = list().map { it.name.uppercase() }.toSet()
        var n = 1
        while ("SESSION %02d".format(n) in used) n++
        return "SESSION %02d".format(n)
    }

    companion object {
        const val FILE = "session.json"
    }
}
