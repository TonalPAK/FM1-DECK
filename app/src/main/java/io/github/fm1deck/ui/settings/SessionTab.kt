package io.github.fm1deck.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.fm1deck.fw.FirmwareCatalog
import io.github.fm1deck.settings.AppSettings
import io.github.fm1deck.settings.Session
import kotlinx.coroutines.delay
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.roundToInt

private val SLabel = Color(0xFF888888)
private val SText = Color(0xFFDDDDDD)
private val SDim = Color(0xFF666666)
private val SCard = Color(0xFF1E1E1E)
private val SEdge = Color(0xFF2C2C2C)
private val SLit = Color(0xFF6FA8DC)
private val SWarn = Color(0xFFD9A441)

/** The rack in one line: "1 HRT · 2 FEL · 3 JNT · 120 BPM". */
private fun rackLine(rack: List<String>, tempo: Double): String {
    val slots = rack.mapIndexedNotNull { i, id ->
        FirmwareCatalog.findById(id)?.let { "${i + 1} ${it.shortName}" }
    }
    val clock = if (rack.count { it.isNotEmpty() } > 1) " · ${tempo.roundToInt()} BPM" else ""
    return slots.joinToString(" · ") + clock
}

/**
 * Settings → Session: the whole rack saved under a name in one tap (every firmware's flash with it), and loaded
 * back the same way. Saving over a name, loading and deleting each take a second tap to confirm.
 */
@Composable
fun SessionTab(
    settings: AppSettings,
    sessions: List<Session>,
    suggestedName: String,
    onSave: (name: String) -> Unit,
    onLoad: (Session) -> Unit,
    onDelete: (Session) -> Unit
) {
    var name by remember(suggestedName) { mutableStateOf(suggestedName) }
    var armed by remember { mutableStateOf<String?>(null) }   // "save:<name>", "load:<id>", "del:<id>"
    LaunchedEffect(armed) { if (armed != null) { delay(3000); armed = null } }
    val existing = sessions.firstOrNull { it.name.equals(name.trim(), ignoreCase = true) }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Text("SAVE SESSION", color = SLabel, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(4.dp))
        Text("Now: ${rackLine(settings.rack, settings.rackTempo)}", color = SDim, fontSize = 11.sp)
        Spacer(Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(8.dp))
                    .background(SCard)
                    .border(1.dp, SEdge, RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 11.dp)
            ) {
                BasicTextField(
                    value = name,
                    onValueChange = { name = it.take(24) },
                    singleLine = true,
                    textStyle = TextStyle(color = SText, fontSize = 14.sp, fontWeight = FontWeight.Bold),
                    cursorBrush = SolidColor(SLit),
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { if (name.isNotBlank() && existing == null) onSave(name.trim()) }),
                    modifier = Modifier.fillMaxWidth()
                )
            }
            Spacer(Modifier.width(10.dp))
            val saveKey = "save:${name.trim().uppercase()}"
            Pill(
                text = when {
                    existing != null && armed == saveKey -> "OVERWRITE?"
                    existing != null -> "SAVE OVER"
                    else -> "SAVE"
                },
                lit = true,
                warn = armed == saveKey
            ) {
                if (name.isBlank()) return@Pill
                if (existing == null || armed == saveKey) { onSave(name.trim()); armed = null } else armed = saveKey
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(
            "Saves every slot's firmware with its flash, MASTER, mute / solo, the focus and the tempo. " +
                "Hortator keeps no autosave: save its pattern to a project (SAVE) before the session.",
            color = SDim, fontSize = 10.sp
        )

        Spacer(Modifier.height(18.dp))
        Text("SESSIONS", color = SLabel, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        if (sessions.isEmpty()) {
            Text("None saved yet.", color = SDim, fontSize = 12.sp)
        }
        val fmt = remember { SimpleDateFormat("dd.MM HH:mm", Locale.US) }
        for (s in sessions) {
            Row(
                Modifier
                    .fillMaxWidth()
                    .padding(bottom = 8.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(SCard)
                    .border(1.dp, SEdge, RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Column(Modifier.weight(1f)) {
                    Text(s.name, color = SText, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
                    Text("${fmt.format(Date(s.savedAt))}  ·  ${rackLine(s.rack, s.rackTempo)}", color = SDim, fontSize = 10.sp, maxLines = 1)
                }
                val loadKey = "load:${s.id}"
                Pill(if (armed == loadKey) "LOAD?" else "LOAD", lit = true, warn = armed == loadKey) {
                    if (armed == loadKey) { onLoad(s); armed = null } else armed = loadKey
                }
                val delKey = "del:${s.id}"
                Pill(if (armed == delKey) "DELETE?" else "✕", lit = false, warn = armed == delKey) {
                    if (armed == delKey) { onDelete(s); armed = null } else armed = delKey
                }
            }
        }
    }
}

@Composable
private fun Pill(text: String, lit: Boolean, warn: Boolean = false, onClick: () -> Unit) {
    val color = if (warn) SWarn else if (lit) SLit else SLabel
    Box(
        Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (warn) SWarn.copy(alpha = 0.15f) else Color(0xFF242424))
            .border(1.dp, color.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text, color = color, fontSize = 12.sp, fontWeight = FontWeight.Bold)
    }
}
