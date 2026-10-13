package io.github.fm1deck.ui.panel.skin

import android.content.res.AssetManager
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream

/**
 * Loads, parses and validates FM1 DECK panel skins (schema 1).
 * Built-in skins are read from assets/skins/ (.json files), user skins from filesDir/skins/.
 */
object SkinRepository {
    const val CURRENT_SCHEMA = 1

    val BUILT_IN_IDS = listOf(
        "classic",
        "knobs-top",
        "screen-left",
        "screen-left-knobs-top",
        "screen-center",
        "screen-center-buttons-top"
    )

    /**
     * Parses and validates a skin JSON string. Throws [IllegalArgumentException] on schema or layout errors.
     */
    fun parseSkin(jsonString: String, isUserSkin: Boolean = false): Skin {
        val root = JSONObject(jsonString)
        val schema = root.optInt("schema", -1)
        require(schema == CURRENT_SCHEMA) { "Unsupported skin schema: $schema (expected $CURRENT_SCHEMA)" }

        val id = root.optString("id", "").trim()
        require(id.isNotBlank()) { "Skin id cannot be blank" }

        val order = root.optInt("order", 999)
        val nameObj = root.optJSONObject("name")
        val nameEn = nameObj?.optString("en", "") ?: id
        val name = SkinName(en = nameEn)

        val reference = root.optString("reference", "")

        val panelObj = root.optJSONObject("panel")
            ?: throw IllegalArgumentException("Missing 'panel' object in skin $id")
        val panel = parsePanelConfig(panelObj, id)

        val framesObj = root.optJSONObject("frames")
            ?: throw IllegalArgumentException("Missing 'frames' object in skin $id")
        val frames = FramesConfig(
            system = framesObj.optBoolean("system", true),
            buttons = framesObj.optBoolean("buttons", true),
            knobs = framesObj.optBoolean("knobs", false),
            screen = framesObj.optBoolean("screen", true)
        )

        val separatorsArr = root.optJSONArray("separators")
        val separators = mutableListOf<String>()
        if (separatorsArr != null) {
            for (i in 0 until separatorsArr.length()) {
                separators.add(separatorsArr.getString(i))
            }
        }

        val palette = root.optString("palette", "graphite")
        require(palette.isNotBlank()) { "Palette cannot be blank in skin $id" }

        val gearStr = if (root.has("gear")) root.getString("gear") else null
        val gear = GearPosition.fromJson(gearStr)

        // Validate that all four blocks are present exactly once in the layout tree
        val blockCounts = mutableMapOf<BlockId, Int>()
        collectBlocks(panel.top, blockCounts)
        for (block in BlockId.entries) {
            val count = blockCounts[block] ?: 0
            require(count == 1) {
                "Skin $id must contain block '${block.jsonName}' exactly once, found $count"
            }
        }

        return Skin(
            schema = schema,
            id = id,
            order = order,
            name = name,
            reference = reference,
            panel = panel,
            frames = frames,
            separators = separators,
            palette = palette,
            gear = gear,
            isUserSkin = isUserSkin
        )
    }

    private fun parsePanelConfig(obj: JSONObject, skinId: String): PanelConfig {
        val padArr = obj.optJSONArray("padding")
            ?: throw IllegalArgumentException("Missing 'padding' array in skin $skinId")
        require(padArr.length() == 4) { "Padding array must contain 4 values [left, top, right, bottom]" }
        val padding = FloatArray(4) { i ->
            val v = padArr.getDouble(i).toFloat()
            require(v >= 0f) { "Padding must be non-negative" }
            v
        }

        val gap = obj.optDouble("gap", 8.0).toFloat()
        require(gap >= 0f) { "Panel gap must be non-negative" }

        val keyboardFraction = obj.optDouble("keyboardFraction", 0.36).toFloat()
        require(keyboardFraction in 0.1f..0.9f) { "keyboardFraction must be between 0.1 and 0.9" }

        val minKeyboardDp = obj.optDouble("minKeyboardDp", 112.0).toFloat()
        require(minKeyboardDp > 0f) { "minKeyboardDp must be positive" }

        val topObj = obj.optJSONObject("top")
            ?: throw IllegalArgumentException("Missing 'top' node in skin $skinId")
        val topNode = parseNode(topObj, defaultWeight = 1.0f)

        return PanelConfig(
            padding = padding,
            gap = gap,
            keyboardFraction = keyboardFraction,
            minKeyboardDp = minKeyboardDp,
            top = topNode
        )
    }

    private fun parseNode(obj: JSONObject, defaultWeight: Float): LayoutNode {
        val weight = obj.optDouble("weight", defaultWeight.toDouble()).toFloat()
        require(weight > 0f) { "Node weight must be greater than 0" }

        if (obj.has("block")) {
            val blockName = obj.getString("block")
            val blockId = BlockId.fromJson(blockName)
                ?: throw IllegalArgumentException("Unknown block name '$blockName'")
            val align = obj.optString("align", "center")
            return BlockNode(block = blockId, weight = weight, align = align)
        }

        val type = obj.optString("type", "")
        val gap = obj.optDouble("gap", 0.0).toFloat()
        require(gap >= 0f) { "Node gap must be non-negative" }

        val childrenArr = obj.optJSONArray("children")
            ?: throw IllegalArgumentException("Container node of type '$type' must have 'children'")
        require(childrenArr.length() > 0) { "Container node of type '$type' must have at least one child" }

        val children = mutableListOf<LayoutNode>()
        for (i in 0 until childrenArr.length()) {
            val childObj = childrenArr.getJSONObject(i)
            children.add(parseNode(childObj, defaultWeight = 1.0f))
        }

        return when (type) {
            "row" -> RowNode(weight = weight, gap = gap, children = children)
            "column" -> ColumnNode(weight = weight, gap = gap, children = children)
            else -> throw IllegalArgumentException("Unknown node type '$type' (expected 'row' or 'column')")
        }
    }

    private fun collectBlocks(node: LayoutNode, counts: MutableMap<BlockId, Int>) {
        when (node) {
            is BlockNode -> {
                counts[node.block] = (counts[node.block] ?: 0) + 1
            }
            is RowNode -> {
                for (child in node.children) collectBlocks(child, counts)
            }
            is ColumnNode -> {
                for (child in node.children) collectBlocks(child, counts)
            }
        }
    }

    /**
     * Loads a built-in skin from assets by its ID.
     */
    fun loadBuiltIn(assetManager: AssetManager, id: String): Skin {
        val json = assetManager.open("skins/$id.json").bufferedReader().use { it.readText() }
        return parseSkin(json, isUserSkin = false)
    }

    /**
     * Loads all 6 built-in skins from assets sorted by order.
     */
    fun loadAllBuiltIns(assetManager: AssetManager): List<Skin> {
        return BUILT_IN_IDS.map { loadBuiltIn(assetManager, it) }.sortedBy { it.order }
    }

    /**
     * Loads user skins from filesDir/skins/ directory.
     */
    fun loadUserSkins(skinsDir: File): List<Skin> {
        if (!skinsDir.exists() || !skinsDir.isDirectory) return emptyList()
        val files = skinsDir.listFiles { _, name -> name.endsWith(".json", ignoreCase = true) } ?: return emptyList()
        return files.mapNotNull { file ->
            runCatching {
                val json = file.readText()
                parseSkin(json, isUserSkin = true)
            }.getOrNull()
        }.sortedBy { it.order }
    }

    /**
     * Loads all built-in and user skins, sorted by order.
     * User skins can override built-in skins with the same ID.
     */
    fun loadAll(assetManager: AssetManager, skinsDir: File?): List<Skin> {
        val builtIns = loadAllBuiltIns(assetManager)
        val userSkins = if (skinsDir != null) loadUserSkins(skinsDir) else emptyList()
        val map = linkedMapOf<String, Skin>()
        for (b in builtIns) map[b.id] = b
        for (u in userSkins) map[u.id] = u
        return map.values.sortedBy { it.order }
    }

    /**
     * Helper to load skins from a filesystem directory (e.g. for host tests).
     */
    fun loadFromDirectory(dir: File): List<Skin> {
        if (!dir.exists() || !dir.isDirectory) return emptyList()
        val files = dir.listFiles { _, name -> name.endsWith(".json", ignoreCase = true) } ?: return emptyList()
        return files.mapNotNull { file ->
            runCatching {
                parseSkin(file.readText(), isUserSkin = false)
            }.getOrNull()
        }.sortedBy { it.order }
    }
}
