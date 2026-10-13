package io.github.fm1deck

import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.StrictMode
import android.util.Log
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import io.github.fm1deck.engine.AudioController
import io.github.fm1deck.engine.LinkController
import io.github.fm1deck.engine.MasterControl
import io.github.fm1deck.engine.MasterMeter
import io.github.fm1deck.engine.MasterPreset
import io.github.fm1deck.engine.NativeBridge
import io.github.fm1deck.fw.FirmwareCatalog
import io.github.fm1deck.fw.FirmwareEntry
import io.github.fm1deck.fw.flash.FlashStore
import io.github.fm1deck.settings.AppSettings
import io.github.fm1deck.settings.SettingsStore
import io.github.fm1deck.ui.panel.Insets
import io.github.fm1deck.ui.panel.PanelSurface
import io.github.fm1deck.ui.panel.SkinLayout
import io.github.fm1deck.ui.panel.rememberPanelState
import io.github.fm1deck.ui.panel.skin.PaletteRepository
import io.github.fm1deck.ui.panel.skin.Skin
import io.github.fm1deck.ui.panel.skin.SkinRepository
import io.github.fm1deck.ui.settings.SettingsScreen
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import java.io.File

class MainActivity : ComponentActivity() {
    private lateinit var flashStore: FlashStore
    private lateinit var settingsFile: File

    private var activeFirmwareState = mutableStateOf(FirmwareCatalog.defaultFirmware)
    private var settingsState = mutableStateOf(AppSettings())
    private var builtInMasterPresets: List<MasterPreset> = emptyList()
    private val userMasterPresetsState = mutableStateOf<List<MasterPreset>>(emptyList())
    private var skinsState = mutableStateOf<List<Skin>>(emptyList())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (BuildConfig.DEBUG) {
            StrictMode.setThreadPolicy(
                StrictMode.ThreadPolicy.Builder()
                    .detectDiskReads()
                    .detectDiskWrites()
                    .detectNetwork()
                    .penaltyLog()
                    .build()
            )
        }

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        flashStore = FlashStore(this)
        settingsFile = File(filesDir, "settings.json")
        val loadedSettings = SettingsStore.load(settingsFile)
        settingsState.value = loadedSettings

        // Master bus: built-in + user presets, and the stored state to the native bus (it keeps them across streams)
        builtInMasterPresets = loadBuiltInMasterPresets()
        userMasterPresetsState.value = loadUserMasterPresets()
        MasterControl.sendAll(
            MasterControl.resolve(MasterControl.params, loadedSettings.masterValues),
            loadedSettings.quality
        )
        // Ableton Link stays off until the user switches it on (Settings → MIDI); only its quantum is restored
        LinkController.setQuantum(loadedSettings.linkQuantum)

        // Load skins & palettes
        val userSkinsDir = File(filesDir, "skins")
        skinsState.value = SkinRepository.loadAll(assets, userSkinsDir)

        if (BuildConfig.DEBUG) {
            handleDebugIntent(intent)
        }

        // Restore active firmware
        val restoredFw = FirmwareCatalog.findById(loadedSettings.activeFwId) ?: FirmwareCatalog.defaultFirmware
        activeFirmwareState.value = restoredFw

        setContent {
            val settings by settingsState
            val skins by skinsState
            val activeFirmware by activeFirmwareState
            var showSettings by remember { mutableStateOf(false) }

            val palettes = remember { PaletteRepository.loadFromAssets(assets) }
            val activePalette = palettes[settings.activePaletteId] ?: PaletteRepository.GRAPHITE

            val activeSkin = remember(settings.activeSkinId, skins) {
                skins.firstOrNull { it.id == settings.activeSkinId } ?: skins.firstOrNull()
            }

            val panelState = rememberPanelState(active = true)

            // Update button names depending on active firmware (SLOOP has SEL)
            LaunchedEffect(activeFirmware) {
                if (activeFirmware.id == "sloop") {
                    panelState.buttonNames = listOf(
                        "FX", "SEL", "ENV", "LFO", "EDIT", "GLO",
                        "HOME", "SAVE", "ARP", "SEQ", "PLAY\nSTOP", "REC"
                    )
                } else {
                    panelState.buttonNames = listOf(
                        "FX", "SCL", "ENV", "LFO", "EDIT", "GLO",
                        "HOME", "SAVE", "ARP", "SEQ", "PLAY\nSTOP", "REC"
                    )
                }
            }

            LaunchedEffect(settings.activeFwId) {
                val target = FirmwareCatalog.findById(settings.activeFwId) ?: FirmwareCatalog.defaultFirmware
                if (target.id != activeFirmwareState.value.id) {
                    val oldFw = activeFirmwareState.value
                    activeFirmwareState.value = target
                    AudioController.switchFirmware(oldFw.id, target.id, target.soname, flashStore, settings.masterAdc)
                }
            }

            // Autosave loop running in the background (~2 seconds)
            LaunchedEffect(activeFirmware) {
                while (isActive) {
                    delay(2000)
                    AudioController.checkAndAutosave(activeFirmware.id, flashStore)
                    AudioController.reopenIfNeeded()
                }
            }

            BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
                val density = LocalDensity.current.density
                val layoutDirection = LocalLayoutDirection.current
                val windowInsets = WindowInsets.displayCutout

                val safeInsets = Insets(
                    left = windowInsets.getLeft(LocalDensity.current, layoutDirection).toFloat(),
                    top = windowInsets.getTop(LocalDensity.current).toFloat(),
                    right = windowInsets.getRight(LocalDensity.current, layoutDirection).toFloat(),
                    bottom = windowInsets.getBottom(LocalDensity.current).toFloat(),
                    cornerRadius = topLeftCornerRadius()
                )

                if (activeSkin != null) {
                    // Apply keyboardFraction setting override
                    val customSkin = remember(activeSkin, settings.keyboardFraction) {
                        activeSkin.copy(
                            panel = activeSkin.panel.copy(keyboardFraction = settings.keyboardFraction)
                        )
                    }

                    val geometry = remember(customSkin, constraints.maxWidth, constraints.maxHeight, density, safeInsets) {
                        SkinLayout.calculate(
                            skin = customSkin,
                            widthPx = constraints.maxWidth.toFloat(),
                            heightPx = constraints.maxHeight.toFloat(),
                            density = density,
                            safeInsets = safeInsets
                        )
                    }

                    // T8: Exclude keyboard edges from system back gesture
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        val view = LocalView.current
                        LaunchedEffect(geometry) {
                            val kRect = geometry.keyboardRect
                            val edgeW = 40f * density
                            val leftRect = android.graphics.Rect(
                                kRect.left.toInt(),
                                kRect.top.toInt(),
                                (kRect.left + edgeW).toInt(),
                                kRect.bottom.toInt()
                            )
                            val rightRect = android.graphics.Rect(
                                (kRect.right - edgeW).toInt(),
                                kRect.top.toInt(),
                                kRect.right.toInt(),
                                kRect.bottom.toInt()
                            )
                            view.systemGestureExclusionRects = listOf(leftRect, rightRect)
                        }
                    }

                    PanelSurface(
                        geometry = geometry,
                        palette = activePalette,
                        settings = settings,
                        panelState = panelState,
                        onGearClick = { showSettings = true },
                        onMasterChange = { newAdc ->
                            updateSettings(settings.copy(masterAdc = newAdc))
                        },
                        modifier = Modifier.fillMaxSize()
                    )
                }

                if (showSettings) {
                    SettingsScreen(
                        currentSettings = settings,
                        onSettingsChanged = { newSettings -> updateSettings(newSettings) },
                        availableSkins = skins,
                        availablePalettes = palettes,
                        onImportSkinJson = { jsonStr -> importUserSkin(jsonStr) },
                        builtInMasterPresets = builtInMasterPresets,
                        userMasterPresets = userMasterPresetsState.value,
                        onSaveMasterPreset = { preset -> saveUserMasterPreset(preset) },
                        onClose = { showSettings = false }
                    )
                }
            }
        }
    }

    private fun loadBuiltInMasterPresets(): List<MasterPreset> = runCatching {
        (assets.list("presets/master") ?: emptyArray()).sorted().mapNotNull { name ->
            runCatching {
                MasterPreset.fromJson(assets.open("presets/master/$name").bufferedReader().use { it.readText() }, builtIn = true)
            }.getOrNull()
        }
    }.getOrDefault(emptyList())

    private fun loadUserMasterPresets(): List<MasterPreset> {
        val dir = File(filesDir, "presets/master")
        return (dir.listFiles { f -> f.name.endsWith(".json") } ?: emptyArray()).sortedBy { it.name }.mapNotNull { f ->
            runCatching { MasterPreset.fromJson(f.readText(), builtIn = false) }.getOrNull()
        }
    }

    private fun saveUserMasterPreset(preset: MasterPreset) {
        val dir = File(filesDir, "presets/master").apply { mkdirs() }
        val safe = preset.name.replace(Regex("[^\\p{L}0-9 _-]"), "_").trim().ifEmpty { "preset" }
        File(dir, "$safe.json").writeText(preset.toJson())
        userMasterPresetsState.value = loadUserMasterPresets()
    }

    private fun updateSettings(newSettings: AppSettings) {
        settingsState.value = newSettings
        SettingsStore.save(settingsFile, newSettings)
    }

    private fun importUserSkin(jsonString: String): Result<Skin> = runCatching {
        val parsed = SkinRepository.parseSkin(jsonString, isUserSkin = true)
        val userSkinsDir = File(filesDir, "skins")
        if (!userSkinsDir.exists()) userSkinsDir.mkdirs()
        val targetFile = File(userSkinsDir, "${parsed.id}.json")
        targetFile.writeText(jsonString)
        skinsState.value = SkinRepository.loadAll(assets, userSkinsDir)
        parsed
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (BuildConfig.DEBUG) {
            handleDebugIntent(intent)
        }
    }

    private fun handleDebugIntent(intent: Intent?) {
        val skinExtra = intent?.getStringExtra("skin")?.takeIf { it.isNotBlank() }
        val paletteExtra = intent?.getStringExtra("palette")?.takeIf { it.isNotBlank() }
        val framesExtra = intent?.getStringExtra("frames")?.takeIf { it.isNotBlank() } // skin / on / off
        // --ei quality 1|2|4|8: master bus oversampling; --ez metrics true: one line of CPU / bus metrics to logcat
        val qualityExtra = intent?.getIntExtra("quality", 0) ?: 0
        if (qualityExtra in AppSettings.QUALITIES) {
            NativeBridge.nativeMasterSetQuality(qualityExtra)
            updateSettings(settingsState.value.copy(quality = qualityExtra))
            NativeBridge.nativeResetCpuMetrics()
        }
        if (intent?.getBooleanExtra("metrics", false) == true) {
            val cpu = NativeBridge.nativeCpuLoad()
            val meters = FloatArray(MasterMeter.COUNT).also { MasterControl.readMeters(it) }
            val info = NativeBridge.nativeStreamInfo()
            android.util.Log.i(
                "FM1_METRICS",
                "q=${settingsState.value.quality} rate=${info[0]} burst=${info[1]} buf=${info[2]} xruns=${info[7]} " +
                    "cb p50=%.1f%% p99=%.1f%% max=%.1f%% bus=%.2f%% latency=%d".format(
                        cpu[4] * 100, cpu[5] * 100, cpu[6] * 100, meters[MasterMeter.DSP_LOAD], MasterControl.latencyFrames()
                    ) + " meters=" + meters.joinToString(",") { "%.1f".format(it) }
            )
        }
        if (skinExtra != null || paletteExtra != null || framesExtra != null) {
            val current = settingsState.value
            val updated = current.copy(
                activeSkinId = skinExtra ?: current.activeSkinId,
                activePaletteId = paletteExtra ?: current.activePaletteId,
                framesOverride = framesExtra ?: current.framesOverride
            )
            updateSettings(updated)
        }
    }

    override fun onStart() {
        super.onStart()
        AudioController.start()
        NativeBridge.nativeSetScreenWanted(true)
        val fw = activeFirmwareState.value
        AudioController.ensureLoaded(fw.id, fw.soname, flashStore, settingsState.value.masterAdc)
    }

    override fun onStop() {
        super.onStop()
        NativeBridge.nativeSetScreenWanted(false)
        val fw = activeFirmwareState.value
        flashStore.saveActiveNow(fw.id)
        AudioController.stop()
    }
}

/** Radius of the display's rounded top-left corner in px (Android 12+; 0 when square or unknown). */
private fun android.app.Activity.topLeftCornerRadius(): Float {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return 0f
    val corner = display?.getRoundedCorner(android.view.RoundedCorner.POSITION_TOP_LEFT) ?: return 0f
    return corner.radius.toFloat()
}
