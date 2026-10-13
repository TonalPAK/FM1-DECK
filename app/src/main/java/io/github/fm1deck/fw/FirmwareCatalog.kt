package io.github.fm1deck.fw

data class FirmwareEntry(
    val id: String,
    val displayName: String,
    val soname: String,
    /** Three letters for the rack's narrow cells. */
    val shortName: String = displayName.take(3).uppercase(),
    val isBuiltin: Boolean = true
)

object FirmwareCatalog {
    val FELUCCA = FirmwareEntry("felucca", "Felucca", "libfw_felucca.so", "FEL")
    val JIANT = FirmwareEntry("jiant", "JIANT FM", "libfw_jiant.so", "JNT")
    val SLOOP = FirmwareEntry("sloop", "SLOOP", "libfw_sloop.so", "SLP")
    val HORTATOR = FirmwareEntry("hortator", "Hortator", "libfw_hortator.so", "HRT")

    val builtins: List<FirmwareEntry> = listOf(FELUCCA, JIANT, SLOOP, HORTATOR)

    fun findById(id: String): FirmwareEntry? {
        return builtins.firstOrNull { it.id.equals(id, ignoreCase = true) }
    }

    val defaultFirmware: FirmwareEntry get() = FELUCCA
}
