# Skin file format (schema 1)

Each `*.json` here is one panel layout. Loaded by `SkinRepository` (P2), validated by `SkinSchemaTest`.
Full rationale and the scaling rules: `docs/SKINS.md`.

Units: **u** = panel height / 440 (the reference screenshots are ≈950×440). Widths come from weights, so a skin
fills any landscape aspect (Samsung M52 ≈ 2.22:1, POCO C75 ≈ 2.28:1) without letterboxing.

```jsonc
{
  "schema": 1,
  "id": "classic",                       // stable, lowercase; stored in settings
  "order": 1,                            // position in the picker
  "name": { "en": "Classic" },
  "reference": "skin-1.png",             // the reference screenshot of this layout (informational)
  "panel": {
    "padding": [18, 14, 18, 8],          // u: left, top, right, bottom (of the whole panel, inside safe insets)
    "gap": 10,                           // u between the top area and the keyboard
    "keyboardFraction": 0.36,            // share of the panel height for the keyboard (clamped by minKeyboardDp)
    "minKeyboardDp": 112,
    "top": { "type": "row", "gap": 14, "children": [ /* nodes */ ] }
  },
  "frames": { "system": true, "buttons": true, "knobs": false, "screen": true },
  "separators": ["buttons/knobs"],      // thin line between two blocks of one column
  "palette": "graphite",                 // name in palettes.json (P2), colours for frames, keys, LEDs
  "gear": "top-right"                    // optional: settings icon corner (top-left | top-right), default top-right
}
```

Node = `{ "type": "row" | "column", "gap": u, "weight": w, "children": [...] }`
or a block leaf `{ "block": "system" | "buttons" | "knobs" | "screen", "weight": w, "align": "center" }`.
Every skin contains each block exactly once; the keyboard is not a block (always full width at the bottom).

Blocks have fixed inner layouts (docs/SKINS.md §Blocks): `system` = MASTER/SELECT, PRESETS/ALGORITHM, OCT−/OCT+;
`buttons` = 2×6 in the firmware's label order; `knobs` = KNOB 1–4; `screen` = 240×240 LCD, square.
