# Matrix rain screensaver — design

Date: 2026-11-15
Status: approved (user: "build it"); revised after builder check

## Intent

Eye candy for the Prospector dongle. After 2 minutes with no key presses,
the status screen gives way to falling columns of half-width katakana in
Nord colours. The first key press brings the status screen back. It is an
original "digital rain" in the Nord theme, not a copy of cmatrix.

## Decided (by the user)

- Characters: half-width katakana.
- Starts after 2 minutes with no key presses.
- Colours: Nord green. Near-white head (NORD6), soft-green trail
  (NORD14 `#A3BE8C`) fading into the background (NORD0).
- Rendering approach B: one full-screen object with a custom draw routine
  over a character grid, invalidating only changed cells.

## Goals

- Smooth enough to enjoy: target ~15 fps, the display's practical ceiling
  for a mostly changing screen.
- Instant, predictable wake: any key press restores the status screen.
- Zero cost when not showing: no timer running, no per-frame work.

## Non-goals

- Halves' nice!view screens.
- Configurable colours or density at runtime.
- Full-width katakana or kanji.

## Anti-goals

- Swallowing or delaying key presses. The waking key still types, and its
  binding runs normally.
- Showing during Zappy Birds, or fighting the game for the screen.
- Running the LVGL heap out of memory, or stalling the display thread so
  the status widgets lag after wake.

## Constraints (from the code)

- **Code lives in the Prospector fork.**
  `bakerbrandond/prospector-zmk-module`, branch `nord`, local checkout
  `~/repos/zmk/prospector-zmk-module`. Theme and font declarations in
  `boards/shields/prospector_adapter/include/` (`theme.h`, `fonts.h`); fonts in
  `boards/shields/prospector_adapter/src/fonts/`.
- **Thread rules match Zappy Birds.** LVGL only on the display work queue
  (`zmk_display_work_q()`); event listeners run elsewhere and only touch
  atomics and submit work.
- **Key presses** reach the dongle as `zmk_position_state_changed` events,
  from both halves, on the central. A listener sees every press, whatever
  the layer.
- **Display timing.**
  - LVGL is serviced every 10 ms (`CONFIG_ZMK_DISPLAY_TICK_PERIOD_MS`).
  - A full-screen flush costs about 35 ms (SPI).
  - Display blanking is off on the dongle, so nothing else hides the
    screen.
- **Memory budget.**
  - LVGL heap: 24 KB, about 14 KB free with the game screen allocated.
  - Static RAM: about 33 KB free.
  - Flash: about 311 KB free.
  - No full-screen canvas: 134 KB.
- **Screen ownership.** The game swaps screens with `lv_scr_load` and
  restores the saved status screen; the screensaver does the same. Both
  must agree on who owns the screen.

## Approaches considered

- A. One recoloured label per column. Simple, but re-parses text every
  frame and redraws whole columns.
- **B. One object with a custom draw over a grid, partial invalidation
  (chosen).** Best frame rate; no buffer.
- C. Full-screen canvas. Needs 134 KB RAM. Ruled out.

## Design

### Font

- A new LVGL font, `NotoSansMonoCJKJP_Katakana_18` or similar, generated
  with LVGL's font converter (`npx lv_font_conv`, 4 bpp, no compression).
- Source: the installed Noto Sans Mono CJK JP Regular. That file is a
  `.ttc` collection, so one face must be extracted to `.otf` first; the
  builder picks the tool (e.g. fontTools via a throwaway venv, or a converter
  that accepts collections).
- Glyphs:
  - half-width katakana U+FF66–U+FF9D (56 glyphs)
  - digits 0–9 for texture (optional, builder's call)
- Committed as a `.c` file next to the other fonts, with the font's SIL OFL
  licence text, following the existing `CaskaydiaCove-LICENSE.txt`
  precedent.
- Exact pixel size is the builder's call: target a 12 × 20 px cell, giving
  a 20 × 14 grid on 240 × 280.

### Rain model (pure C, host-tested, `src/matrix/matrix_core.{h,c}`)

- **Grid:** `MATRIX_COLS × MATRIX_ROWS` cells, each holding a glyph index
  and a brightness level 0..N (0 = empty, N = head).
- **Per column:** a head row, a speed (1 step every 1–3 ticks), and a
  trail length (6–12). New drops start at random delays after one leaves
  the bottom.
- **Each tick:**
  - heads advance
  - cells behind them dim one level
  - a few random cells change glyph (flicker)
  - the step reports which cells changed, so the renderer invalidates only
    those
- **Determinism:** seeded xorshift, like the game core, so tests are
  deterministic.

### Renderer (`src/matrix/matrix_screen.{h,c}`)

- Its own `lv_obj_t` screen, created lazily once, holding one full-size
  child object with an `LV_EVENT_DRAW_MAIN` handler. The handler draws only
  cells intersecting the clip area, with `lv_draw_label`, one glyph each.
  Colour comes from a small palette:
  - head: NORD6
  - level N-1: NORD14
  - then NORD14 mixed toward NORD0 in about 3 steps
- Background: NORD0.
- Changed cells are invalidated as small areas. LVGL merges overlapping
  invalidations.

### Controller (`src/matrix/matrix_mode.c`)

- **Idle detection rides on ZMK's activity tracking.** Its listener sits
  before the keymap and sees every position/sensor event; the jiggler's mouse
  moves don't count. A listener on `zmk_activity_state_changed` does two
  things:
  - On `ZMK_ACTIVITY_IDLE`, raised after `CONFIG_ZMK_IDLE_TIMEOUT` (30 s), it
    schedules a `k_work_delayable` for the remaining
    `MATRIX_TIMEOUT_S - ZMK_IDLE_TIMEOUT/1000` seconds (90 s). A build-time
    check requires the timeout to be at least the ZMK idle timeout.
  - On `ZMK_ACTIVITY_ACTIVE` it cancels that work and wakes. The event is
    raised synchronously while the waking key press is being dispatched,
    before the keymap runs, so the wake is queued before anything the key's
    binding queues (e.g. the game's sync work).
- **Delayed work fires** (system queue). If Zappy Birds is active, it
  reschedules itself in 10 s and checks again. Otherwise it sets
  `want_rain` and submits a sync work item to the display queue.
- **Sync** (display queue) reconciles the screen with `want_rain`, the same
  way the game's sync works, so ordering can't leave the wrong screen up:
  - **Show** only when `want_rain` is set, the game is not active, and the
    rain is not already up. It saves `lv_scr_act()`, loads the rain screen,
    and starts a 10 ms `lv_timer` that steps the model on a fixed timestep
    (about 66 ms per step, ~15 fps).
  - **Hide** (when `want_rain` is clear): delete the timer, then reload the
    saved screen only if the rain screen is still the active one. If
    something else, i.e. the game, has taken over, it leaves that alone.
- **The game cooperates through `matrix_mode_hide_now()`** (display queue).
  It clears `want_rain`, stops the timer, and restores the saved screen if the
  rain is showing. The game's `show_game` calls it before saving
  `lv_scr_act()`, so the game never saves the rain screen as "status".
- **Kconfig.**
  - `CONFIG_PROSPECTOR_MATRIX_SCREENSAVER` (bool, default n,
    `depends on SHIELD_PROSPECTOR_ADAPTER`)
  - `CONFIG_PROSPECTOR_MATRIX_TIMEOUT_S` (int, default 120)
  - enabled in `config/corne_dongle.conf`
- **Includes.** Sources need
  `zephyr_library_include_directories(boards/shields/prospector_adapter/include)`
  for `theme.h` and `fonts.h`, like the zappy block.

### Invalidation (correction from the builder check)

- LVGL 8.3 keeps at most 32 dirty areas, then falls back to a full-screen
  redraw, and only merges overlapping areas.
- So the renderer invalidates **one vertical span per column**, covering
  that column's changed rows: at most 20 areas per frame. The model still
  reports changes per cell, and tests check that reporting exactly.
- An on-device timing check comes early, before look tuning.

## Left to the builder

- Exact font size and cell size within the 12 × 20 target, drop speeds,
  trail lengths, flicker rate, and fade steps — tuned on device for looks.
- The tool used to extract the font from the `.ttc`.
- Whether digits are mixed in.

## Verification

- **Host tests (rain model):**
  - heads advance at their speed
  - trails dim
  - changed-cell reporting is exact: every changed cell reported, and no
    unchanged cell reported
  - columns respawn
  - deterministic with a seed
  - everything stays in bounds over a long run
- **Build:** dongle with the feature on and off; both halves.
- **Device:**
  - starts after 2 minutes idle, not before
  - smooth-looking
  - any key wakes it and that key still types
  - no screensaver during the game
  - entering the game from the screensaver works, and exiting the game
    returns to the status screen, not the rain
  - LVGL heap still has headroom (debug log)
