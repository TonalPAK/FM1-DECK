package io.github.fm1deck.ui.panel

import io.github.fm1deck.ui.panel.skin.BlockId
import io.github.fm1deck.ui.panel.skin.BlockNode
import io.github.fm1deck.ui.panel.skin.ColumnNode
import io.github.fm1deck.ui.panel.skin.LayoutNode
import io.github.fm1deck.ui.panel.skin.RowNode
import io.github.fm1deck.ui.panel.skin.Skin
import kotlin.math.abs

/**
 * Pure Kotlin 2D geometry primitives.
 * Completely independent of Android framework graphics classes for safe host unit testing.
 */
data class Rect(
    val left: Float,
    val top: Float,
    val right: Float,
    val bottom: Float
) {
    val width: Float get() = right - left
    val height: Float get() = bottom - top
    val centerX: Float get() = (left + right) / 2f
    val centerY: Float get() = (top + bottom) / 2f

    fun contains(x: Float, y: Float): Boolean =
        x >= left && x <= right && y >= top && y <= bottom

    fun intersects(other: Rect): Boolean =
        left < other.right && right > other.left && top < other.bottom && bottom > other.top
}

data class Point(val x: Float, val y: Float)

data class Insets(
    val left: Float = 0f,
    val top: Float = 0f,
    val right: Float = 0f,
    val bottom: Float = 0f,
    /** Radius of the display's rounded top-left corner, px (0 = square or unknown). */
    val cornerRadius: Float = 0f
) {
    companion object {
        val ZERO = Insets()
    }
}

/**
 * Semantic targets identified by hit-testing.
 */
sealed interface PanelTarget {
    data class Button(val index: Int) : PanelTarget
    data class Knob(val role: Int) : PanelTarget
    object MasterPot : PanelTarget
    data class Key(val bit: Int) : PanelTarget
    object Screen : PanelTarget
    object Gear : PanelTarget
    /** The app's F (latch) button: not a firmware button. */
    object Fn : PanelTarget
}

/**
 * Geometric elements within the panel layout.
 */
sealed class PanelElement {
    data class Button(
        val index: Int,
        val rect: Rect,
        val hitRect: Rect,
        val label: String
    ) : PanelElement()

    data class Knob(
        val role: Int,
        val hitRect: Rect,
        val knobCenter: Point,
        val knobRadius: Float,
        val defaultLabel: String
    ) : PanelElement()

    data class MasterPot(
        val hitRect: Rect,
        val knobCenter: Point,
        val knobRadius: Float
    ) : PanelElement()

    data class Key(
        val bit: Int,
        val isBlack: Boolean,
        val hitRect: Rect,
        val capsuleRect: Rect,
        val stripRect: Rect
    ) : PanelElement()

    data class Screen(
        val bounds: Rect,
        val frameRect: Rect,
        val lcdRect: Rect
    ) : PanelElement()

    data class Gear(
        val hitRect: Rect,
        val center: Point,
        val radius: Float
    ) : PanelElement()
}

data class BlockGeometry(
    val blockId: BlockId,
    val bounds: Rect,
    val hasFrame: Boolean
)

data class SeparatorLine(
    val id: String,
    val start: Point,
    val end: Point
)

/**
 * Immutable layout calculation result for a panel skin.
 */
data class PanelGeometry(
    val skinId: String,
    val widthPx: Float,
    val heightPx: Float,
    val density: Float,
    val safeInsets: Insets,
    val u: Float,
    val topAreaRect: Rect,
    val keyboardRect: Rect,
    val blocks: Map<BlockId, BlockGeometry>,
    val buttons: List<PanelElement.Button>,
    val knobs: List<PanelElement.Knob>,
    val masterPot: PanelElement.MasterPot,
    val keys: List<PanelElement.Key>,
    val screen: PanelElement.Screen,
    val gear: PanelElement.Gear,
    val separators: List<SeparatorLine>,
    /** The dark island under OCT- / OCT+ (as the browser emulator's tray). */
    val octTray: Rect = Rect(0f, 0f, 0f, 0f),
    /** The mini F (latch) button left of MASTER's label; its hit target is a little larger. */
    val fnRect: Rect = Rect(0f, 0f, 0f, 0f),
    val fnHitRect: Rect = Rect(0f, 0f, 0f, 0f)
) {
    private val buttonsByIndex = buttons.associateBy { it.index }
    private val knobsByRole = knobs.associateBy { it.role }
    private val keysByBit = keys.associateBy { it.bit }

    fun getButton(index: Int): PanelElement.Button? = buttonsByIndex[index]
    fun getKnob(role: Int): PanelElement.Knob? = knobsByRole[role]
    fun getKey(bit: Int): PanelElement.Key? = keysByBit[bit]

    /**
     * Hit-tests coordinates (x, y) in panel space to a semantic target.
     */
    fun hitTest(x: Float, y: Float): PanelTarget? {
        if (gear.hitRect.contains(x, y)) return PanelTarget.Gear
        if (fnHitRect.contains(x, y)) return PanelTarget.Fn

        // Fast path: keyboard area
        if (keyboardRect.contains(x, y)) {
            val halfY = keyboardRect.top + keyboardRect.height / 2f
            if (y < halfY) {
                // Check black keys in top half first
                for (k in keys) {
                    if (k.isBlack && k.hitRect.contains(x, y)) {
                        return PanelTarget.Key(k.bit)
                    }
                }
            }
            // Fall through to white keys: the cell under x (the outer cells reach the keybed's edges)
            var best: PanelElement.Key? = null
            for (k in keys) {
                if (k.isBlack) continue
                if (x >= k.hitRect.left && x <= k.hitRect.right) return PanelTarget.Key(k.bit)
                if (best == null || abs(k.hitRect.centerX - x) < abs(best.hitRect.centerX - x)) best = k
            }
            return best?.let { PanelTarget.Key(it.bit) }
        }

        // Check buttons (FX..REC, OCT-, OCT+)
        for (b in buttons) {
            if (b.hitRect.contains(x, y)) return PanelTarget.Button(b.index)
        }

        // Check knobs (K1..K4, SELECT, ALGO, PRESET)
        for (k in knobs) {
            if (k.hitRect.contains(x, y)) return PanelTarget.Knob(k.role)
        }

        // Check master pot
        if (masterPot.hitRect.contains(x, y)) return PanelTarget.MasterPot

        // Check screen block
        if (screen.bounds.contains(x, y)) return PanelTarget.Screen

        return null
    }
}

/**
 * Pure Kotlin layout engine for FM1 DECK skins.
 */
object SkinLayout {
    // 16 white keys chromatic semitones from F3 (MIDI 53)
    val MW_WHITE_SEMITONES = intArrayOf(0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26)
    // Black keys placed after corresponding white keys (0=F3 -> F#3, 1=G3 -> G#3, etc.)
    val MW_BLACK_AFTER_WHITE = intArrayOf(0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14)
    const val WHITE_KEYS_COUNT = 16
    const val BLACK_KEYS_COUNT = 11
    const val TOTAL_KEYS_COUNT = 27

    // Default button names
    val ROW0_BUTTON_LABELS = listOf("FX", "SCL", "ENV", "LFO", "EDIT", "GLO")
    val ROW1_BUTTON_LABELS = listOf("HOME", "SAVE", "ARP", "SEQ", "PLAY\nSTOP", "REC")

    // Encoder roles from Fm1FirmwareApi.h
    const val ROLE_SELECT = 0
    const val ROLE_ALGO = 1
    const val ROLE_PRESET = 2
    const val ROLE_K1 = 3
    const val ROLE_K2 = 4
    const val ROLE_K3 = 5
    const val ROLE_K4 = 6

    // Button indices from Fm1FirmwareApi.h
    const val BTN_OCTDN = 12
    const val BTN_OCTUP = 13

    fun calculate(
        skin: Skin,
        widthPx: Float,
        heightPx: Float,
        density: Float,
        safeInsets: Insets = Insets.ZERO
    ): PanelGeometry {
        val m = maxOf(safeInsets.left, safeInsets.right)
        val safeTop = safeInsets.top
        val safeBottom = safeInsets.bottom
        val safeHeight = heightPx - safeTop - safeBottom

        val u = safeHeight / 440f

        val padH = if (skin.panel.padding.size >= 4) {
            maxOf(skin.panel.padding[0], skin.panel.padding[2]) * u
        } else {
            18f * u
        }
        val padTop = skin.panel.padding[1] * u
        val padBottom = skin.panel.padding[3] * u

        val minKeyboardPx = skin.panel.minKeyboardDp * density
        val keyboardHeight = maxOf(safeHeight * skin.panel.keyboardFraction, minKeyboardPx)
        val panelGap = skin.panel.gap * u

        val topAreaHeight = safeHeight - keyboardHeight - panelGap - padTop - padBottom
        val topAreaWidth = widthPx - 2f * m - 2f * padH
        val topAreaRect = Rect(
            left = m + padH,
            top = safeTop + padTop,
            right = widthPx - m - padH,
            bottom = safeTop + padTop + topAreaHeight
        )

        // Keyboard spans full width minus small symmetric margin (2 * u)
        val keyboardMargin = 2f * u
        val keyboardRect = Rect(
            left = keyboardMargin,
            top = safeTop + safeHeight - keyboardHeight,
            right = widthPx - keyboardMargin,
            bottom = safeTop + safeHeight
        )

        // Recursively lay out top area tree
        val blockBoundsMap = mutableMapOf<BlockId, Rect>()
        layoutNode(skin.panel.top, topAreaRect, u, blockBoundsMap)

        // Settings: a small dot in the top-left corner margin. It takes no space from any block.
        // Its touch target is a square in the screen's own corner (in landscape the cutout sits mid-height of a short
        // edge, so the corner is free), sized at the end (fitGearTarget) to stay clear of every control.
        // On a rounded display the dot moves along the diagonal until it sits wholly inside the visible arc, with a
        // small rim, so it never hides under the corner.
        val gearRadius = GEAR_DOT_U * u
        val cornerR = safeInsets.cornerRadius
        val inCorner = if (cornerR > 0f) cornerR - (cornerR - gearRadius - GEAR_RIM_U * u) / SQRT2 else 0f
        val gearCenter = Point(maxOf(padH * 0.5f, inCorner), maxOf(safeTop + padTop * 0.5f, inCorner))

        val blocks = BlockId.entries.associateWith { id ->
            val rect = blockBoundsMap[id] ?: Rect(0f, 0f, 0f, 0f)
            BlockGeometry(
                blockId = id,
                bounds = rect,
                hasFrame = skin.frames.hasFrame(id)
            )
        }

        // Lay out screen: largest square that fits its block minus frame padding (3 * u)
        val screenBounds = blockBoundsMap[BlockId.SCREEN] ?: Rect(0f, 0f, 0f, 0f)
        val frameSize = minOf(screenBounds.width, screenBounds.height)
        val frameRect = Rect(
            left = screenBounds.centerX - frameSize / 2f,
            top = screenBounds.centerY - frameSize / 2f,
            right = screenBounds.centerX + frameSize / 2f,
            bottom = screenBounds.centerY + frameSize / 2f
        )
        // The bezel around the 240 x 240 panel: at least 3 u, else 1/48 of the side (the emulator uses 1/32)
        val framePadding = maxOf(3f * u, frameSize / 48f)
        val lcdSize = frameSize - 2f * framePadding
        val lcdRect = Rect(
            left = frameRect.centerX - lcdSize / 2f,
            top = frameRect.centerY - lcdSize / 2f,
            right = frameRect.centerX + lcdSize / 2f,
            bottom = frameRect.centerY + lcdSize / 2f
        )
        val screenElement = PanelElement.Screen(
            bounds = screenBounds,
            frameRect = frameRect,
            lcdRect = lcdRect
        )

        // Lay out knobs block (K1..K4)
        val knobsBounds = blockBoundsMap[BlockId.KNOBS] ?: Rect(0f, 0f, 0f, 0f)
        val kColW = knobsBounds.width / 4f
        val kLabelH = minOf(18f * u, knobsBounds.height * 0.28f)
        val kKnobH = knobsBounds.height - kLabelH
        val kKnobDiameter = minOf(kColW * 0.72f, kKnobH * 0.82f)
        val kKnobRadius = kKnobDiameter / 2f

        val knobsList = mutableListOf<PanelElement.Knob>()
        val kRoles = intArrayOf(ROLE_K1, ROLE_K2, ROLE_K3, ROLE_K4)
        for (i in 0..3) {
            val colLeft = knobsBounds.left + i * kColW
            val colRight = colLeft + kColW
            val colRect = Rect(colLeft, knobsBounds.top, colRight, knobsBounds.bottom)
            val centerY = knobsBounds.top + kLabelH + kKnobH / 2f
            knobsList.add(
                PanelElement.Knob(
                    role = kRoles[i],
                    hitRect = colRect,
                    knobCenter = Point(colRect.centerX, centerY),
                    knobRadius = kKnobRadius,
                    defaultLabel = "KNOB ${i + 1}"
                )
            )
        }

        // Lay out buttons block (2x6)
        // The block is the dark tray; the twelve buttons sit inside it with the emulator's grooves
        val buttonsBlock = blockBoundsMap[BlockId.BUTTONS] ?: Rect(0f, 0f, 0f, 0f)
        val trayPad = 6f * u
        val buttonsBounds = Rect(
            buttonsBlock.left + trayPad, buttonsBlock.top + trayPad,
            buttonsBlock.right - trayPad, buttonsBlock.bottom - trayPad
        )
        val btnGapX = 6f * u
        val btnGapY = 7f * u
        val btnW = (buttonsBounds.width - 5f * btnGapX) / 6f
        val btnH = (buttonsBounds.height - btnGapY) / 2f

        val buttonsList = mutableListOf<PanelElement.Button>()
        // Row 0
        for (c in 0..5) {
            val bLeft = buttonsBounds.left + c * (btnW + btnGapX)
            val bTop = buttonsBounds.top
            val bRect = Rect(bLeft, bTop, bLeft + btnW, bTop + btnH)
            buttonsList.add(
                PanelElement.Button(
                    index = c,
                    rect = bRect,
                    hitRect = bRect,
                    label = ROW0_BUTTON_LABELS[c]
                )
            )
        }
        // Row 1
        for (c in 0..5) {
            val bLeft = buttonsBounds.left + c * (btnW + btnGapX)
            val bTop = buttonsBounds.top + btnH + btnGapY
            val bRect = Rect(bLeft, bTop, bLeft + btnW, bTop + btnH)
            buttonsList.add(
                PanelElement.Button(
                    index = 6 + c,
                    rect = bRect,
                    hitRect = bRect,
                    label = ROW1_BUTTON_LABELS[c]
                )
            )
        }

        // Lay out system block: 2x2 knobs + OCT-/OCT+
        val sysBounds = blockBoundsMap[BlockId.SYSTEM] ?: Rect(0f, 0f, 0f, 0f)
        val octH = minOf(40f * u, sysBounds.height * 0.25f)
        val octGapY = 8f * u
        val sysKnobsH = sysBounds.height - octH - octGapY
        val sysKnobsRect = Rect(sysBounds.left, sysBounds.top, sysBounds.right, sysBounds.top + sysKnobsH)

        val sysColW = sysKnobsRect.width / 2f
        val sysRowH = sysKnobsRect.height / 2f
        val sysLabelH = minOf(16f * u, sysRowH * 0.28f)
        val sysKnobDiameter = minOf(sysColW * 0.68f, (sysRowH - sysLabelH) * 0.82f)
        val sysKnobRadius = sysKnobDiameter / 2f

        // MASTER (row 0, col 0)
        val masterHit = Rect(sysBounds.left, sysBounds.top, sysBounds.left + sysColW, sysBounds.top + sysRowH)
        val masterCenterY = sysBounds.top + sysLabelH + (sysRowH - sysLabelH) / 2f
        val masterPot = PanelElement.MasterPot(
            hitRect = masterHit,
            knobCenter = Point(masterHit.centerX, masterCenterY),
            knobRadius = sysKnobRadius
        )

        // F (latch): a mini button level with the gap between MASTER and PRESETS, in the panel's side margin next to
        // the system block (left of it, or right of it when the block stands at the right edge), as the reference layouts place it
        // (2026-10-12). Only when that margin is too narrow does it fall back inside the block, at the left of the
        // label strip between MASTER and PRESETS (a corner no well reaches).
        val fnWantW = minOf(sysColW * 0.22f, 30f * u) * 1.3f
        val fnWantH = minOf(sysLabelH * 0.95f, fnWantW * 0.85f / 1.3f) * 1.3f
        val fnCy = sysBounds.top + sysRowH + sysLabelH / 2f
        val sideGap = 4f * u
        val leftRoom = sysBounds.left - 2f * sideGap                       // from the screen edge to the block
        val rightRoom = widthPx - sysBounds.right - 2f * sideGap
        val atLeft = sysBounds.left - topAreaRect.left < 1f
        val atRight = topAreaRect.right - sysBounds.right < 1f
        val fnRect = when {
            atLeft && leftRoom >= fnWantW * 0.7f -> {
                val w = minOf(fnWantW, leftRoom)
                val cx = (sideGap + sysBounds.left - sideGap) / 2f
                Rect(cx - w / 2f, fnCy - fnWantH / 2f, cx + w / 2f, fnCy + fnWantH / 2f)
            }
            atRight && rightRoom >= fnWantW * 0.7f -> {
                val w = minOf(fnWantW, rightRoom)
                val cx = (sysBounds.right + sideGap + widthPx - sideGap) / 2f
                Rect(cx - w / 2f, fnCy - fnWantH / 2f, cx + w / 2f, fnCy + fnWantH / 2f)
            }
            else -> {
                val w = fnWantW / 1.3f
                val h = minOf(sysLabelH * 0.95f, w * 0.85f)
                Rect(sysBounds.left, fnCy - h / 2f, sysBounds.left + w, fnCy + h / 2f)
            }
        }
        val fnHitRect = Rect(fnRect.left - 4f * u, fnRect.top - 6f * u, fnRect.right + 4f * u, fnRect.bottom + 6f * u)

        // SELECT (row 0, col 1)
        val selectHit = Rect(sysBounds.left + sysColW, sysBounds.top, sysBounds.right, sysBounds.top + sysRowH)
        val selectCenterY = sysBounds.top + sysLabelH + (sysRowH - sysLabelH) / 2f
        knobsList.add(
            PanelElement.Knob(
                role = ROLE_SELECT,
                hitRect = selectHit,
                knobCenter = Point(selectHit.centerX, selectCenterY),
                knobRadius = sysKnobRadius,
                defaultLabel = "SELECT"
            )
        )

        // PRESETS (row 1, col 0)
        val presetHit = Rect(sysBounds.left, sysBounds.top + sysRowH, sysBounds.left + sysColW, sysKnobsRect.bottom)
        val presetCenterY = sysBounds.top + sysRowH + sysLabelH + (sysRowH - sysLabelH) / 2f
        knobsList.add(
            PanelElement.Knob(
                role = ROLE_PRESET,
                hitRect = presetHit,
                knobCenter = Point(presetHit.centerX, presetCenterY),
                knobRadius = sysKnobRadius,
                defaultLabel = "PRESETS"
            )
        )

        // ALGORITHM (row 1, col 1)
        val algoHit = Rect(sysBounds.left + sysColW, sysBounds.top + sysRowH, sysBounds.right, sysKnobsRect.bottom)
        val algoCenterY = sysBounds.top + sysRowH + sysLabelH + (sysRowH - sysLabelH) / 2f
        knobsList.add(
            PanelElement.Knob(
                role = ROLE_ALGO,
                hitRect = algoHit,
                knobCenter = Point(algoHit.centerX, algoCenterY),
                knobRadius = sysKnobRadius,
                defaultLabel = "ALGORITHM"
            )
        )

        // OCT- / OCT+ on their own dark island at the bottom of the system block
        val octTray = Rect(sysBounds.left, sysBounds.bottom - octH, sysBounds.right, sysBounds.bottom)
        val octPadY = octH * 0.16f
        val octPadX = 5f * u
        val octGapX = 8f * u
        val octW = (octTray.width - 2f * octPadX - octGapX) / 2f
        val octTop = octTray.top + octPadY
        val octBottom = octTray.bottom - octPadY
        val octDnRect = Rect(octTray.left + octPadX, octTop, octTray.left + octPadX + octW, octBottom)
        val octUpRect = Rect(octDnRect.right + octGapX, octTop, octTray.right - octPadX, octBottom)
        buttonsList.add(
            PanelElement.Button(
                index = BTN_OCTDN,
                rect = octDnRect,
                hitRect = Rect(octTray.left, octTray.top, octTray.centerX, octTray.bottom),
                label = "OCT-"
            )
        )
        buttonsList.add(
            PanelElement.Button(
                index = BTN_OCTUP,
                rect = octUpRect,
                hitRect = Rect(octTray.centerX, octTray.top, octTray.right, octTray.bottom),
                label = "OCT+"
            )
        )

        // Lay out keyboard (27 keys: 16 white, 11 black) after the emulator's keybed: a side inset of 12/928 of the
        // width, capsules 0.78 of the pitch wide and 0.425 of the bed high, centred on 1/4 and 3/4 of its height, each
        // with a slit down its middle (kw * 0.4 from both ends)
        val bedInset = keyboardRect.width * (12f / 928f)
        val keyStep = (keyboardRect.width - 2f * bedInset) / WHITE_KEYS_COUNT
        val keyRowH = keyboardRect.height / 2f
        val capsuleW = keyStep * 0.78f
        val capsuleH = keyboardRect.height * 0.425f
        val stripW = maxOf(2f * density, capsuleW * 0.14f)
        val stripH = maxOf(capsuleH * 0.3f, capsuleH - capsuleW * 0.8f)
        val keysLeft = keyboardRect.left + bedInset

        val keysList = mutableListOf<PanelElement.Key>()

        // 16 White keys (lower row)
        for (i in 0 until WHITE_KEYS_COUNT) {
            val bit = MW_WHITE_SEMITONES[i]
            val cellLeft = if (i == 0) keyboardRect.left else keysLeft + i * keyStep
            val cellRight = if (i == WHITE_KEYS_COUNT - 1) keyboardRect.right else keysLeft + (i + 1) * keyStep
            val hitRect = Rect(cellLeft, keyboardRect.top + keyRowH, cellRight, keyboardRect.bottom)
            val cx = keysLeft + (i + 0.5f) * keyStep
            val cy = keyboardRect.top + keyboardRect.height * 0.75f
            val capRect = Rect(cx - capsuleW / 2f, cy - capsuleH / 2f, cx + capsuleW / 2f, cy + capsuleH / 2f)
            val stripRect = Rect(cx - stripW / 2f, cy - stripH / 2f, cx + stripW / 2f, cy + stripH / 2f)
            keysList.add(
                PanelElement.Key(
                    bit = bit,
                    isBlack = false,
                    hitRect = hitRect,
                    capsuleRect = capRect,
                    stripRect = stripRect
                )
            )
        }

        // 11 Black keys (upper row)
        for (wi in MW_BLACK_AFTER_WHITE) {
            val bit = MW_WHITE_SEMITONES[wi] + 1
            val cx = keysLeft + (wi + 1) * keyStep
            val cy = keyboardRect.top + keyboardRect.height * 0.25f
            val capRect = Rect(cx - capsuleW / 2f, cy - capsuleH / 2f, cx + capsuleW / 2f, cy + capsuleH / 2f)
            val stripRect = Rect(cx - stripW / 2f, cy - stripH / 2f, cx + stripW / 2f, cy + stripH / 2f)
            val hitW = keyStep * 0.90f
            val hitRect = Rect(cx - hitW / 2f, keyboardRect.top, cx + hitW / 2f, keyboardRect.top + keyRowH)
            keysList.add(
                PanelElement.Key(
                    bit = bit,
                    isBlack = true,
                    hitRect = hitRect,
                    capsuleRect = capRect,
                    stripRect = stripRect
                )
            )
        }

        // Sort keys by bit index (0..26)
        keysList.sortBy { it.bit }


        // Separators
        val separatorsList = mutableListOf<SeparatorLine>()
        for (sep in skin.separators) {
            when (sep) {
                "buttons/knobs" -> {
                    val b = blockBoundsMap[BlockId.BUTTONS]
                    val k = blockBoundsMap[BlockId.KNOBS]
                    if (b != null && k != null) {
                        val midY = (b.bottom + k.top) / 2f
                        val startX = minOf(b.left, k.left)
                        val endX = maxOf(b.right, k.right)
                        separatorsList.add(
                            SeparatorLine(
                                id = sep,
                                start = Point(startX, midY),
                                end = Point(endX, midY)
                            )
                        )
                    }
                }
                "knobs/buttons" -> {
                    val k = blockBoundsMap[BlockId.KNOBS]
                    val b = blockBoundsMap[BlockId.BUTTONS]
                    if (k != null && b != null) {
                        val midY = (k.bottom + b.top) / 2f
                        val startX = minOf(k.left, b.left)
                        val endX = maxOf(k.right, b.right)
                        separatorsList.add(
                            SeparatorLine(
                                id = sep,
                                start = Point(startX, midY),
                                end = Point(endX, midY)
                            )
                        )
                    }
                }
            }
        }

        return PanelGeometry(
            skinId = skin.id,
            widthPx = widthPx,
            heightPx = heightPx,
            density = density,
            safeInsets = safeInsets,
            u = u,
            topAreaRect = topAreaRect,
            keyboardRect = keyboardRect,
            blocks = blocks,
            buttons = buttonsList,
            knobs = knobsList,
            masterPot = masterPot,
            keys = keysList,
            screen = screenElement,
            gear = PanelElement.Gear(
                hitRect = fitGearTarget(
                    0f, safeTop, density, knobsList, masterPot, buttonsList, screenElement.lcdRect, keyboardRect, fnRect
                ),
                center = gearCenter,
                radius = gearRadius
            ),
            separators = separatorsList,
            octTray = octTray,
            fnRect = fnRect,
            fnHitRect = fnHitRect
        )
    }

    /** The settings dot's radius and its clearance from the rounded screen edge, u. */
    const val GEAR_DOT_U = 2.6f
    const val GEAR_RIM_U = 2f
    private const val SQRT2 = 1.4142135f

    /** Smallest settings target we accept: a corner square of 28 dp (the screen edge stops the finger there). */
    const val GEAR_MIN_DP = 28f

    /**
     * The largest corner square from 40 dp down to [GEAR_MIN_DP] that covers no knob well, button face, LCD or key.
     */
    private fun fitGearTarget(
        left: Float,
        top: Float,
        density: Float,
        knobs: List<PanelElement.Knob>,
        master: PanelElement.MasterPot,
        buttons: List<PanelElement.Button>,
        lcd: Rect,
        keybed: Rect,
        fn: Rect
    ): Rect {
        fun circleHits(r: Rect, cx: Float, cy: Float, radius: Float): Boolean {
            val nx = cx.coerceIn(r.left, r.right)
            val ny = cy.coerceIn(r.top, r.bottom)
            return (cx - nx) * (cx - nx) + (cy - ny) * (cy - ny) < radius * radius
        }
        var dp = 40f
        while (true) {
            val size = dp * density
            val r = Rect(left, top, left + size, top + size)
            val clear = knobs.none { circleHits(r, it.knobCenter.x, it.knobCenter.y, it.knobRadius) } &&
                !circleHits(r, master.knobCenter.x, master.knobCenter.y, master.knobRadius) &&
                buttons.none { r.intersects(it.rect) } &&
                !r.intersects(lcd) && !r.intersects(keybed) && !r.intersects(fn)
            if (clear || dp <= GEAR_MIN_DP) return r
            dp -= 1f
        }
    }

    private fun layoutNode(
        node: LayoutNode,
        rect: Rect,
        u: Float,
        blockBoundsMap: MutableMap<BlockId, Rect>
    ) {
        when (node) {
            is BlockNode -> {
                blockBoundsMap[node.block] = rect
            }
            is RowNode -> {
                val gapsTotal = (node.children.size - 1) * node.gap * u
                val availW = rect.width - gapsTotal
                val totalWeight = node.children.sumOf { it.weight.toDouble() }.toFloat()
                var curX = rect.left
                for (child in node.children) {
                    val childW = (child.weight / totalWeight) * availW
                    val childRect = Rect(curX, rect.top, curX + childW, rect.bottom)
                    layoutNode(child, childRect, u, blockBoundsMap)
                    curX += childW + node.gap * u
                }
            }
            is ColumnNode -> {
                val gapsTotal = (node.children.size - 1) * node.gap * u
                val availH = rect.height - gapsTotal
                val totalWeight = node.children.sumOf { it.weight.toDouble() }.toFloat()
                var curY = rect.top
                for (child in node.children) {
                    val childH = (child.weight / totalWeight) * availH
                    val childRect = Rect(rect.left, curY, rect.right, curY + childH)
                    layoutNode(child, childRect, u, blockBoundsMap)
                    curY += childH + node.gap * u
                }
            }
        }
    }
}
