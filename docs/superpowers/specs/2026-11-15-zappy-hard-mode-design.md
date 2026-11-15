# Zappy Birds — hard mode (short design)

Date: 2026-11-15
Status: draft, awaiting approval
Builds on: `2026-11-15-zappy-birds-design.md`

## Intent

A harder variant for showing off: the screen shows one letter, only that
key flaps, and each correct press picks a new random letter. It's a reflex
test layered on the timing game.

## Decided (by the user)

- A wrong key does nothing: no flap, no penalty.
- Chosen on the title screen: **H starts hard mode**, any other key starts
  normal mode. The title reads `any key: play` / `H: hard`.
- Letter labels are read from the keymap at runtime, not from a fixed list.

## My calls (correct if wrong)

- **Key pool:** letters only. These are the positions whose Default-layer
  binding resolves to a keyboard-page usage A–Z, either `&kp X` or a
  hold-tap whose tap is X (your `&hrm MOD X`). Built once when the game
  screen is first created. With your layout that's 26 keys. The H position
  is found the same way.
- **Target:** a big letter near the top in hard mode. The score moves to the
  top-right corner. The next target never repeats the current one.
- **Physics:** hard mode has gentler gravity and a wider gap (starting
  values, tuned on device).
- **Scores:** a separate hard best, saved as `zappy/hh`. The title shows
  both bests.
- **After a crash:** a restart stays in the same mode. To switch modes, exit
  and re-enter.

## Approaches

- **Read labels from the keymap at runtime (chosen):** follows layout
  changes automatically; about 40 lines to decode bindings.
- Fixed 42-char label string in config: simpler, but it silently goes
  wrong when a key moves. Rejected.

## Shape of the change

- `&zflap` passes `event.position` through: `zappy_mode_flap(position)`.
- Core (`zappy_core`, host-tested): `zappy_init` gains a mode; `struct
  zappy_game` gains `mode`, `target` (position), the pool, and per-mode
  physics. `zappy_press` takes the position and ignores non-target
  positions in hard PLAYING. On TITLE, H's position starts hard mode and
  anything else starts normal. A new target is picked after each accepted
  flap, never the same as the last.
  - Pure helper `zappy_usage_letter(uint32_t usage) -> char`: maps a ZMK
    usage to 'A'–'Z', else 0.
- Pool builder (firmware side, `zappy_mode.c`): walks layer
  `zmk_keymap_layer_default()` with `zmk_keymap_get_layer_binding_at_idx`.
  For `key_press` it uses `param1`; for any other behavior it uses
  `param2`, so hold-taps contribute their tap key. Both go through
  `zappy_usage_letter`.
- Renderer: target letter label, corner score in hard mode, two-line title
  prompt, both bests.
- Store: second key `zappy/hh`.

## Verification

- Host tests: usage→letter mapping, wrong key ignored, correct key flaps and
  changes the target (never a repeat), H on the title starts hard and other
  keys start normal, separate hard best.
- Device: hard mode playable and fair after tuning, letters match the
  keycaps, both bests survive a power cycle.
