package io.github.fm1deck.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.fm1deck.APP_EDIT_REVISION
import io.github.fm1deck.engine.MasterPreset
import io.github.fm1deck.engine.NativeBridge
import io.github.fm1deck.fw.FirmwareCatalog
import io.github.fm1deck.fw.FirmwareEntry
import io.github.fm1deck.fw.rememberFirmwareThumb
import io.github.fm1deck.settings.AppSettings
import io.github.fm1deck.settings.Session
import io.github.fm1deck.ui.panel.skin.KeyLight
import io.github.fm1deck.ui.panel.SkinLayout
import io.github.fm1deck.ui.panel.skin.BlockId
import io.github.fm1deck.ui.panel.skin.Palette
import io.github.fm1deck.ui.panel.skin.Skin

enum class SettingsTab(val title: String) {
    SESSION("Session"),
    SKIN("Skin"),
    FIRMWARE("Firmware"),
    AUDIO("Audio"),
    MASTER("Master"),
    MIDI("MIDI"),
    RECORDER("Recorder"),
    ABOUT("About")
}

@Composable
fun SettingsScreen(
    currentSettings: AppSettings,
    onSettingsChanged: (AppSettings) -> Unit,
    availableSkins: List<Skin>,
    availablePalettes: Map<String, Palette>,
    onImportSkinJson: (String) -> Result<Skin>,
    builtInMasterPresets: List<MasterPreset> = emptyList(),
    userMasterPresets: List<MasterPreset> = emptyList(),
    onSaveMasterPreset: (MasterPreset) -> Unit = {},
    sessions: List<Session> = emptyList(),
    suggestedSessionName: String = "SESSION 01",
    onSaveSession: (String) -> Unit = {},
    onLoadSession: (Session) -> Unit = {},
    onDeleteSession: (Session) -> Unit = {},
    initialTab: SettingsTab = SettingsTab.SESSION,
    onClose: () -> Unit
) {
    var selectedTab by remember(initialTab) { mutableStateOf(initialTab) }
    val context = LocalContext.current

    val openDocumentLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri != null) {
            runCatching {
                val json = context.contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
                if (json != null) {
                    val res = onImportSkinJson(json)
                    res.onSuccess { imported ->
                        Toast.makeText(context, "Imported skin: ${imported.name.en}", Toast.LENGTH_SHORT).show()
                        onSettingsChanged(currentSettings.copy(activeSkinId = imported.id))
                    }.onFailure { err ->
                        Toast.makeText(context, "Invalid skin: ${err.message}", Toast.LENGTH_LONG).show()
                    }
                }
            }.onFailure {
                Toast.makeText(context, "Failed to read skin file", Toast.LENGTH_SHORT).show()
            }
        }
    }

    val tabContent: @Composable () -> Unit = {
        when (selectedTab) {
            SettingsTab.SESSION -> SessionTab(
                settings = currentSettings,
                sessions = sessions,
                suggestedName = suggestedSessionName,
                onSave = onSaveSession,
                onLoad = onLoadSession,
                onDelete = onDeleteSession
            )
            SettingsTab.SKIN -> SkinSettingsTab(
                settings = currentSettings,
                onSettingsChanged = onSettingsChanged,
                availableSkins = availableSkins,
                availablePalettes = availablePalettes,
                onImportClick = { openDocumentLauncher.launch(arrayOf("application/json", "*/*")) }
            )
            SettingsTab.FIRMWARE -> FirmwareSettingsTab(
                settings = currentSettings,
                onSettingsChanged = onSettingsChanged
            )
            SettingsTab.AUDIO -> AudioTab(
                quality = currentSettings.quality,
                onQuality = { q ->
                    NativeBridge.nativeMasterSetQuality(q)
                    onSettingsChanged(currentSettings.copy(quality = q))
                }
            )
            SettingsTab.MASTER -> MasterTab(
                initialValues = currentSettings.masterValues,
                presetName = currentSettings.masterPreset,
                builtInPresets = builtInMasterPresets,
                userPresets = userMasterPresets,
                scopeOn = currentSettings.masterScope,
                onScopeChanged = { onSettingsChanged(currentSettings.copy(masterScope = it)) },
                onCommit = { values, name ->
                    onSettingsChanged(currentSettings.copy(masterValues = values, masterPreset = name))
                },
                onSaveUserPreset = onSaveMasterPreset
            )
            SettingsTab.MIDI -> LinkTab(
                quantum = currentSettings.linkQuantum,
                onQuantumChanged = { onSettingsChanged(currentSettings.copy(linkQuantum = it)) }
            )
            SettingsTab.RECORDER -> PlaceholderTab("WAV live audio recording", "coming in P8")
            SettingsTab.ABOUT -> AboutTab()
        }
    }

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF161616))
    ) {
        // Portrait (the gamepad skin): the tabs go to a scrolling strip on top, the content gets the full width
        if (maxHeight > maxWidth) {
            CompositionLocalProvider(LocalSettingsCompact provides true) {
                PortraitSettings(selectedTab, { selectedTab = it }, onClose, tabContent)
            }
            return@BoxWithConstraints
        }
        Row(modifier = Modifier.fillMaxSize()) {
            // Left rail with tabs
            Column(
                modifier = Modifier
                    .width(160.dp)
                    .fillMaxHeight()
                    .background(Color(0xFF1E1E1E))
                    .padding(vertical = 12.dp)
            ) {
                Text(
                    text = "SETTINGS",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 14.sp,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
                )

                HorizontalDivider(color = Color(0xFF2C2C2C), thickness = 1.dp)

                Spacer(modifier = Modifier.height(8.dp))

                // The tabs scroll on a short screen; Close stays at the bottom
                Column(modifier = Modifier.weight(1f).verticalScroll(rememberScrollState())) {
                SettingsTab.entries.forEach { tab ->
                    val isSelected = selectedTab == tab
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { selectedTab = tab }
                            .background(if (isSelected) Color(0xFF2D2D2D) else Color.Transparent)
                            .padding(horizontal = 16.dp, vertical = 10.dp)
                    ) {
                        Text(
                            text = tab.title,
                            color = if (isSelected) Color(0xFF6FA8DC) else Color(0xFFCCCCCC),
                            fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                            fontSize = 13.sp
                        )
                    }
                }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Button(
                    onClick = onClose,
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF333333)),
                    shape = RoundedCornerShape(8.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp)
                ) {
                    Text("Close", color = Color.White, fontSize = 12.sp)
                }
            }

            // Right content area
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .padding(20.dp)
            ) {
                tabContent()
            }
        }
    }
}

/** True while Settings is laid out for a phone held upright: tabs pick their narrow arrangement. */
val LocalSettingsCompact = staticCompositionLocalOf { false }

@Composable
private fun PortraitSettings(
    selectedTab: SettingsTab,
    onSelect: (SettingsTab) -> Unit,
    onClose: () -> Unit,
    content: @Composable () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
    ) {
        Column(modifier = Modifier.fillMaxWidth().background(Color(0xFF1E1E1E))) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(start = 16.dp, end = 10.dp, top = 10.dp, bottom = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("SETTINGS", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 14.sp)
                Spacer(modifier = Modifier.weight(1f))
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF333333))
                        .clickable(onClick = onClose)
                        .padding(horizontal = 18.dp, vertical = 9.dp)
                ) {
                    Text("Close", color = Color.White, fontSize = 12.sp)
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 6.dp)
            ) {
                SettingsTab.entries.forEach { tab ->
                    val isSelected = selectedTab == tab
                    Column(
                        modifier = Modifier
                            .clickable { onSelect(tab) }
                            .padding(horizontal = 10.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = tab.title,
                            color = if (isSelected) Color(0xFF6FA8DC) else Color(0xFFCCCCCC),
                            fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                            fontSize = 13.sp,
                            modifier = Modifier.padding(vertical = 11.dp)
                        )
                        Box(
                            modifier = Modifier
                                .width(28.dp)
                                .height(2.dp)
                                .background(if (isSelected) Color(0xFF6FA8DC) else Color.Transparent)
                        )
                    }
                }
            }
            HorizontalDivider(color = Color(0xFF2C2C2C), thickness = 1.dp)
        }
        Box(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp)
        ) {
            content()
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SkinSettingsTab(
    settings: AppSettings,
    onSettingsChanged: (AppSettings) -> Unit,
    availableSkins: List<Skin>,
    availablePalettes: Map<String, Palette>,
    onImportClick: () -> Unit
) {
    val scrollState = rememberScrollState()
    val perRow = if (LocalSettingsCompact.current) 2 else 3

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
    ) {
        Text("PANEL LAYOUT", color = Color(0xFF888888), fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(10.dp))

        // Skin thumbnails: a grid of three per row (two upright), each a schematic of the real layout
        availableSkins.chunked(perRow).forEach { rowSkins ->
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                rowSkins.forEach { skin ->
                    val isSelected = skin.id == settings.activeSkinId
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier
                            .weight(1f)
                            .clickable { onSettingsChanged(settings.copy(activeSkinId = skin.id)) }
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .aspectRatio(THUMB_REF_W / THUMB_REF_H)
                                .clip(RoundedCornerShape(6.dp))
                                .background(Color(0xFF1E1E1E))
                                .border(
                                    width = if (isSelected) 2.dp else 1.dp,
                                    color = if (isSelected) Color(0xFF6FA8DC) else Color(0xFF333333),
                                    shape = RoundedCornerShape(6.dp)
                                )
                        ) {
                            SkinThumbnail(skin = skin)
                        }
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = skin.name.en,
                            color = if (isSelected) Color(0xFF6FA8DC) else Color.White,
                            fontSize = 11.sp,
                            maxLines = 1,
                            textAlign = TextAlign.Center
                        )
                    }
                }
                repeat(perRow - rowSkins.size) { Spacer(modifier = Modifier.weight(1f)) }
            }
            Spacer(modifier = Modifier.height(10.dp))
        }

        Spacer(modifier = Modifier.height(16.dp))

        Button(
            onClick = onImportClick,
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2E2E2E)),
            shape = RoundedCornerShape(6.dp)
        ) {
            Text("Import skin (.json)", color = Color(0xFF6FA8DC), fontSize = 12.sp)
        }

        Spacer(modifier = Modifier.height(20.dp))
        HorizontalDivider(color = Color(0xFF282828))
        Spacer(modifier = Modifier.height(16.dp))

        // Options: Keyboard Height
        Text("KEYBOARD HEIGHT: ${(settings.keyboardFraction * 100).toInt()}%", color = Color.White, fontSize = 12.sp)
        Slider(
            value = settings.keyboardFraction,
            onValueChange = { onSettingsChanged(settings.copy(keyboardFraction = it)) },
            valueRange = 0.30f..0.45f,
            steps = 15,
            colors = SliderDefaults.colors(
                thumbColor = Color(0xFF6FA8DC),
                activeTrackColor = Color(0xFF6FA8DC),
                inactiveTrackColor = Color(0xFF333333)
            )
        )

        Spacer(modifier = Modifier.height(12.dp))

        // Knob Sensitivity
        Text("KNOB SENSITIVITY: ${settings.knobSensitivityDp.toInt()} dp/step", color = Color.White, fontSize = 12.sp)
        Slider(
            value = settings.knobSensitivityDp,
            onValueChange = { onSettingsChanged(settings.copy(knobSensitivityDp = it)) },
            valueRange = 3f..12f,
            steps = 9,
            colors = SliderDefaults.colors(
                thumbColor = Color(0xFF6FA8DC),
                activeTrackColor = Color(0xFF6FA8DC),
                inactiveTrackColor = Color(0xFF333333)
            )
        )

        Spacer(modifier = Modifier.height(12.dp))

        // Switches: Fine mode, Glissando, Screen mode, Frames override
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(
                checked = settings.fineModeOnLongPress,
                onCheckedChange = { onSettingsChanged(settings.copy(fineModeOnLongPress = it)) },
                colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFF6FA8DC))
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text("Fine mode on knob hold (×0.25 speed)", color = Color.White, fontSize = 12.sp)
        }

        Spacer(modifier = Modifier.height(8.dp))

        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(
                checked = settings.glissando,
                onCheckedChange = { onSettingsChanged(settings.copy(glissando = it)) },
                colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFF6FA8DC))
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text("Key Glissando (slide finger between keys)", color = Color.White, fontSize = 12.sp)
        }

        Spacer(modifier = Modifier.height(8.dp))

        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(
                checked = settings.screenPixelated,
                onCheckedChange = { onSettingsChanged(settings.copy(screenPixelated = it)) },
                colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFF6FA8DC))
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text("Pixelated screen scaling (nearest-neighbour)", color = Color.White, fontSize = 12.sp)
        }

        Spacer(modifier = Modifier.height(14.dp))

        // The key LEDs' colour: the palette's white, or one that stands out on the grey keybed
        Text("KEY LIGHT", color = Color(0xFF888888), fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            KeyLight.COLORS.forEach { (id, c) ->
                val selected = settings.keyLight == id
                Box(
                    modifier = Modifier
                        .size(34.dp)
                        .clip(RoundedCornerShape(17.dp))
                        .background(Color(0xFF1F1F1F))
                        .border(if (selected) 2.dp else 1.dp, if (selected) Color.White else Color(0xFF3A3A3A), RoundedCornerShape(17.dp))
                        .clickable { onSettingsChanged(settings.copy(keyLight = id)) },
                    contentAlignment = Alignment.Center
                ) {
                    // A key's slit in that colour (the palette's: its warm white)
                    Box(
                        Modifier
                            .size(width = 6.dp, height = 18.dp)
                            .clip(RoundedCornerShape(3.dp))
                            .background(c ?: Color(0xFFF2EBDD))
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(10.dp))

        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(
                checked = settings.keyBeatMarks,
                onCheckedChange = { onSettingsChanged(settings.copy(keyBeatMarks = it)) },
                colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFF6FA8DC))
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text("Beat marks on the keys (every 4 white keys)", color = Color.White, fontSize = 12.sp)
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Haptics mode toggle
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(
                checked = settings.hapticsMode != AppSettings.HAPTICS_OFF,
                onCheckedChange = {
                    onSettingsChanged(
                        settings.copy(
                            hapticsMode = if (it) AppSettings.HAPTICS_LIGHT else AppSettings.HAPTICS_OFF
                        )
                    )
                },
                colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFF6FA8DC))
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text("Haptic feedback on key tap", color = Color.White, fontSize = 12.sp)
        }
    }
}

/** Reference panel the thumbnail lays out (M52 in dp): the schematic shows the real geometry, scaled down. */
private const val THUMB_REF_W = 914f
private const val THUMB_REF_H = 411f

/**
 * A schematic of the skin drawn from its real layout: islands, the screen, the 2x6 buttons, the knobs, OCT and the
 * keyboard, so each skin is recognisable at a glance.
 */
@Composable
private fun SkinThumbnail(skin: Skin) {
    // A portrait skin is laid out on the upright phone and drawn standing in the middle of the tile
    val refW = if (skin.isPortrait) THUMB_REF_H else THUMB_REF_W
    val refH = if (skin.isPortrait) THUMB_REF_W else THUMB_REF_H
    val geometry = remember(skin) {
        SkinLayout.calculate(skin = skin, widthPx = refW, heightPx = refH, density = 1.0f)
    }
    Canvas(modifier = Modifier.fillMaxSize()) {
        val k = minOf(size.width / refW, size.height / refH)
        val ox = (size.width - refW * k) / 2f
        val oy = (size.height - refH * k) / 2f
        fun o(x: Float, y: Float) = Offset(ox + x * k, oy + y * k)
        fun sz(r: io.github.fm1deck.ui.panel.Rect) = Size(r.width * k, r.height * k)
        fun rr(r: io.github.fm1deck.ui.panel.Rect, color: Color, radius: Float) =
            drawRoundRect(color, o(r.left, r.top), sz(r), CornerRadius(radius * k))

        if (skin.isPortrait) {
            rr(io.github.fm1deck.ui.panel.Rect(0f, 0f, refW, refH), Color(0xFF1D1D1F), 20f)
        } else {
            drawRect(Color(0xFF1D1D1F))
        }
        // islands
        for ((id, block) in geometry.blocks) {
            if (id == BlockId.SCREEN || block.bounds.width <= 0f) continue
            val b = block.bounds
            val pad = 6f
            rr(io.github.fm1deck.ui.panel.Rect(b.left - pad, b.top - pad, b.right + pad, b.bottom + pad), Color(0xFF26262A), 14f)
        }
        if (geometry.octTray.width > 0f) rr(geometry.octTray, Color(0xFF141415), 10f)
        // screen: black bezel, a lit LCD with a header bar
        rr(geometry.screen.frameRect, Color(0xFF0C0C0D), 18f)
        val lcd = geometry.screen.lcdRect
        rr(lcd, Color(0xFF2B3A4A), 6f)
        drawRect(Color(0xFF79B8FF), o(lcd.left, lcd.top), Size(lcd.width * k, lcd.height * 0.14f * k))
        // buttons (OCT ones are part of the list too)
        for (btn in geometry.buttons) rr(btn.rect, Color(0xFF5A5952), 8f)
        // knobs and MASTER
        for (kn in geometry.knobs) {
            drawCircle(Color(0xFF8A8A8E), kn.knobRadius * k, o(kn.knobCenter.x, kn.knobCenter.y))
        }
        val mp = geometry.masterPot
        drawCircle(Color(0xFFD6D6D8), mp.knobRadius * k, o(mp.knobCenter.x, mp.knobCenter.y))
        // keyboard: bed and capsules
        rr(geometry.keyboardRect, Color(0xFF141415), 12f)
        for (key in geometry.keys) {
            rr(key.capsuleRect, if (key.isBlack) Color(0xFF3A3A3E) else Color(0xFF4A4A4E), 12f)
        }
    }
}

@Composable
private fun FirmwareSettingsTab(
    settings: AppSettings,
    onSettingsChanged: (AppSettings) -> Unit
) {
    val compact = LocalSettingsCompact.current
    val perRow = if (compact) 2 else 4
    var targetSlot by remember { mutableIntStateOf(settings.rackFocus) }
    LaunchedEffect(settings.rackFocus) {
        if (settings.rack.getOrNull(targetSlot)?.isNotEmpty() == true) {
            targetSlot = settings.rackFocus
        }
    }
    val rows = remember(perRow) { FirmwareCatalog.builtins.chunked(perRow) }
    val filledCount = settings.rack.count { it.isNotEmpty() }

    Column(modifier = Modifier.fillMaxSize()) {
        // 4-slot rack switcher bar
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            for (slot in 0 until AppSettings.RACK_SLOTS) {
                val fwId = settings.rack.getOrElse(slot) { "" }
                val entry = FirmwareCatalog.findById(fwId)
                val isFocused = slot == settings.rackFocus
                val isTarget = slot == targetSlot
                val canClear = entry != null && filledCount > 1
                Row(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(8.dp))
                        .background(
                            when {
                                isFocused -> Color(0xFF243342)
                                isTarget -> Color(0xFF28282C)
                                else -> Color(0xFF1E1E1E)
                            }
                        )
                        .border(
                            width = if (isTarget) 1.5.dp else 1.dp,
                            color = when {
                                isTarget -> Color(0xFF6FA8DC)
                                isFocused -> Color(0xFF476A8E)
                                else -> Color(0xFF2E2E2E)
                            },
                            shape = RoundedCornerShape(8.dp)
                        )
                        .clickable {
                            targetSlot = slot
                            if (entry != null && !isFocused) {
                                onSettingsChanged(settings.withFocus(slot))
                            }
                        }
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "${slot + 1}",
                        color = if (isFocused) Color(0xFF6FA8DC) else Color(0xFF888888),
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = entry?.displayName ?: "Empty",
                        color = if (entry != null) Color.White else Color(0xFF666666),
                        fontSize = 11.sp,
                        fontWeight = if (isFocused) FontWeight.Bold else FontWeight.Normal,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                    if (canClear) {
                        Text(
                            text = "×",
                            color = Color(0xFF888888),
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Bold,
                            modifier = Modifier
                                .clip(RoundedCornerShape(4.dp))
                                .clickable {
                                    onSettingsChanged(settings.withSlot(slot, ""))
                                }
                                .padding(horizontal = 4.dp)
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(6.dp))
        Text(
            text = "FIRMWARES (${FirmwareCatalog.builtins.size}) · TAP CARD TO LOAD INTO SLOT ${targetSlot + 1} · RACK PLAYS ${AppSettings.RACK_SLOTS} AT ONCE",
            color = Color(0xFF777777),
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(8.dp))

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(10.dp),
            contentPadding = PaddingValues(bottom = 12.dp)
        ) {
            itemsIndexed(rows) { _, rowFirmwares ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    rowFirmwares.forEach { fw ->
                        FirmwareCard(
                            fw = fw,
                            isFocused = fw.id == settings.activeFwId,
                            slotIndex = settings.rack.indexOf(fw.id).takeIf { it >= 0 },
                            modifier = Modifier.weight(1f),
                            onClick = {
                                val existingSlot = settings.rack.indexOf(fw.id).takeIf { it >= 0 }
                                val next = if (existingSlot != null && existingSlot != targetSlot) {
                                    settings.withFocus(existingSlot)
                                } else {
                                    settings.withSlot(targetSlot, fw.id).withFocus(targetSlot)
                                }
                                targetSlot = next.rackFocus
                                onSettingsChanged(next)
                            }
                        )
                    }
                    repeat(perRow - rowFirmwares.size) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun FirmwareCard(
    fw: FirmwareEntry,
    isFocused: Boolean,
    slotIndex: Int?,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    val thumb = rememberFirmwareThumb(fw.thumbAsset)
    val accent = Color(fw.accentColorHex)
    val borderColor = when {
        isFocused -> Color(0xFF6FA8DC)
        slotIndex != null -> accent.copy(alpha = 0.7f)
        else -> Color(0xFF2B2B30)
    }

    Column(
        modifier = modifier
            .clip(RoundedCornerShape(10.dp))
            .background(if (isFocused) Color(0xFF1E2936) else Color(0xFF1A1A1E))
            .border(if (isFocused) 1.5.dp else 1.dp, borderColor, RoundedCornerShape(10.dp))
            .clickable(onClick = onClick)
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(1.22f)
                .background(Color(0xFF0E0E11)),
            contentAlignment = Alignment.Center
        ) {
            if (thumb != null) {
                Image(
                    bitmap = thumb,
                    contentDescription = fw.displayName,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize().padding(4.dp)
                )
            } else {
                Text(
                    text = fw.shortName,
                    color = accent.copy(alpha = 0.6f),
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
            }
            if (isFocused || slotIndex != null) {
                Box(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(5.dp)
                        .clip(RoundedCornerShape(4.dp))
                        .background(if (isFocused) Color(0xFF6FA8DC) else Color(0xCC222228))
                        .border(1.dp, if (isFocused) Color(0xFF6FA8DC) else accent.copy(alpha = 0.8f), RoundedCornerShape(4.dp))
                        .padding(horizontal = 5.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = if (isFocused) "SLOT ${ (slotIndex ?: 0) + 1 } · ON" else "SLOT ${slotIndex!! + 1}",
                        color = if (isFocused) Color(0xFF0E141B) else Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }
        }

        // Accent bar matching The Firmware Hub cards
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(2.5.dp)
                .background(accent)
        )

        Column(modifier = Modifier.padding(horizontal = 8.dp, vertical = 7.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Bottom
            ) {
                Text(
                    text = fw.displayName,
                    color = accent,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false)
                )
                if (fw.version.isNotEmpty()) {
                    Spacer(Modifier.width(4.dp))
                    Text(
                        text = fw.version,
                        color = Color(0xFF88888E),
                        fontSize = 9.sp,
                        maxLines = 1
                    )
                }
            }
            if (fw.author.isNotEmpty()) {
                Text(
                    text = "by ${fw.author}",
                    color = Color(0xFF9A9AA2),
                    fontSize = 9.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            if (fw.tags.isNotEmpty()) {
                Spacer(Modifier.height(4.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    fw.tags.take(2).forEach { tag ->
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(3.dp))
                                .background(
                                    when (tag) {
                                        "BETA" -> Color(0xFF3E3018)
                                        else -> Color(0xFF25252B)
                                    }
                                )
                                .padding(horizontal = 4.dp, vertical = 1.dp)
                        ) {
                            Text(
                                text = tag,
                                color = if (tag == "BETA") Color(0xFFF2B66D) else Color(0xFFB4B4BC),
                                fontSize = 7.5.sp,
                                fontWeight = FontWeight.Bold,
                                maxLines = 1
                            )
                        }
                    }
                }
            }
            Spacer(Modifier.height(4.dp))
            Text(
                text = fw.description,
                color = Color(0xFFC2C2C8),
                fontSize = 9.sp,
                lineHeight = 12.sp,
                maxLines = 2,
                minLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun PlaceholderTab(title: String, subtitle: String) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Text(subtitle, color = Color(0xFF888888), fontSize = 12.sp)
        }
    }
}

@Composable
private fun AboutTab() {
    Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Text("FM1 DECK", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(4.dp))
        Text("Alternative firmware groovebox for Android (${FirmwareCatalog.builtins.size} firmwares, ${AppSettings.RACK_SLOTS} rack slots)", color = Color(0xFF888888), fontSize = 12.sp)

        Spacer(modifier = Modifier.height(16.dp))
        HorizontalDivider(color = Color(0xFF282828))
        Spacer(modifier = Modifier.height(16.dp))

        Text("App Edit Revision: $APP_EDIT_REVISION", color = Color(0xFF6FA8DC), fontSize = 12.sp)
        Text("License: GNU General Public License v3.0 (GPL-3.0-only)", color = Color.White, fontSize = 12.sp)
        Text("Audio Engine: Oboe C++17 (AAudio / OpenSL ES)", color = Color.White, fontSize = 12.sp)
        Text("Firmware ABI: 1", color = Color.White, fontSize = 12.sp)

        Spacer(modifier = Modifier.height(16.dp))
        Text("CREDITS & PORTS (${FirmwareCatalog.builtins.size} FIRMWARES)", color = Color(0xFF888888), fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(8.dp))
        FirmwareCatalog.builtins.forEach { fw ->
            val repo = fw.repoUrl.removePrefix("https://")
            Text("• ${fw.displayName} ${fw.version} — ${fw.author} ($repo)", color = Color.White, fontSize = 11.sp)
        }
    }
}
