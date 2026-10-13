package io.github.fm1deck.ui.panel.skin

/**
 * The four blocks of the upper panel zone.
 * Fixed inner layouts per docs/SKINS.md §3.
 */
enum class BlockId(val jsonName: String) {
    SYSTEM("system"),
    BUTTONS("buttons"),
    KNOBS("knobs"),
    SCREEN("screen");

    companion object {
        fun fromJson(name: String): BlockId? = entries.firstOrNull { it.jsonName == name }
    }
}

/**
 * A node in the weighted row/column layout tree for the top panel zone.
 */
sealed interface LayoutNode {
    val weight: Float
}

data class RowNode(
    override val weight: Float,
    val gap: Float,
    val children: List<LayoutNode>
) : LayoutNode

data class ColumnNode(
    override val weight: Float,
    val gap: Float,
    val children: List<LayoutNode>
) : LayoutNode

data class BlockNode(
    val block: BlockId,
    override val weight: Float,
    val align: String = "center"
) : LayoutNode

data class PanelConfig(
    val padding: FloatArray, // [left, top, right, bottom] in u
    val gap: Float,          // gap between top zone and keyboard in u
    val keyboardFraction: Float, // share of panel height (e.g. 0.36)
    val minKeyboardDp: Float,    // min keyboard height in dp
    val top: LayoutNode          // root node of the top area
)

data class FramesConfig(
    val system: Boolean,
    val buttons: Boolean,
    val knobs: Boolean,
    val screen: Boolean
) {
    fun hasFrame(blockId: BlockId): Boolean = when (blockId) {
        BlockId.SYSTEM -> system
        BlockId.BUTTONS -> buttons
        BlockId.KNOBS -> knobs
        BlockId.SCREEN -> screen
    }
}

enum class GearPosition(val jsonName: String) {
    TOP_LEFT("top-left"),
    TOP_RIGHT("top-right");

    companion object {
        fun fromJson(str: String?): GearPosition =
            if (str == "top-left") TOP_LEFT else TOP_RIGHT
    }
}

/**
 * How the phone is held. Landscape skins are laid out from their block tree; a portrait skin has one fixed
 * "gamepad" layout (SkinLayout.calculatePortrait): the screen between the two pairs of side knobs, KNOB 1-4, the 2x6
 * buttons, OCT with F, the keyboard at the bottom.
 */
enum class SkinOrientation(val jsonName: String) {
    LANDSCAPE("landscape"),
    PORTRAIT("portrait");

    companion object {
        fun fromJson(str: String?): SkinOrientation = if (str == "portrait") PORTRAIT else LANDSCAPE
    }
}

data class SkinName(
    val en: String
)

data class Skin(
    val schema: Int,
    val id: String,
    val order: Int,
    val name: SkinName,
    val reference: String,
    val panel: PanelConfig,
    val frames: FramesConfig,
    val separators: List<String>,
    val palette: String,
    val gear: GearPosition = GearPosition.TOP_RIGHT,
    val isUserSkin: Boolean = false,
    val orientation: SkinOrientation = SkinOrientation.LANDSCAPE
) {
    val isPortrait: Boolean get() = orientation == SkinOrientation.PORTRAIT
}
