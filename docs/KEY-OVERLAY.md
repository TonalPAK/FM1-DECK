# Hints on keys and buttons ("live feedback")

On the hardware you cannot label what the keys do; on a virtual panel it is easy. Hold a layer (or a button
combination) and the keys and knobs show what they do right now, as the firmware's author intended, while the
firmware's LEDs light up the current state (for example which tracks are muted). The device becomes much faster to
learn.

Status: note names that light up when the firmware offers keys as functions are done; the full hint engine below is
planned (P4).

## 1. Sources of truth

| What | From | Updated |
|---|---|---|
| Current layer, page, knob labels and values | **the firmware** (`FM1_CAP_UI_STATE`: `ui.layer`, `PAGES[ui.page]`, `page_desc`) | automatically with the firmware |
| A layer's key labels, descriptions, combos | `overlay.json` in the firmware package | with the package (CI) |
| State (what is on/muted) | the firmware's LEDs (`leds()`) | in real time |

## 2. The `overlay.json` format (schema 1)

```jsonc
{
  "schema": 1,
  "firmware": "jiant",
  "appliesTo": { "versions": ["v0.6.41"] },          // or { "min": "v0.6.30", "max": "v0.6.99" }
  "lang": "en",
  "source": { "repo": "…", "commit": "…", "file": "tools/gen_cheatsheets.py", "extractor": "…" },
  "layers": [{
    "id": "fx",
    "title": "FX · PUNCH-IN EFFECTS",
    "color": "#ff7a00",
    "trigger": { "held": ["FX"] },                    // + optional "uiLayer": "FX" (from ui_state)
    "keys": { "F3": { "label": "REP 1/8", "short": "R⅛", "desc": "…" }, "F#3": { "label": "M1" } },
    "allKeys": { "label": "ROOT {note}" },            // a template for every key (SCL: any key = root)
    "knobs": ["FILTER", "CRUSH", "THROW", "DEPTH"],
    "note": "Act while held, in on the next 1/16. …"
  }],
  "combos": [{ "held": ["REC"], "press": ["FX","EDIT","ENV","LFO","SCL","ARP","GLO"], "text": "Clear that part's automation…" }],
  "pages":  [{ "button": "FX", "taps": "TRACK · SLICER · …", "hold": "PUNCH-IN FX" }],
  "panel":  [{ "control": "SELECT", "text": "HOME: DICE EVERYTHING? …" }],
  "knobPages": { "DLY 2": ["WIDE", "PITCH", "SPRAY", ""] }
}
```

Translations: `overlay.<lang>.json` with the same ids and only the `label/short/desc/note/text/title` fields.
Validator: `tools/overlay/validate_overlay.py` (P4): note and button names, label lengths (`short` ≤ 6 characters,
`label` ≤ 12), no duplicates, `appliesTo` matches the package version.

### Where the data comes from

- **JIANT** — automatically: [`tools/overlay/extract_jiant_cheatsheet.py`](../tools/overlay/extract_jiant_cheatsheet.py)
  reads JIANT's `tools/gen_cheatsheets.py` via `ast` (without executing it) + a small hand-written `overrides.json`.
  The result for v0.6.41: [`fwbuild/overlays/jiant/overlay.json`](../fwbuild/overlays/jiant/overlay.json) —
  8 layers, 75 key labels, 8 combos, 53 pages with knob labels.
- **Felucca** — by hand (P4) from the README's "Controls", "Scale keyboard", `docs/controls.jpg`, `firmware/src/ui_layer.c`.
- **SLOOP** — by hand (P4) from `GUIDE.md` §5 "Every button", §9 "The layers", §16 "Every page", `ui_layers.c`.

## 3. Which hints to show (HintEngine)

Every UI frame (~60 Hz, cheap):

1. If there is `ui_state` and `layer ≠ NONE` → the overlay layer with `trigger.uiLayer == layer` (or with `held` ==
   that layer's button). This also catches a layer **latched by a double tap**.
2. Otherwise — buttons held (from `InputState`) longer than `hintDelay` (120 ms by default; it does not get in the way
   of fast page taps): the layer with the most specific `held` (the largest match) wins.
3. Combos: while a button from `combos[].held` is held, the buttons in `press` get a hint badge (a small tag above the
   button; the text shows in "Explain" mode).
4. Knob labels: the layer (`knobs`) → otherwise `ui_state.knob[]` (+ a value ring) → otherwise `knobPages[page_title]`
   (if the title is known) → otherwise "KNOB n".
5. Without `ui_state` and with nothing held — no hints (a clean panel), unless "Always show the current layer's
   labels" is on.

## 4. How it looks

- Keys: the label inside the pill (1–2 lines, white on the layer colour at 70 % opacity); keys with no function in
  this layer dim to 40 % — you see at once where to press. The firmware's LEDs are drawn on top: if the firmware lit a
  key (for example a muted track in GLO), the bar glows and the label stays readable.
- Knobs: the layer's label above the knob, in the layer colour.
- Layer strip: the title (`title`) and a short note (`note`) — a thin line above the keyboard that does not cover the
  keys (can be switched off).
- "Explain" mode (ⓘ in the panel's corner): touches **do not reach the firmware**; they show a popup description
  (`desc`/`text`/`panel`) of what was touched. Tap ⓘ again to leave.
- Cheat sheets from the package (`docs/*.png|pdf`) open from Settings → Firmware → "Cheat sheet".

## 5. Settings (Settings → Skin → Hints)

Off · While holding (default) · Always for the current layer; delay 0–400 ms; layer strip on/off; dimming of inactive
keys on/off; hint language (as in the package / another language if a translation exists).

## 6. Tests

Host: layer choice by `held` and by `uiLayer`; the most specific combo; the `allKeys` template; knob label priority;
the validator on all three `overlay.json`. Device: holding FX/GLO/SAVE/EDIT/SEQ/REC/LFO/SCL on JIANT, with screenshots.
