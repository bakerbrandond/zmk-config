# Dongle Brightness Key Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** `&pbl` on Function-layer position 15 cycles the Prospector backlight
0/5/15/30/60/100, and the level persists across power cycles.

**Spec:** `docs/superpowers/specs/2026-11-15-dongle-brightness-key-design.md`

## Global Constraints

- Fork: `~/repos/zmk/prospector-zmk-module`. Config: `~/repos/zmk/zmk-config`.
  Both are on `main`. Push only when the user asks.
- 36-key Corne: never bind to positions 0, 11, 12, 23, 24 or 35.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

### Task 1: Level stepping (host-tested)

Files: create `src/backlight/bl_levels.{h,c}` and `tests/backlight/{test_levels.c,Makefile}`.

Produces `uint8_t bl_next_level(uint8_t current);`.

Tests:
- `test_cycles_in_order`: 0→5→15→30→60→100→0.
- `test_between_levels_goes_up`: 50→60, 1→5.
- `test_out_of_range_wraps`: 101 and 255 → 0.

Steps:
- [ ] Write the tests.
- [ ] Run them and confirm they fail.
- [ ] Implement it.
- [ ] Run them and confirm `ok 3 tests`.
- [ ] Commit.

### Task 2: Behavior, persistence, keymap

Files:
- Create `dts/bindings/behaviors/zmk,behavior-prospector-backlight.yaml`
  (zero_param) and `src/behaviors/behavior_prospector_bl.c`.
- Modify `boards/shields/prospector_adapter/src/brightness.c` (fixed path) and
  `CMakeLists.txt`.
- Config: modify `config/behaviors.dtsi` (`pbl:` node) and `config/corne.keymap`
  (Function layer position 15 → `&pbl`).

Rules:
- `brightness.c`:
  - holds `current`, initialised to `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS`
  - `void prospector_backlight_cycle(void)` sets `current = bl_next_level(current)`,
    applies it with `led_set_brightness`, then reschedules a 2 s delayable save
    with `settings_save_one("prospector/bl", ...)`
  - a `SETTINGS_STATIC_HANDLER_DEFINE(prospector, "prospector", ...)` handler
    loads `bl` and applies it
- The behavior calls `prospector_backlight_cycle()`. A weak no-op covers the
  ALS mode and non-dongle builds.
- CMake: build the behavior when `CONFIG_DT_HAS_ZMK_BEHAVIOR_PROSPECTOR_BACKLIGHT_ENABLED`,
  plus `bl_levels.c` under the shield.

Steps:
- [ ] Implement it.
- [ ] Build: `scripts/build-local.sh all`. Expected: exit 0.
- [ ] Run the tests.
- [ ] Commit both repos.

### Task 3: Device check

- [ ] Flash the dongle.
- [ ] Each press steps the level, 0 is dark, and the level survives unplugging.
