# Zappy Birds — design

Date: 2026-11-15
Status: reviewed, awaiting approval

## Intent

A party trick to show off the keyboard. A hidden key on the Function layer
turns the Prospector dongle's screen into **Zappy Birds**, a one-button
tap-to-fly game: you hand someone the keyboard, they mash any key to keep a
little electric bird aloft through gaps, and try to beat the saved high score.
Pressing the exit combo returns to the normal status screen and layers.

"Zappy" comes from the Z in ZMK. The mechanic (tap to flap, fly through gaps)
is a well-known genre; the name, art, and look are our own. Do **not** copy
Flappy Bird's bird, green pipes, sky/ground art, fonts, sounds, or layout.

## Goals

- Polished enough to show off: title screen, smooth motion, a satisfying crash,
  score counter, and a high score that survives power cycles.
- Any key plays. Nothing typed reaches the computer while in game mode.
- Clean exit back to the exact status screen and layer state as before.
- Nord look, consistent with the rest of the Prospector theme (`theme.h`).

## Non-goals

- Sound, multiple game modes, difficulty settings, leaderboards with names.
- Running the game on the nice!view halves (dongle only).
- Packaging as a standalone module (possible later; see Approaches).

## Anti-goals (failure even if it "works")

- Keystrokes leaking to the host during play.
- The keyboard getting stuck in game mode (no way out, or unclear how).
- Breaking or slowing normal typing, BLE, the jiggler, or the status widgets
  when the game is not running.
- Running the LVGL heap out of memory (crash/garbled screen).
- Looking like a Flappy Bird copy.

## Constraints (from the current code)

- Hardware: Seeed XIAO nRF52840 in a Prospector, ST7789V 240×280 color LCD,
  LVGL (the version bundled with ZMK v0.3, LVGL 8.x API: `lv_scr_load`,
  `lv_timer_create`, `lv_obj_*`).
- The dongle is the split central, so all key events and keymap behaviors run
  on it.
- Screen code lives in the user's fork `bakerbrandond/prospector-zmk-module`
  branch `nord` (local checkout `~/repos/zmk/prospector-zmk-module`):
  `boards/shields/prospector_adapter/src/custom_status_screen.c` builds the
  status screen via `zmk_display_status_screen()`; colors in
  `include/theme.h`.
- All LVGL calls must happen on the display work queue
  (`zmk_display_work_q()` from `<zmk/display.h>`). Behavior callbacks run on a
  different thread, so they must submit work to that queue, not touch LVGL.
- LVGL heap is `CONFIG_LV_Z_MEM_POOL_SIZE=16384`; current peak ~11 KB with the
  status screen. The status screen stays allocated while the game runs, so the
  game must stay small (target < 3 KB of LVGL objects): rectangles/simple
  objects and at most a tiny bitmap sprite, no full-screen images. Raising the
  pool is allowed if measured need requires it.
- Keymap: `config/corne.keymap`, layers DEF LOW RAS FUN MOS (0–4); FUN is
  reached via LOW+RAS (tri-layer).
- ZMK has a local build workspace history; firmware is normally built by
  GitHub Actions from `build.yaml`.
- Target ZMK **v0.3** (`west.yml` default revision). The local checkout at
  `~/repos/zmk/zmk` is an older `main` (Aug 2023) — verify behavior-driver
  macros, layer id vs index, and binding lookup against v0.3, not that tree.
- `config/corne.keymap` is shared by the dongle **and** both halves, so the
  `&zappy`/`&zflap` DT nodes, the ZAP layer, and the combo must compile in
  every build (halves, and dongle with the feature off). Devicetree can't be
  Kconfig-gated; only the driver C code is gated. Bindings/drivers must
  therefore be harmless no-ops when the game code isn't built.
- The fork's `zephyr/module.yml` has no `dts_root`; add `dts_root: .` so new
  `dts/bindings/zmk,behavior-zappy-*.yaml` files are found.
- The status screen is extended from outside the fork:
  `zmk-config/src/prospector_indicators.c` hooks in via a linker `--wrap` of
  `zmk_widget_battery_bar_init` and runs its own 500 ms `lv_timer`. It keeps
  running while the game screen is loaded (harmless, its objects are just
  off-screen) and its labels count toward the heap budget.
- No display blanking concern: ZMK blank-on-idle defaults on only for
  SSD1306, not this ST7789V.

## Approaches considered

- **A. In the Prospector fork, with a dedicated game layer (chosen).** New
  `&zappy` behavior toggles game mode and a ZAP keymap layer where every key is
  a flap that emits nothing to the host. Least plumbing; "hit any key" works.
- B. Standalone `zmk-zappy-birds` module. Cleaner to share, but needs hooks in
  the fork to hand over the screen — extra work for no benefit now. Can be
  extracted from A later.
- C. No layer; game listens to all key presses globally. Either keys type into
  the host while playing, or a global swallow hack is needed. Rejected.

The user picked A.

## Design

### Pieces

1. **Behaviors** (in the fork, e.g. `src/behaviors/behavior_zappy.c`, with DT
   bindings `zmk,behavior-zappy-toggle` and `zmk,behavior-zappy-flap`):
   - `&zappy` — toggle game mode. On enter: activate layer ZAP and load the
     game screen. On exit: deactivate ZAP, reload the status screen.
   - `&zflap` — flap on key press; does nothing on release; sends no HID
     report.
   Both just set flags / submit work to `zmk_display_work_q()`; all LVGL work
   happens there.
   The ZAP layer index is passed to `&zappy` as a binding parameter
   (`&zappy ZAP`) so the module doesn't hardcode keymap layout.

2. **Game screen** (in the fork, e.g. `src/zappy/zappy_game.c`): its own
   `lv_obj_t` screen created once lazily, loaded with `lv_scr_load()`. Status
   screen object is kept and re-loaded on exit (not destroyed), so widgets keep
   working and need no re-init.

3. **Game loop:** an `lv_timer` at ~30 FPS (33 ms) created on enter, deleted
   (or paused) on exit, so zero cost when not playing. Fixed-step physics in
   integer/fixed-point: gravity, flap sets upward velocity, gaps scroll left at
   constant speed, axis-aligned box collision, score +1 per gap passed.
   Optional gentle speed-up as score rises (builder's call on tuning).

4. **High score:** persisted with Zephyr settings (NVS), key e.g.
   `zappy/hi`, saved only when beaten (avoid flash wear). Loaded at boot.

5. **Kconfig:** `CONFIG_PROSPECTOR_ZAPPY_BIRDS` (default n) gates all of it;
   enabled in `config/corne_dongle.conf`.

### States

- **Title:** "ZAPPY BIRDS" title, bird bobbing, "press any key", high score.
  First flap starts a run.
- **Playing:** bird, scrolling gaps, score at top.
- **Crash:** brief flash (e.g. screen tint in Nord red/yellow), then a game
  over card with score, high score, "NEW BEST" if beaten. Ignore input for
  ~600 ms so mashing doesn't instantly restart; then any key → new run.
- **Exit:** the exit combo (below) from any state.
- **Idle safety:** if no key is pressed for 60 s in any state, auto-exit to the
  status screen and normal layers, so the keyboard can never be left stuck in
  game mode.

### Look (original, Nord)

- Background NORD0; gap "walls" as solid vertical bars in NORD10/NORD9
  (frost blues) with a NORD8 cap edge — think charged pylons, not pipes.
- Bird: small round body (NORD13 yellow) with a lightning-bolt wing/crest
  (NORD12), tiny eye; built from a few LVGL objects or a ≤ 20×20 indexed
  bitmap. Flap = brief wing/bolt change.
- Optional small "spark" particles on flap and crash (few objects, reused).
- Text uses fonts already in the fork (`fonts.h`).

Rough layout (240×280):

```
┌────────────────────────┐
│          12            │  score
│   ▐▌           ▐▌      │
│   ▐▌           ▐▌      │
│                ▐▌      │
│      ⚡●              │  bird
│   ▐▌                   │
│   ▐▌           ▐▌      │
│   ▐▌           ▐▌      │
└────────────────────────┘
```

### Keymap (in `config/corne.keymap`)

- `#define ZAP 5`, new `zappy_layer` (display-name "Zappy") where every
  position is `&zflap`.
- Enter: `&zappy ZAP` on the FUN layer, in the currently unused slot right of
  `&out OUT_TOG` (row 2, right half).
- Exit: a combo active only on ZAP — Q + P positions pressed together
  (both outer top-row letter keys) → `&zappy ZAP`, easy to explain ("press
  the two top corners"). *(Builder's call, to stop two-handed mashers exiting
  by accident:)* set `require-prior-idle-ms` (~300 ms) so the combo only fires
  after a pause in pressing, and a short `timeout-ms` (~40 ms) so flaps on
  Q/P aren't noticeably delayed.
- Holding LOW/RAS on entry: entering activates ZAP on top, so FUN keys aren't
  reachable while playing; releasing them is harmless.

### Hidden from the status screen (user decision)

The layer roller (`src/widgets/layer_roller.c`, infinite mode) lists every
layer, so ZAP would show faded above "Default" and give the trick away. The
roller must skip the ZAP layer (e.g. ignore layers whose index is the
configured ZAP layer, or layers with no display-name — builder's call). The
normal status screen must look exactly as it does today.

### Branching (user decision)

- Fork: new branch `zappy` off `nord` in `bakerbrandond/prospector-zmk-module`;
  `nord` stays untouched until the game is proven on device.
- Config: work on a branch of `zmk-config`, with `config/west.yml` pointing
  the fork at `revision: zappy` on that branch only.
- Pushing either branch to GitHub (needed for the Actions build) is done only
  when the user says so.

## Left to the builder

- Exact physics constants, gap size/spacing, speed curve — tune for "fun in a
  30-second demo".
- Bird art details within the brief above.
- Whether to use a tiny bitmap or composed objects for the bird (measure heap).
- Whether WPM/anti-idle events need any special handling while in game mode
  (expected: none, since `&zflap` emits no HID reports; confirm the jiggler
  still works and doesn't interfere).

## Verification

- Firmware builds for the dongle with the feature on and off.
- On device: enter/exit works; nothing types into a text editor while playing;
  status screen widgets update correctly after exit; high score survives a
  power cycle; 60 s idle auto-exit; normal typing unaffected.
