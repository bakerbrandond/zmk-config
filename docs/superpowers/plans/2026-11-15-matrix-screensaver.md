# Matrix Rain Screensaver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After 2 minutes idle, the Prospector shows Nord-green half-width katakana rain; any key restores the status screen.

**Architecture:**
- A pure-C rain model (host-tested) holds the grid and reports changes per cell.
- A renderer draws the grid from one custom-draw LVGL object and invalidates one span per column.
- A controller drives it from ZMK activity events and syncs screens on the display queue. It cooperates with Zappy Birds through `matrix_mode_hide_now()`.

**Tech Stack:** C, ZMK v0.3, LVGL 8.3, `lv_font_conv` (npx), host `gcc`, `scripts/build-local.sh`.

**Spec:** `docs/superpowers/specs/2026-11-15-matrix-screensaver-design.md`

## Global Constraints

- **Branches:** fork `~/repos/zmk/prospector-zmk-module` on `nord`; config `~/repos/zmk/zmk-config` on `jiggler`. Never push without the user's go-ahead.
- **Threads:** LVGL only on the display queue. Event listeners only touch atomics and submit work.
- **Colours:**
  - head: NORD6
  - trail: NORD14 mixed toward NORD0
  - background: NORD0
- **Glyphs:** U+FF66–U+FF9D (56). Grid: 20 cols × 14 rows of 12 × 20 px cells.
- **Timing:**
  - Timeout: `CONFIG_PROSPECTOR_MATRIX_TIMEOUT_S` = 120, which must be ≥ `CONFIG_ZMK_IDLE_TIMEOUT`/1000.
  - Step: about 66 ms (`MATRIX_STEP_MS 66`) on a 10 ms poll timer.
- **Invalidation:** at most one area per column per frame.
- **Commits:** messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Wake via the game key:** pressing the Function-layer game key while the rain is up shows the game. Exiting the game shows the status screen, never the rain. Owner: Task 3 (`show_game` calls `matrix_mode_hide_now()`; sync hide checks the active screen) and the Task 4 device check.
2. **Idle during the game:** no rain while playing. After the game's own 60 s idle exit, the rain still arrives within about 10 s plus the remaining time. Owner: Task 3 (reschedule every 10 s while the game is active).
3. **Waking key still types:** Owner: Task 3 (the listener never consumes events; activity is separate from the keymap) and the Task 4 device check.
4. **Model bounds over hours:** no overflow in head, level, wait or glyph. Owner: Task 2 (`test_long_run_bounds`).
5. **Frame cost:** must not starve the status widgets after wake. Owner: Task 4 timing check (step interval from the debug log).

---

### Task 1: Katakana font

**Files (fork):**
- Create `boards/shields/prospector_adapter/src/fonts/NotoSansMonoCJKJP_Katakana_18.c` and `NotoSansCJK-LICENSE.txt` (SIL OFL 1.1 text).
- Modify `boards/shields/prospector_adapter/include/fonts.h` (`LV_FONT_DECLARE(NotoSansMonoCJKJP_Katakana_18)`).

**Produces:** `NotoSansMonoCJKJP_Katakana_18`, 4 bpp, covering 0xFF66–0xFF9D. Its line height must be ≤ 20 px and its advance ≤ 12 px. If it's larger, pick a smaller size and keep the same symbol naming pattern with the size suffix.

- [ ] **Step 1:** Generate it into a scratch dir:
  `npx lv_font_conv@1.5.2 --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc -r 0xFF66-0xFF9D --size 18 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o <scratch>/font.c`
  If `.ttc` input fails, extract face index 5 (Noto Sans Mono CJK JP) with fontTools in a throwaway venv in the scratchpad, then convert the `.otf`.
- [ ] **Step 2:** Check that `.line_height` ≤ 20 and that the glyph advance (`adv_w`/16) ≤ 12. Then copy the file into the fonts dir, renaming the symbol.
- [ ] **Step 3:** Run `scripts/build-local.sh dongle`. Expected: exit 0. The font is unused, so it gets garbage-collected; this only checks that it compiles.
- [ ] **Step 4:** Commit (fork): `Prospector: half-width katakana font (Noto Sans Mono CJK JP, OFL)`.

### Task 2: Rain model (host-tested)

**Files (fork):**
- Create `src/matrix/matrix_core.h`, `src/matrix/matrix_core.c`.
- Create `tests/matrix/test_core.c`, `tests/matrix/Makefile`.
- Add `tests/matrix/test_core` to `.gitignore`.

**Produces (`matrix_core.h`, stdint/stdbool only):**

```c
#define MATRIX_COLS 20
#define MATRIX_ROWS 14
#define MATRIX_GLYPHS 56
#define MATRIX_LEVEL_MAX 15
struct matrix_col { int16_t head; uint8_t period, phase, decay; uint8_t wait; };
struct matrix_state {
    uint8_t glyph[MATRIX_ROWS][MATRIX_COLS];
    uint8_t level[MATRIX_ROWS][MATRIX_COLS]; /* 0 = empty */
    struct matrix_col col[MATRIX_COLS];
    uint16_t dirty[MATRIX_COLS];             /* bit r = row r changed in last step */
    uint32_t rng;
};
void matrix_init(struct matrix_state *m, uint32_t seed);
void matrix_step(struct matrix_state *m);
bool matrix_is_head(const struct matrix_state *m, int r, int c);
```

**Rules:**
- **Columns:** each column has a head row. While running it advances one row every `period` steps (1–3). The cell it lands on gets a random glyph and `level = MATRIX_LEVEL_MAX`.
- **Fading:** every lit non-head cell loses `decay` levels per step, clamped at 0. `decay` is `ceil(15 / trail)`, with trail 6–12, chosen at spawn.
- **Respawn:** once `head ≥ MATRIX_ROWS`, the column waits a random 0–30 steps, then restarts at head = 0 with new period and decay.
- **Flicker:** each step, each column with probability 1/8 changes the glyph of one random lit cell.
- **Starting state:** after `matrix_init` all levels are 0 and the heads are staggered. Each column starts waiting a random 0–40 steps, so the first frame isn't a wall.
- **Dirty bits:** after each step, `dirty[c]` bit `r` is set exactly when that cell's glyph, level, or head status changed in that step.

- [ ] **Step 1:** Write failing tests:
  - `test_init_empty`: all levels 0.
  - `test_head_advances_by_period`: force col 0 to `head=0, period=2, wait=0`. After 2 steps head is 1; after 4 steps it is 2.
  - `test_trail_dims`: one lit cell at `level=15` with `decay=3`, not the head. After 1 step it is 12; after 5 steps it is 0.
  - `test_dirty_exact`: random seed, 2000 steps. Snapshot glyph, level and head status before each step and compare after; the dirty bits must match the differences exactly.
  - `test_columns_respawn`: over 3000 steps every column's head returns to 0 at least twice.
  - `test_long_run_bounds`: 200000 steps. Glyph < 56, level ≤ 15, head in `[0, MATRIX_ROWS + 1]`, period 1–3.
  - `test_deterministic`: the same seed gives identical states after 500 steps.
  Makefile: same pattern as `tests/zappy/Makefile`.
- [ ] **Step 2:** Run `make -C tests/matrix test`. Expected: compile errors.
- [ ] **Step 3:** Implement `matrix_core.c` with xorshift32.
- [ ] **Step 4:** Run `make -C tests/matrix test`. Expected: `ok 7 tests`.
- [ ] **Step 5:** Commit (fork): `Prospector: matrix rain model with host tests`.

### Task 3: Renderer, controller, Kconfig, game cooperation

**Files (fork):**
- Create `src/matrix/matrix_screen.{h,c}` and `src/matrix/matrix_mode.{h,c}`.
- Modify `Kconfig`, `CMakeLists.txt`, `src/zappy/zappy_mode.c`.

**Files (config):** modify `config/corne_dongle.conf`.

**Interfaces:**
- `lv_obj_t *matrix_screen_create(void);`
- `void matrix_screen_render(const struct matrix_state *m);`: invalidates one span per column from `dirty`, covering the rows from the lowest to the highest set bit.
- `void matrix_mode_hide_now(void);`: display queue only. The weak no-op in `zappy_mode.c` is overridden when the feature is on.

**Rules:**
- **Draw:** in `LV_EVENT_DRAW_MAIN`, draw only the cells intersecting `draw_ctx->clip_area`. Each lit cell gets one `lv_draw_label` of its glyph's 3-byte UTF-8, from a precomputed table.
- **Colour:** the head cell is NORD6. Other cells are `lv_color_mix(NORD14, NORD0, level*255/15)` (mix ratio toward NORD14).
- **Controller:** as in spec § Controller:
  - activity listener, delayable work, 10 s re-check while the game is active
  - `want_rain` atomic and sync work
  - 10 ms poll `lv_timer` stepping on a fixed 66 ms timestep. Reuse the `zappy_steps_due` idea locally, capped at 2.
- **Kconfig:**
  - `PROSPECTOR_MATRIX_SCREENSAVER` (bool, default n, depends on `SHIELD_PROSPECTOR_ADAPTER`)
  - `PROSPECTOR_MATRIX_TIMEOUT_S` (int, default 120, range 30 3600)
  - `BUILD_ASSERT(CONFIG_PROSPECTOR_MATRIX_TIMEOUT_S * 1000 >= CONFIG_ZMK_IDLE_TIMEOUT)`
- **CMake:** an `if(CONFIG_PROSPECTOR_MATRIX_SCREENSAVER)` block with the shield include dir and the three matrix sources.
- **Game:** `show_game()` calls `matrix_mode_hide_now()` first.

- [ ] **Step 1:** Implement it, including `CONFIG_PROSPECTOR_MATRIX_SCREENSAVER=y` in `config/corne_dongle.conf`.
- [ ] **Step 2:** Build. Run `scripts/build-local.sh all && scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_MATRIX_SCREENSAVER=n && scripts/build-local.sh dongle`. Expected: all exit 0, no warnings from `src/matrix`.
- [ ] **Step 3:** Run both host suites: `make -s -C tests/zappy test && make -s -C tests/matrix test`. Expected: `ok 26 tests`, then `ok 7 tests`.
- [ ] **Step 4:** Commit both repos:
  - fork: `Prospector: matrix rain screensaver`
  - config: `Enable the matrix rain screensaver`

### Task 4: Device check (with the user)

- [ ] **Step 1:** Make a debug build with USB logging. Temporarily set `CONFIG_PROSPECTOR_MATRIX_TIMEOUT_S=30` so it starts fast, and print every 100th step's uptime plus heap info. Flash it and capture the log. Pass:
  - 100 steps take about 6.6 s (± 15%)
  - the heap has more than 8 KB free
  - no errors
- [ ] **Step 2:** User checklist on the debug build:
  - the rain looks good
  - a key wakes it, and that key types
  - the game key from the rain opens the game, and exiting the game shows the status screen
  - no rain during the game
- [ ] **Step 3:** Tune the look if the user asks. Re-run the host tests, then commit.
- [ ] **Step 4:** Flash the normal build, which uses 120 s.
