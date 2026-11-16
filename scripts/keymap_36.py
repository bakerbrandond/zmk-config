"""Fit keymap-drawer's parsed Corne keymap to the 36-key board.

config/corne.keymap uses the 42-key layout; positions 0, 11, 12, 23, 24 and
35 (the outer columns) have no physical key. This drops them from every layer,
renumbers combo positions, leaves out the Zappy game layer (every key is
"flap") with the combos that only apply there, and draws on QMK's crkbd
LAYOUT_split_3x5_3.

usage: python keymap_36.py parsed.yaml out.yaml
"""
import sys

import yaml

OUTER = {0, 11, 12, 23, 24, 35}
HIDDEN_LAYERS = {"Zappy", "zappy_layer"}


def flat(keys):
    out = []
    for k in keys:
        out.extend(flat(k) if isinstance(k, list) else [k])
    return out


def renumber(p):
    return p - sum(1 for o in OUTER if o < p)


src, dst = sys.argv[1], sys.argv[2]
km = yaml.safe_load(open(src))

layers = {}
for name, keys in km["layers"].items():
    if name in HIDDEN_LAYERS:
        continue
    keys = flat(keys)
    if len(keys) != 42:
        sys.exit(f"layer {name}: expected 42 keys, got {len(keys)}")
    layers[name] = [k for i, k in enumerate(keys) if i not in OUTER]
km["layers"] = layers

combos = []
for c in km.get("combos", []) or []:
    pos_key = "p" if "p" in c else "key_positions"
    on = c.get("l") or c.get("layers")
    if on and all(l in HIDDEN_LAYERS for l in on):
        continue
    if any(p in OUTER for p in c[pos_key]):
        continue
    c[pos_key] = [renumber(p) for p in c[pos_key]]
    combos.append(c)
km["combos"] = combos

km["layout"] = {"qmk_keyboard": "crkbd/rev1", "layout_name": "LAYOUT_split_3x5_3"}
yaml.safe_dump(km, open(dst, "w"), sort_keys=False, allow_unicode=True)
print(f"{len(layers)} layers, {len(combos)} combos -> {dst}")
