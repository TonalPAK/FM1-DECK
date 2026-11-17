package io.github.fm1deck.fw

data class FirmwareEntry(
    val id: String,
    val displayName: String,
    val soname: String,
    /** Three letters for the rack's narrow cells. */
    val shortName: String = displayName.take(3).uppercase(),
    val version: String = "",
    val author: String = "",
    val tags: List<String> = emptyList(),
    val description: String = "",
    val thumbAsset: String = "fw_thumbs/$id.webp",
    val accentColorHex: Long = 0xFF6FA8DC,
    val repoUrl: String = "",
    val usesSelLabel: Boolean = false,
    val isBuiltin: Boolean = true
)

object FirmwareCatalog {
    val MUTABLE = FirmwareEntry(
        id = "mutable",
        displayName = "Mutable Dreams",
        soname = "libfw_mutable.so",
        shortName = "MUT",
        version = "2.3",
        author = "Kid Bunka",
        tags = listOf("GROOVEBOX", "SYNTH"),
        description = "Generative modular instrument: 6 tracks, 15 engines (Plaits, Rings, Elements), 288 tones and 92 Biomes.",
        accentColorHex = 0xFFE88D67,
        repoUrl = "https://github.com/kidbee85-beep/mutable-dreams",
        usesSelLabel = true
    )

    val FMINI = FirmwareEntry(
        id = "fmini",
        displayName = "FMini-1",
        soname = "libfw_fmini.so",
        shortName = "FMN",
        version = "2.6.0",
        author = "Ruppert",
        tags = listOf("SYNTH", "VINTAGE"),
        description = "Three-oscillator monosynth after classic Moogs: 24 dB/oct ladder filter, 100 patches, arp and sequencer.",
        accentColorHex = 0xFFF2B66D,
        repoUrl = "https://github.com/ruppert/fmini-1"
    )

    val OBERVAVE = FirmwareEntry(
        id = "obervave",
        displayName = "OberVave",
        soname = "libfw_obervave.so",
        shortName = "OBV",
        version = "3.5.2",
        author = "zvenson",
        tags = listOf("SYNTH", "VINTAGE"),
        description = "8-voice analog-style poly after 1979–84 classics on 4 tracks with chorus, delay, plate reverb, DJ filter and comp.",
        accentColorHex = 0xFF4C8DF6,
        repoUrl = "https://github.com/zvenson/obervave",
        usesSelLabel = true
    )

    val CORALFLOWER = FirmwareEntry(
        id = "coralflower",
        displayName = "CoralFlower",
        soname = "libfw_coralflower.so",
        shortName = "CRL",
        version = "0.7.1",
        author = "FicusFicusov",
        tags = listOf("BETA", "CHORDS"),
        description = "Orchid-inspired chord synth: one key plays a chord with bass, 27 scales, arp, drum sequencer and growing pixel flower.",
        accentColorHex = 0xFFFF66B2,
        repoUrl = "https://github.com/FicusFicusov/CoralFlower"
    )

    val AMB = FirmwareEntry(
        id = "amb",
        displayName = "AMB-1",
        soname = "libfw_amb.so",
        shortName = "AMB",
        version = "1.3.2",
        author = "Charles Vestal",
        tags = listOf("SYNTH", "AMBIENT"),
        description = "Generative ambient machine after Music for Airports: breathing voices, bells on unequal tape loops and gliding bass.",
        accentColorHex = 0xFFE6C87A,
        repoUrl = "https://github.com/charlesvestal/fm1-amb",
        usesSelLabel = true
    )

    val BLOP = FirmwareEntry(
        id = "blop",
        displayName = "Blop",
        soname = "libfw_blop.so",
        shortName = "BLP",
        version = "1.1.0",
        author = "Obsimo",
        tags = listOf("CHORDS"),
        description = "One-finger chords from 38 grids, chord rhythms, bass riffs, sidechained drums and an animated screen aquarium.",
        accentColorHex = 0xFF5674FF,
        repoUrl = "https://github.com/OSR-Experiment/OSR-1",
        usesSelLabel = true
    )

    val LOAFBREAKER = FirmwareEntry(
        id = "loafbreaker",
        displayName = "LOAF BREAKER",
        soname = "libfw_loafbreaker.so",
        shortName = "LOF",
        version = "0.9.66",
        author = "FLEXOR",
        tags = listOf("BETA", "DRUMS"),
        description = "Hardcore kick synthesizer (gabber, terror, speedcore) and breakbeat chopper with 20 kicks and distortion.",
        accentColorHex = 0xFFFF8470,
        repoUrl = "https://github.com/FLEXOR-R/loaf-breaker",
        usesSelLabel = true
    )

    val FRASIL = FirmwareEntry(
        id = "frasil",
        displayName = "FRASIL",
        soname = "libfw_frasil.so",
        shortName = "FRS",
        version = "3.3.1",
        author = "Styves Goupil",
        tags = listOf("SYNTH", "AMBIENT"),
        description = "Ambient drone instrument: latched chords/drones into shimmer freeze reverb, icy GLINT bells and deep bass.",
        thumbAsset = "fw_thumbs/frasil.png",
        accentColorHex = 0xFF42C6FF,
        repoUrl = "https://github.com/styvesgoupil/frasil",
        usesSelLabel = true
    )

    val ATEK303 = FirmwareEntry(
        id = "atek303",
        displayName = "ATEK303",
        soname = "libfw_atek303.so",
        shortName = "303",
        version = "1.2",
        author = "Animatek",
        tags = listOf("SYNTH"),
        description = "TB-303 acid synth with Open303 diode ladder filter, 64-step pattern generator, mutation, drive, chorus and reverb.",
        accentColorHex = 0xFF2C7FFF,
        repoUrl = "https://github.com/animatek/FM1-Animatek",
        usesSelLabel = true
    )

    val GURU600 = FirmwareEntry(
        id = "guru600",
        displayName = "Guru-600",
        soname = "libfw_guru600.so",
        shortName = "G60",
        version = "1.3.0",
        author = "Marc Massé",
        tags = listOf("SYNTH"),
        description = "Prophet-600 inspired synth with 255 sounds, 4 tracks, sequencer, arpeggiator, morph and dual insert effects.",
        accentColorHex = 0xFFD65440,
        repoUrl = "https://github.com/MarcGoudy/guru-600"
    )

    val RAVEL = FirmwareEntry(
        id = "ravel",
        displayName = "Ravel",
        soname = "libfw_ravel.so",
        shortName = "RVL",
        version = "1.42.0",
        author = "De-Pattern",
        tags = listOf("GROOVEBOX"),
        description = "Eight-track sound-design groovebox: 13 synth engines, struck percussion, 22 drum machines and deep modulation.",
        accentColorHex = 0xFFE66B8A,
        repoUrl = "https://github.com/De-Pattern/Ravel_FM1"
    )

    val S1VAVE = FirmwareEntry(
        id = "s1vave",
        displayName = "S1Vave",
        soname = "libfw_s1vave.so",
        shortName = "S1V",
        version = "2.0.1",
        author = "Andrew GG",
        tags = listOf("SYNTH", "VINTAGE"),
        description = "8-voice virtual analog tweak synth: 4-pole resonant filter, dual sub-oscillators, ADSR envelopes and 64-step seq.",
        accentColorHex = 0xFF34D399,
        repoUrl = "https://github.com/AndrewGG88/S1Vave",
        usesSelLabel = true
    )

    val CLIPPER = FirmwareEntry(
        id = "clipper",
        displayName = "Clipper",
        soname = "libfw_clipper.so",
        shortName = "CLP",
        version = "1.3.1",
        author = "isod89",
        tags = listOf("SYNTH", "SLOOP"),
        description = "Deep synth grown out of SLOOP: 23 engines and 407 sounds from HYBRID 3-osc to 6-op FM, wavetables and Juno.",
        accentColorHex = 0xFF4F8FF7,
        repoUrl = "https://github.com/isod89/clipper-fm1",
        usesSelLabel = true
    )

    val JAMTRACK = FirmwareEntry(
        id = "jamtrack",
        displayName = "JAMTRACK",
        soname = "libfw_jamtrack.so",
        shortName = "JAM",
        version = "0.2.3",
        author = "PhilipsKnobs",
        tags = listOf("BETA", "GROOVEBOX"),
        description = "Backing band groovebox: drums, bass, chords and synth follow your progression across 160 factory jams in 16 genres.",
        accentColorHex = 0xFFF5A642,
        repoUrl = "https://github.com/Philip-Bos/jamtrack-fm1",
        usesSelLabel = true
    )

    val FIMBA = FirmwareEntry(
        id = "fimba",
        displayName = "FiMba-1",
        soname = "libfw_fimba.so",
        shortName = "FMB",
        version = "0.7.0",
        author = "jadamsowers",
        tags = listOf("BETA", "SYNTH"),
        description = "Physically modelled kalimba: tines laid out like the real instrument, thumb-roll chords, mbira patterns and plate reverb.",
        accentColorHex = 0xFFF2AA7E,
        repoUrl = "https://github.com/jadamsowers/fm1-fimba",
        usesSelLabel = true
    )

    val AISLING = FirmwareEntry(
        id = "aisling",
        displayName = "AISLING",
        soname = "libfw_aisling.so",
        shortName = "AIS",
        version = "1.4.1",
        author = "CU_STE",
        tags = listOf("SYNTH", "AMBIENT"),
        description = "Generative Irish ambient instrument: harps in a jig's threes and flute over drone with surf, wind and rain.",
        accentColorHex = 0xFF34C77B,
        repoUrl = "https://github.com/stephencummins/fm1-aisling",
        usesSelLabel = true
    )

    val ECHOFIELD = FirmwareEntry(
        id = "echofield",
        displayName = "ECHOFIELD",
        soname = "libfw_echofield.so",
        shortName = "ECH",
        version = "0.2.13",
        author = "PhilipsKnobs",
        tags = listOf("BETA", "SYNTH"),
        description = "Granular sampler: turns seconds of any sound into slow clouds, sparkles, tape loops and swells with 14 factory sounds.",
        accentColorHex = 0xFF5D6BFF,
        repoUrl = "https://github.com/Philip-Bos/echofield-fm1",
        usesSelLabel = true
    )

    val ARCTIC = FirmwareEntry(
        id = "arctic",
        displayName = "Arctic Scream",
        soname = "libfw_arctic.so",
        shortName = "ARC",
        version = "2.1.1",
        author = "PolarPatch",
        tags = listOf("SYNTH", "AMBIENT"),
        description = "Dark screaming Arctic ambient: living feedback loops, ice resonance, tundra wind, 4 tracks and endless shimmer.",
        accentColorHex = 0xFF22D3EE,
        repoUrl = "https://github.com/PolarPatch/Arctic-scream-FM-1",
        usesSelLabel = true
    )

    val LOOM = FirmwareEntry(
        id = "loom",
        displayName = "LOOM-8",
        soname = "libfw_loom.so",
        shortName = "LOM",
        version = "0.16.2",
        author = "Charles Vestal",
        tags = listOf("BETA", "GROOVEBOX"),
        description = "Groovebox inspired by the OP-XY: 8 tracks on 9 engines, step tricks and parameter locks, weave and throws.",
        accentColorHex = 0xFFF5B742,
        repoUrl = "https://github.com/charlesvestal/fm1-loom",
        usesSelLabel = true
    )

    val JIANT = FirmwareEntry(
        id = "jiant",
        displayName = "JIANT FM",
        soname = "libfw_jiant.so",
        shortName = "JNT",
        version = "V10.9",
        author = "juanjiant-bit",
        tags = listOf("GROOVEBOX"),
        description = "Bio-synthetic groovebox: 6 tracks drawn as living beings in thermal colours, 8 engines, mutating macros and song mode.",
        accentColorHex = 0xFFFF8833,
        repoUrl = "https://github.com/juanjiant-bit/JIANT"
    )

    val DEMAKE = FirmwareEntry(
        id = "demake",
        displayName = "Demake",
        soname = "libfw_demake.so",
        shortName = "DMK",
        version = "0.4.25",
        author = "Sequencist",
        tags = listOf("BETA", "SYNTH"),
        description = "Ambient lofi machine on 64 pedal recordings: 3 synths with piano rolls, chord machine, arp, 16 scenes and tape colour.",
        accentColorHex = 0xFF5674FF,
        repoUrl = "https://github.com/InigoHernandez/demake-synth",
        usesSelLabel = true
    )

    val KWASUB = FirmwareEntry(
        id = "kwasub",
        displayName = "K-WASUB",
        soname = "libfw_kwasub.so",
        shortName = "KWS",
        version = "1.1.03",
        author = "Omnipotard",
        tags = listOf("SYNTH"),
        description = "Bass synth for DnB, dubstep, dub and techno: deep subs, reese, growls, 808s, live wobble keys, drums and FX pads.",
        accentColorHex = 0xFF4ADE80,
        repoUrl = "https://github.com/Guizmoh/K-WASUB_Firmware_for_FM1",
        usesSelLabel = true
    )

    val SHINEON = FirmwareEntry(
        id = "shineon",
        displayName = "Shine On",
        soname = "libfw_shineon.so",
        shortName = "SHN",
        version = "3.5",
        author = "zvenson",
        tags = listOf("SYNTH", "VINTAGE"),
        description = "Late-70s string ensemble grown out of OberVave: 16'/8'/4' divide-down keyers, BBD ensemble, phaser and 4 tracks.",
        accentColorHex = 0xFFF5A642,
        repoUrl = "https://github.com/zvenson/shineon",
        usesSelLabel = true
    )

    val CHOPPAH = FirmwareEntry(
        id = "choppah",
        displayName = "CHOPPAH",
        soname = "libfw_choppah.so",
        shortName = "CHP",
        version = "1.4.1",
        author = "Laurens Profittlich",
        tags = listOf("DRUMS"),
        description = "Amen break chopper: 8 classic breaks sliced across white keys, black keys for rolls, reverse, freeze and dub throws.",
        accentColorHex = 0xFF5674FF,
        repoUrl = "https://github.com/profikid/fm1-break-choppah",
        usesSelLabel = true
    )

    val FELUCCA = FirmwareEntry(
        id = "felucca",
        displayName = "Felucca",
        soname = "libfw_felucca.so",
        shortName = "FEL",
        version = "1.8.2",
        author = "Leo Kuroshita",
        tags = listOf("GROOVEBOX"),
        description = "The original open-source alternative firmware by Hügelton Instruments: FM/VA/sample engines, sequencer and effects.",
        accentColorHex = 0xFF6FA8DC,
        repoUrl = "https://github.com/hugelton/Felucca"
    )

    val SLOOP = FirmwareEntry(
        id = "sloop",
        displayName = "SLOOP",
        soname = "libfw_sloop.so",
        shortName = "SLP",
        version = "2.8.5",
        author = "isod89",
        tags = listOf("GROOVEBOX"),
        description = "Live groovebox: tracks, layers, step sequencer, song mode, punch-in effects and samples played in real time.",
        accentColorHex = 0xFF6FA8DC,
        repoUrl = "https://github.com/isod89/sloop-fm1",
        usesSelLabel = true
    )

    val HORTATOR = FirmwareEntry(
        id = "hortator",
        displayName = "Hortator",
        soname = "libfw_hortator.so",
        shortName = "HRT",
        version = "1.0.1",
        author = "DEADACTIVE",
        tags = listOf("DRUMS"),
        description = "Eight-track drum machine: step sequencer, Mutable Grids Euclidean patterns, ACID bass, compressor and PERFORM.",
        accentColorHex = 0xFFE68A5C,
        repoUrl = "https://github.com/DeadActive/hortator"
    )

    val SHRUB = FirmwareEntry(
        id = "shrub",
        displayName = "SHRUB",
        soname = "libfw_shrub.so",
        shortName = "SHR",
        version = "1.05",
        author = "Steve Duda (Xfer)",
        tags = listOf("SYNTH", "SERUM"),
        description = "Wavetable synth after Serum 1 & 2: dual warp oscillators, Cytomic/DMG filters, 16-slot matrix, 7-slot FX rack with OTT and 8-lane seq.",
        accentColorHex = 0xFF9BEE5E,
        repoUrl = "https://shrub.audio"
    )

    val builtins: List<FirmwareEntry> = listOf(
        SHRUB,
        MUTABLE, FMINI, OBERVAVE, CORALFLOWER,
        AMB, BLOP, LOAFBREAKER, FRASIL,
        ATEK303, GURU600, RAVEL, S1VAVE,
        CLIPPER, JAMTRACK, FIMBA, AISLING,
        ECHOFIELD, ARCTIC, LOOM, JIANT,
        DEMAKE, KWASUB, SHINEON, CHOPPAH,
        FELUCCA, SLOOP, HORTATOR
    )

    fun findById(id: String): FirmwareEntry? {
        return builtins.firstOrNull { it.id.equals(id, ignoreCase = true) }
    }

    val defaultFirmware: FirmwareEntry get() = JIANT
}
