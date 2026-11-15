# Zappy Birds Hard Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A hard mode chosen with H on the title screen: one displayed letter is the only key that flaps, and each correct press picks a new random letter.

**Architecture:** Rules stay in the host-tested pure-C core (`zappy_core`): mode, target, letter pool, per-mode physics, and per-mode bests. The firmware side builds the letter pool from the Default layer at runtime, passes key positions from `&zflap` through a lock-free bitmask to the game tick, and the renderer and store learn about the second mode.

**Tech Stack:** C, ZMK v0.3, LVGL 8.3, Zephyr settings; host `gcc` tests; `scripts/build-local.sh`.

**Spec:** `docs/superpowers/specs/2026-11-15-zappy-hard-mode-design.md` (builds on `2026-11-15-zappy-birds-design.md`).

## Global Constraints

- All of the original plan's constraints still hold (`docs/superpowers/plans/2026-11-15-zappy-birds.md` § Global Constraints): v0.3 APIs, LVGL only on the display queue, no push without the user's go-ahead, branches `zappy` in both repos.
- A wrong key in hard mode does nothing: no flap, no penalty.
- Title copy: `ZAPPY\nBIRDS`, then `any key: play`, then `H: hard`, then `BEST n  HARD m`.
- Letter pool: only positions whose Default-layer binding resolves to keyboard-page usages A–Z (page 0x07, ids 0x04–0x1D). Mod bits in the top byte are ignored.
- Hard best settings key: `zappy/hh`; normal stays `zappy/hi`.
- The next target never equals the current one.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Pool too small:** if the Default layer has fewer than 2 letters (or no H), pressing H must not start an unplayable hard mode. Expected: H starts normal mode. Owner: Task 1 (`test_hard_needs_two_letters`).
2. **Several keys pressed in one 10 ms pass:** if the target key is among them, it must count. Owner: Task 2 (controller tries each pressed position until `zappy_press` accepts one).
3. **Restart after a crash in hard mode:** it stays in hard mode with a fresh target, and the lockout still applies. Owner: Task 1 (`test_hard_restart_stays_hard`).
4. **Labels match the keycaps:** home-row mod keys show their tap letter, not the modifier. Owner: Task 2 (pool builder uses `param2` for non-`key_press` behaviors), checked on device in Task 3.
5. **Existing normal-mode behavior unchanged:** Owner: Task 1 (all 16 existing tests still pass after the signature changes).

---

## File Structure (fork `~/repos/zmk/prospector-zmk-module`)

- Modify `src/zappy/zappy_core.h/.c`: mode, pool, target, per-mode physics and bests, usage→letter.
- Modify `tests/zappy/test_core.c`: new tests; existing calls updated to the new signatures.
- Modify `src/zappy/zappy_mode.h/.c`: flap position bitmask, pool builder, per-mode bests and save.
- Modify `src/behaviors/behavior_zappy.c`: pass `event.position`.
- Modify `src/zappy/zappy_screen.c`: target letter, corner score, new title text.
- Modify `src/zappy/zappy_store.c`: `zappy/hh` key.

---

### Task 1: Core hard-mode rules (host-tested)

**Files:** `src/zappy/zappy_core.h`, `src/zappy/zappy_core.c`, `tests/zappy/test_core.c`

**Interfaces — produces (in `zappy_core.h`):**

```c
#define ZAPPY_MAX_POOL 42
#define ZAPPY_NO_POS 0xFF

enum zappy_mode { ZAPPY_NORMAL, ZAPPY_HARD };

struct zappy_pool {
    uint8_t n;
    uint8_t pos[ZAPPY_MAX_POOL];
    char letter[ZAPPY_MAX_POOL];
    uint8_t h_pos; /* position of 'H', or ZAPPY_NO_POS */
};

/* 'A'..'Z' for keyboard-page usages A..Z (mod bits ignored), else 0 */
char zappy_usage_letter(uint32_t usage);
/* adds position if usage is a letter; records h_pos; ignores if full */
void zappy_pool_add(struct zappy_pool *p, uint8_t position, uint32_t usage);

/* struct zappy_game gains:
 *   enum zappy_mode mode; uint16_t best_hard; struct zappy_pool pool;
 *   uint8_t target;  index into pool, valid in hard mode */
void zappy_init(struct zappy_game *g, uint16_t best, uint16_t best_hard, uint32_t seed,
                const struct zappy_pool *pool); /* pool may be NULL = empty */
bool zappy_press(struct zappy_game *g, uint32_t now_ms, uint8_t position);
char zappy_target_letter(const struct zappy_game *g); /* 0 unless hard mode */
```

Rules:
- On TITLE, a press at `pool.h_pos` starts hard mode if `pool.n >= 2`. Any other press, or H with a pool smaller than 2, starts normal mode.
- In hard PLAYING, a press is accepted (flap, return true) only when `position == pool.pos[target]`. It then picks a new target, uniformly from the other `n-1` entries via the existing xorshift. Any other position returns false and changes nothing.
- In hard mode, CRASHED restart rules (the lockout) are unchanged, and any key restarts in the same mode with a fresh target. Normal mode is unchanged.
- Hard physics uses its own constants: gravity about 70% of normal, gap height `ZAPPY_GAP_H + 24`. Make gap height a per-game value (`g->gap_h`) used by collision and by recycling bounds. Keep `ZAPPY_GAP_H` as the normal value.
- Crash updates `best` or `best_hard` by mode. `new_best` keeps its meaning for the current mode.

- [ ] **Step 1: Update existing tests to the new signatures.** `zappy_init(&g, best, 0, seed, NULL)` and `zappy_press(&g, t, 0)`. Gap-related helpers use `g.gap_h` where they used `ZAPPY_GAP_H`. Then add these new tests:
  - `test_usage_letter`: `zappy_usage_letter((0x07<<16)|0x04) == 'A'`, `(0x07<<16)|0x1D` → `'Z'`, `(0x02u<<24)|(0x07<<16)|0x0B` → `'H'` (mods ignored), `(0x07<<16)|0x2C` → `0` (space), `(0x0C<<16)|0x04` → `0` (consumer page).
  - `test_pool_add`: add positions 1 (Q), 13 (A), 19 (H), 37 (space usage). Expect `n == 3`, `letter[2] == 'H'`, `h_pos == 19`.
  - `test_h_starts_hard`: pool with Q, W, H (positions 1, 2, 19). Pressing 19 on the title gives `mode == ZAPPY_HARD`, `phase == PLAYING`, and `zappy_target_letter(&g) != 0`.
  - `test_other_key_starts_normal`: same pool, press 1 → `mode == ZAPPY_NORMAL`, `zappy_target_letter(&g) == 0`.
  - `test_hard_needs_two_letters` (Focus 1): pool with only H. Pressing H gives `mode == ZAPPY_NORMAL`.
  - `test_wrong_key_ignored`: in hard mode, record `vel_fp` and `target`. Pressing a non-target position returns false, and `vel_fp` and `target` are unchanged.
  - `test_right_key_flaps_and_moves_target`: in hard mode, pressing `pool.pos[target]` returns true, `vel_fp < 0`, and the new target differs. Repeat 200 times (resetting the bird each time); the target never repeats consecutively, and every pool entry is chosen at least once.
  - `test_hard_restart_stays_hard` (Focus 3): crash in hard mode. A press before the lockout returns false. Any position after the lockout gives `mode == ZAPPY_HARD` and `phase == PLAYING`.
  - `test_hard_best_separate`: `init(best=5, best_hard=2)`, play hard, score 3, crash → `best_hard == 3`, `best == 5`, `new_best`.
  - `test_hard_gap_wider`: in hard mode `g.gap_h == ZAPPY_GAP_H + 24`. Run the long-run bounds loop for 20000 steps in hard mode: every `gap_top` stays within `[ZAPPY_MARGIN, ZAPPY_H - ZAPPY_MARGIN - g.gap_h]`.

- [ ] **Step 2: Run the tests to verify they fail.** Run `make -C ~/repos/zmk/prospector-zmk-module/tests/zappy test`. Expected: compile errors (new API missing).

- [ ] **Step 3: Implement it in `zappy_core.c`.**

- [ ] **Step 4: Run the tests.** Expected: `ok 26 tests`.

- [ ] **Step 5: Commit (fork).** Message: `Zappy Birds: hard mode rules in core`.

---

### Task 2: Firmware wiring (behavior, controller, pool, renderer, store)

**Files:** `src/behaviors/behavior_zappy.c`, `src/zappy/zappy_mode.h/.c`, `src/zappy/zappy_screen.c`, `src/zappy/zappy_store.c`

**Interfaces:**
- Consumes the Task 1 API, `zmk_keymap_layer_default()`, `zmk_keymap_get_layer_binding_at_idx(layer, idx)` (`->behavior_dev` name, `->param1`, `->param2`), and `ZMK_KEYMAP_LEN`.
- Produces `void zappy_mode_flap(uint8_t position);`, `void zappy_mode_set_best(enum zappy_mode m, uint16_t best);`, and `void zappy_store_save(enum zappy_mode m, uint16_t best);` (weak no-op stays).

Rules:
- **Behavior:** `&zflap` calls `zappy_mode_flap(event.position)`.
- **Flap queue:** two `atomic_t` bitmasks cover positions 0–31 and 32–63. A flap does `atomic_or` of its bit. Each tick swaps both to 0 and calls `zappy_press` for each set position in ascending order, stopping at the first that returns true (Focus 2). Remove `pending_flaps`. Ignore positions ≥ 64.
- **Pool builder:** runs on first `show_game`, once. For each idx `< ZMK_KEYMAP_LEN` on `zmk_keymap_layer_default()`:
  - If `behavior_dev` equals `"key_press"`, use `param1`. Otherwise use `param2` (hold-taps' tap). This assumes v0.3 stores the node name in `behavior_dev`; verify by logging one binding once during Task 3's debug build, and if it is a label like `"kp"`, match that instead.
  - Feed the usage to `zappy_pool_add`.
- **Bests:** per-mode atomics. Save on beat using `(mode, best)`.
- **Store:** `zappy/hi` maps to normal, `zappy/hh` maps to hard. Same `uint16_t` format, both saved from the system work queue (one work item carrying both latest values is fine).
- **Renderer:**
  - Title shows `any key: play`, `H: hard`, then `BEST n  HARD m`.
  - Hard PLAYING shows the target letter in `FRAC_Regular_48` centered near the top (y≈14), with the score in `CaskaydiaCove_SemiBold_20` top-right. Normal PLAYING is unchanged.
  - Game over shows the current mode's best, plus `HARD` above the score in hard mode.
  - Pylons draw with `g->gap_h`.
  - Only re-text and show/hide on change, like the existing code.

- [ ] **Step 1: Implement it.**
- [ ] **Step 2: Build the matrix.** Run `scripts/build-local.sh all && scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_ZAPPY_BIRDS=n && scripts/build-local.sh dongle`. Expected: all exit 0, with no warnings from `src/zappy` or `behavior_zappy.c`.
- [ ] **Step 3: Run the host tests.** Expected: `ok 26 tests`.
- [ ] **Step 4: Commit (fork).** Message: `Zappy Birds: hard mode on device (pool from keymap, target letter)`.

---

### Task 3: On-device check and tuning (with the user)

- [ ] **Step 1: Debug build.** Build with `-DSNIPPET=zmk-usb-logging` and a temporary `LOG_INF` of the pool, printed once as `n=..` followed by each `pos:letter`, plus `behavior_dev` of position 1. Do not commit these lines. Flash it when the user puts the dongle in bootloader mode, and capture `/dev/ttyACM0`. Pass: `n=26`, letters match the keycaps (e.g. `13:A`, `19:H`).
- [ ] **Step 2: User checklist.**
  - H on the title starts hard mode and any other key starts normal.
  - The shown letter matches the key that flaps, and a wrong key does nothing.
  - The letter changes after each correct press and never repeats.
  - Hard mode is playable.
  - Both bests show on the title and survive a power cycle.
  - Normal mode feels unchanged.
- [ ] **Step 3: Tune** the hard gravity and gap if the user asks. Re-run the host tests after any core change, then commit `Zappy Birds: hard mode tuning`.
- [ ] **Step 4: Normal build.** Revert the debug lines, flash the normal build, and confirm the power-cycle check.
