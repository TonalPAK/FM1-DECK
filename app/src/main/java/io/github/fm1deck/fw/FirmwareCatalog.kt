package io.github.fm1deck.fw

data class FirmwareEntry(
    val id: String,
    val displayName: String,
    val soname: String,
    val isBuiltin: Boolean = true
)

object FirmwareCatalog {
    val FELUCCA = FirmwareEntry("felucca", "Felucca", "libfw_felucca.so")
    val JIANT = FirmwareEntry("jiant", "JIANT FM", "libfw_jiant.so")
    val SLOOP = FirmwareEntry("sloop", "SLOOP", "libfw_sloop.so")

    val builtins: List<FirmwareEntry> = listOf(FELUCCA, JIANT, SLOOP)

    fun findById(id: String): FirmwareEntry? {
        return builtins.firstOrNull { it.id.equals(id, ignoreCase = true) }
    }

    val defaultFirmware: FirmwareEntry get() = FELUCCA
}
