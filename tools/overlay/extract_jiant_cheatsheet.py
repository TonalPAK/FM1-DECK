#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build JIANT's key-overlay (fwbuild/overlays/jiant/overlay.json) from its own cheat-sheet generator.

JIANT keeps the content of its printed cheat sheets as Python literals in tools/gen_cheatsheets.py (layer key maps,
page buttons, combos, every page's knob labels). This script reads that file with `ast` -- it never imports or runs
upstream code -- and writes the overlay format described in docs/KEY-OVERLAY.md. A small hand-written
overrides.json next to the output fills what prose cannot give (e.g. "F3 rolls new routes") and is merged last.

    python -I tools/overlay/extract_jiant_cheatsheet.py upstream/jiant fwbuild/overlays/jiant/overlay.json

Re-run on every JIANT update (the firmware-packs CI does): a changed cheat sheet becomes a changed overlay.
"""
import ast
import html
import json
import re
import subprocess
import sys
from pathlib import Path

NOTES = ['F3', 'F#3', 'G3', 'G#3', 'A3', 'A#3', 'B3', 'C4', 'C#4', 'D4', 'D#4', 'E4', 'F4', 'F#4', 'G4', 'G#4', 'A4',
         'A#4', 'B4', 'C5', 'C#5', 'D5', 'D#5', 'E5', 'F5', 'F#5', 'G5']
BUTTONS = ['FX', 'SCL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC', 'OCT-', 'OCT+']
NOTE_RE = re.compile(r'\b([A-G]#?[3-5])\b')


def norm(s):
    """'OCT−' (U+2212) -> 'OCT-', collapse spaces."""
    return re.sub(r'\s+', ' ', s.replace('−', '-').replace(' ', ' ')).strip()


def strip_html(s):
    return norm(html.unescape(re.sub(r'<[^>]+>', ' ', s)))


class Src:
    """Literal evaluation of the parts of gen_cheatsheets.py we need, without executing it."""

    def __init__(self, path):
        self.text = path.read_text(encoding='utf-8')
        self.tree = ast.parse(self.text)
        self.consts = {}
        for node in ast.walk(self.tree):
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                try:
                    self.consts[node.targets[0].id] = ast.literal_eval(node.value)
                except (ValueError, SyntaxError, TypeError):
                    self.consts.setdefault(node.targets[0].id, node.value)

    def value(self, node):
        """str / HEAT[i] / literal / Name of a literal."""
        if isinstance(node, ast.Constant):
            return node.value
        if isinstance(node, ast.Name) and not isinstance(self.consts.get(node.id), ast.AST):
            return self.consts.get(node.id)
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name):
            seq = self.consts.get(node.value.id)
            idx = self.value(node.slice)
            if isinstance(seq, (list, tuple)) and isinstance(idx, int):
                return seq[idx]
        try:
            return ast.literal_eval(node)
        except (ValueError, SyntaxError, TypeError):
            return None

    def flatten(self, node, out):
        """A concatenation of strings and calls -> [('str', s) | ('call', name, args)]."""
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            self.flatten(node.left, out)
            self.flatten(node.right, out)
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
            out.append(('call', node.func.id, node.args, node.keywords))
        elif isinstance(node, ast.JoinedStr):
            out.append(('str', ''.join(v.value for v in node.values if isinstance(v, ast.Constant))))
        else:
            v = self.value(node)
            if isinstance(v, str):
                out.append(('str', v))
        return out


def key_map(whites, blacks):
    keys = {}
    for d in (whites or {}), (blacks or {}):
        for note, label in d.items():
            n = norm(note)
            if n in NOTES and label:
                keys[n] = {'label': norm(label)}
    return keys


def keys_from_prose(text):
    """'F3 CLEAR · G3 REVERSE · A3 B3 SHIFT' -> {F3: CLEAR, G3: REVERSE, A3: SHIFT, B3: SHIFT}; parentheses skipped."""
    text = re.sub(r'\([^)]*\)', ' ', text)
    parts = re.split(r'\s*(?:·|,|;|\.(?:\s|$))\s*', text)
    keys = {}
    for part in parts:
        notes = []
        rest = part
        while True:
            m = re.match(r'\s*([A-G]#?[3-5])\b', rest)
            if not m:
                break
            notes.append(m.group(1))
            rest = rest[m.end():]
        if not notes:
            m = NOTE_RE.search(part)
            if not m:
                continue
            notes, rest = [m.group(1)], part[m.end():]
        caps = [w for w in re.findall(r"[A-Z][A-Z0-9/+\-]+", rest) if len(w) >= 2]
        if caps:
            for n in notes:
                keys[n] = {'label': ' '.join(caps[:2])}
    return keys


def layer_entry(src, title, color, body_parts):
    button = norm(title.split('·')[0])
    entry = {'id': button.lower().replace('+', 'p').replace('-', 'm'), 'title': norm(title), 'color': color,
             'trigger': {'held': [button]}, 'keys': {}, 'knobs': [], 'note': ''}
    texts = []
    for p in body_parts:
        if p[0] == 'str':
            texts.append(p[1])
        elif p[1] == 'keyboard':
            entry['keys'] = key_map(*(src.value(a) for a in p[2][:2]))
        elif p[1] == 'knobs':
            entry['knobs'] = [norm(src.value(a)) if src.value(a) != '-' else '' for a in p[2]]
    note = strip_html(' '.join(texts)).replace('KNOBS', '').strip()
    entry['note'] = note
    if not entry['keys']:
        entry['keys'] = keys_from_prose(note)
    return entry


def main(repo, out):
    repo, out = Path(repo), Path(out)
    gen = repo / 'tools' / 'gen_cheatsheets.py'
    src = Src(gen)
    version = src.consts.get('VERSION')
    layers, combos, pages, panel, knob_pages = [], [], [], [], {}

    controls = next(n for n in src.tree.body if isinstance(n, ast.FunctionDef) and n.name == 'controls')
    for node in ast.walk(controls):
        # b.append(card('TITLE', COLOR, keyboard(...) + '...' + knobs(...)))
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'card' \
                and len(node.args) >= 3:
            title, color = src.value(node.args[0]), src.value(node.args[1])
            if isinstance(title, str) and '·' in title and title.split('·')[0].strip() in BUTTONS:
                parts = src.flatten(node.args[2], [])
                if any(p[0] == 'call' and p[1] == 'keyboard' for p in parts):
                    layers.append(layer_entry(src, title, color, parts))
        # small = [(title, colour, desc, (k1, k2, k3, k4)), ...]
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name == 'small' and isinstance(node.value, ast.List):
                for elt in node.value.elts:
                    t, c, d, kn = (src.value(e) for e in elt.elts)
                    parts = [('str', d), ('call', 'knobs', [ast.Constant(k) for k in kn], [])]
                    layers.append(layer_entry(src, t, c, parts))
            elif name == 'rows' and isinstance(node.value, ast.List):
                for elt in node.value.elts:
                    n, d = (src.value(e) for e in elt.elts)
                    panel.append({'control': norm(n), 'text': strip_html(d)})
            elif name == 'pb' and isinstance(node.value, ast.List):
                for elt in node.value.elts:
                    b, taps, hold = (src.value(e) for e in elt.elts)
                    pages.append({'button': norm(b), 'taps': norm(taps), 'hold': norm(hold)})
            elif name == 'combos' and isinstance(node.value, ast.List):
                # k('REC') + ' + ' + k('FX') + k('EDIT') ...  -> held REC, then any one of FX / EDIT / ...
                # k('SAVE') + ' + ' + k('OCT+') + ' / ' + k('OCT−') -> held SAVE, press OCT+ or OCT-
                # k('HOME', cls='a') + ' held'                  -> held HOME, nothing pressed
                for elt in node.value.elts:
                    parts = src.flatten(elt.elts[0], [])
                    held, press, target = [], [], None
                    target = held
                    for p in parts:
                        if p[0] == 'call' and p[1] == 'k':
                            target.extend(norm(src.value(a)) for a in p[2])
                        elif p[0] == 'str' and '+' in p[1]:
                            target = press
                    text = strip_html(src.value(elt.elts[1]) or '')
                    combos.append({'held': held, 'press': press, 'text': text})
            elif name == 'fams' and isinstance(node.value, ast.List):
                for fam in node.value.elts:
                    for row in src.value(fam.elts[2]) or []:
                        page, ks = row
                        parts = ks.split(' ')
                        knob_pages[norm(page)] = [('' if k == '-' else norm(k)) for k in parts] if len(parts) == 4 \
                            else {'text': norm(ks)}

    overrides_path = out.parent / 'overrides.json'
    overrides = json.loads(overrides_path.read_text(encoding='utf-8')) if overrides_path.exists() else {}
    for lid, ov in overrides.get('layers', {}).items():
        for layer in layers:
            if layer['id'] == lid:
                layer['keys'].update(ov.get('keys', {}))
                for k in ('allKeys', 'trigger', 'note', 'knobs'):
                    if k in ov:
                        layer[k] = ov[k]

    try:
        commit = subprocess.run(['git', '-C', str(repo), 'rev-parse', 'HEAD'], capture_output=True, text=True,
                                check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        commit = 'unknown'

    doc = {
        'schema': 1,
        'firmware': 'jiant',
        'appliesTo': {'versions': [version]},
        'lang': 'en',
        'source': {'repo': 'https://github.com/juanjiant-bit/JIANT', 'commit': commit,
                   'file': 'tools/gen_cheatsheets.py', 'extractor': 'tools/overlay/extract_jiant_cheatsheet.py'},
        'license': 'GPL-3.0-only (content from JIANT FM by juanjiant-bit)',
        'layers': layers,
        'combos': combos,
        'pages': pages,
        'panel': panel,
        'knobPages': knob_pages,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    n_keys = sum(len(layer['keys']) for layer in layers)
    print(f'jiant overlay {version}: {len(layers)} layers, {n_keys} key labels, {len(combos)} combos, '
          f'{len(knob_pages)} pages -> {out}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
