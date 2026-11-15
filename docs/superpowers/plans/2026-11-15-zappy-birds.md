# Zappy Birds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A hidden tap-to-fly game ("Zappy Birds") on the Prospector dongle's screen, entered from the Function layer, played with any key, with a saved high score.

**Architecture:** Pure-C game core (no Zephyr/LVGL deps, host unit-tested) + an LVGL renderer + a mode controller that swaps screens and owns the 30 FPS `lv_timer`, all in the user's Prospector fork on a new `zappy` branch. Two ZMK behaviors (`&zappy`, `&zflap`) feed the controller; `zmk-config` adds the ZAP layer, exit combo, and points `west.yml` at the fork's `zappy` branch.

**Tech Stack:** C, Zephyr (ZMK v0.3), LVGL 8.3, Zephyr settings (NVS), devicetree, Kconfig; host `gcc` for core tests; Docker `zmkfirmware/zmk-build-arm:stable` for firmware builds.

**Spec:** `docs/superpowers/specs/2026-11-15-zappy-birds-design.md` (read it first).

## Global Constraints

- Target ZMK **v0.3** / LVGL **8.3** APIs. Do NOT use `~/repos/zmk/zmk` (old 2023 `main`) as a reference; use the v0.3 tree in the build workspace (Task 1).
- Fork: `~/repos/zmk/prospector-zmk-module`, new branch `zappy` off `nord`. Never commit to `nord`.
- Config: `~/repos/zmk/zmk-config`, new branch `zappy` off `jiggler`.
- **Never `git push`** either repo without the user's explicit go-ahead.
- Everything game-related is gated by `CONFIG_PROSPECTOR_ZAPPY_BIRDS` (default `n`), enabled only in `config/corne_dongle.conf`.
- `config/corne.keymap` is shared with both halves: all DT nodes must compile in left, right, and dongle builds, with the feature on or off.
- All LVGL calls only on the display work queue (`zmk_display_work_q()`), i.e. inside LVGL timers or work items submitted there. Behavior callbacks never touch LVGL.
- Keymap/layer calls (`zmk_keymap_layer_activate/deactivate`) only from the system work queue / behavior context.
- Game LVGL objects must total < 3 KB; LVGL pool is `CONFIG_LV_Z_MEM_POOL_SIZE=16384` (current peak ~11 KB). No full-screen bitmaps.
- Tick 33 ms (~30 FPS). Crash input lockout 600 ms. Idle auto-exit 60 s. Screen 240×280.
- Original art only: Nord colors from `include/theme.h`, bird NORD13 body + NORD12 bolt, pylons NORD10/NORD9 with NORD8 cap, bg NORD0. Nothing resembling Flappy Bird's bird, green pipes, or sky/ground.
- Name/copy: title text `ZAPPY BIRDS`, prompt `press any key`, game over shows score, `BEST n`, and `NEW BEST` when beaten.
- High score settings key: `zappy/hi`; save only when beaten.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Typing while the game screen is up** — no HID report reaches the host from any key on ZAP, including thumbs and held mods carried over from FUN. Owner: Task 4 (on-device check list + `&zflap` emits nothing).
2. **Toggle pressed again mid-transition / double-tap** — enter then immediately exit must leave layer and screen consistent (status screen shown, ZAP off). Owner: Task 4 (controller uses a single `active` flag; toggle test in checklist).
3. **Very long runs** — score past 255/999 and gap recycling must never overflow or place a gap off-screen. Owner: Task 2 (`test_long_run_stays_in_bounds`).
4. **Feature off / halves** — dongle with `CONFIG_PROSPECTOR_ZAPPY_BIRDS=n` and both halves still build and type normally; `&zappy` presses are ignored, not a crash. Owner: Task 4 (build matrix step).
5. **First boot with no saved score** — best starts at 0 and the title shows `BEST 0`. Owner: Task 5 (`settings` load default + checklist).

---

## File Structure

Fork (`~/repos/zmk/prospector-zmk-module`):
- Modify `zephyr/module.yml` — add `dts_root: .`
- Modify `Kconfig` — `PROSPECTOR_ZAPPY_BIRDS`, `PROSPECTOR_ROLLER_HIDDEN_TRAILING_LAYERS`
- Modify `CMakeLists.txt` — compile zappy sources when enabled
- Create `dts/bindings/behaviors/zmk,behavior-zappy-toggle.yaml`, `zmk,behavior-zappy-flap.yaml`
- Create `src/zappy/zappy_core.h`, `zappy_core.c` — game rules, pure C
- Create `src/zappy/zappy_screen.h`, `zappy_screen.c` — LVGL objects + render
- Create `src/zappy/zappy_mode.h`, `zappy_mode.c` — enter/exit, timer, idle, flap queue
- Create `src/zappy/zappy_store.c` — high score settings handler + save
- Create `src/behaviors/behavior_zappy.c` — both behavior drivers
- Create `tests/zappy/test_core.c`, `tests/zappy/Makefile` — host tests
- Modify `boards/shields/prospector_adapter/src/widgets/layer_roller.c` — hide trailing layers

Config (`~/repos/zmk/zmk-config`):
- Modify `config/behaviors.dtsi` — `zappy` / `zflap` nodes
- Modify `config/corne.keymap` — `#define ZAP 5`, zappy layer, FUN slot, exit combo
- Modify `config/corne_dongle.conf` — enable feature
- Modify `config/west.yml` — fork `revision: zappy`
- Create `scripts/build-local.sh` — local Docker build of dongle/left/right

---

### Task 1: Branches, module plumbing, local build harness

**Files:**
- Modify (fork): `zephyr/module.yml`, `Kconfig`, `CMakeLists.txt`
- Create (fork): `dts/bindings/behaviors/zmk,behavior-zappy-toggle.yaml`, `dts/bindings/behaviors/zmk,behavior-zappy-flap.yaml`
- Create (config): `scripts/build-local.sh`

**Interfaces:**
- Produces: `CONFIG_PROSPECTOR_ZAPPY_BIRDS` (bool, default n, `depends on SHIELD_PROSPECTOR_ADAPTER`, `select LV_USE_LABEL`); `CONFIG_PROSPECTOR_ROLLER_HIDDEN_TRAILING_LAYERS` (int, range 0–8, `default 1 if PROSPECTOR_ZAPPY_BIRDS`, `default 0`); compatibles `zmk,behavior-zappy-toggle` (include `one_param.yaml`) and `zmk,behavior-zappy-flap` (include `zero_param.yaml`); `scripts/build-local.sh <dongle|left|right|all> [extra west args]` producing `.uf2` files under `$OUT/<target>/zephyr/zmk.uf2`.

- [ ] **Step 1: Create branches**

```bash
git -C ~/repos/zmk/prospector-zmk-module switch -c zappy nord
git -C ~/repos/zmk/zmk-config switch -c zappy jiggler
```
Expected: both report `Switched to a new branch 'zappy'`. The config repo has untracked `docs/` — leave it untracked except files this plan creates.

- [ ] **Step 2: Write `scripts/build-local.sh`**

Behaviour: workspace at `${ZAPPY_WS:-$HOME/.cache/zmk-zappy-ws}`, output `${ZAPPY_OUT:-$ZAPPY_WS/out}`. First run: copy `config/` into `$WS/config`, `west init -l config && west update` inside the Docker image. Every run: rsync `config/` into `$WS/config`, then run `west build` in `zmkfirmware/zmk-build-arm:stable` with `-v $WS:/ws -v ~/repos/zmk/prospector-zmk-module:/ws/prospector-zmk-module -v <zmk-config root>:/zmk-config` (the bind mount shadows the west-fetched fork so local unpushed commits are built), `-DZMK_CONFIG=/ws/config -DZMK_EXTRA_MODULES=/zmk-config`, pristine (`-p`). Targets mirror `build.yaml`:
- `dongle`: `-b seeeduino_xiao_ble -- -DSHIELD="corne_dongle prospector_adapter"`
- `left`: `-b nice_nano_v2 -- -DSHIELD="corne_left nice_view_adapter nice_view"`
- `right`: same with `corne_right`
Extra args after the target are appended to the cmake args (used for `-DCONFIG_PROSPECTOR_ZAPPY_BIRDS=n`). `set -euo pipefail`; exits non-zero on any build failure.

Note: `west.yml` still says `revision: nord` until Task 4; `west update` must not be re-run after Task 4 changes the revision before it is pushed (the bind mount makes the fetched revision irrelevant; the script only runs `west update` when `$WS/.west` is missing).

- [ ] **Step 3: Baseline build (before any fork change) to prove the harness**

Run: `scripts/build-local.sh all`
Expected: three `zmk.uf2` files, exit 0. If Docker needs `sudo`, stop and ask the user.

- [ ] **Step 4: Add `dts_root: .` to `zephyr/module.yml` under `build.settings`, the two Kconfig symbols, the two binding YAMLs, and in `CMakeLists.txt` (inside the existing `if(CONFIG_SHIELD_PROSPECTOR_ADAPTER)`) an `if(CONFIG_PROSPECTOR_ZAPPY_BIRDS)` block with `zephyr_library_sources()` for the future zappy files (leave it listing nothing yet, or comment the list — no missing files may be referenced).**

- [ ] **Step 5: Rebuild all**

Run: `scripts/build-local.sh all`
Expected: exit 0, all three uf2s.

- [ ] **Step 6: Commit both repos**

```bash
git -C ~/repos/zmk/prospector-zmk-module add zephyr/module.yml Kconfig CMakeLists.txt dts
git -C ~/repos/zmk/prospector-zmk-module commit -m "Zappy Birds: module plumbing (dts_root, Kconfig, bindings)"
git -C ~/repos/zmk/zmk-config add scripts/build-local.sh
git -C ~/repos/zmk/zmk-config commit -m "Add local Docker build script"
```

---

### Task 2: Game core (pure C, host-tested)

**Files:**
- Create (fork): `src/zappy/zappy_core.h`, `src/zappy/zappy_core.c`
- Test (fork): `tests/zappy/test_core.c`, `tests/zappy/Makefile`

**Interfaces:**
- Produces (in `zappy_core.h`, only `<stdint.h>`/`<stdbool.h>`):

```c
#define ZAPPY_W 240
#define ZAPPY_H 280
#define ZAPPY_TICK_MS 33
#define ZAPPY_CRASH_LOCKOUT_MS 600
#define ZAPPY_NUM_PYLONS 3
#define ZAPPY_BIRD_X 60
#define ZAPPY_BIRD_SIZE 16      /* collision box, square */
#define ZAPPY_PYLON_W 36
#define ZAPPY_GAP_H 92          /* tune in Task 3 if needed */
#define ZAPPY_MARGIN 24         /* min distance of gap edge from top/bottom */

enum zappy_phase { ZAPPY_TITLE, ZAPPY_PLAYING, ZAPPY_CRASHED };

struct zappy_pylon { int16_t x; int16_t gap_top; bool scored; };

struct zappy_game {
    enum zappy_phase phase;
    int32_t bird_y_fp;   /* top of bird, 24.8 fixed point */
    int32_t vel_fp;      /* +down, 24.8 */
    struct zappy_pylon pylons[ZAPPY_NUM_PYLONS];
    uint16_t score, best;
    bool new_best;
    uint32_t crash_ms;   /* now_ms when crashed */
    uint32_t rng;        /* xorshift32 state, never 0 */
    uint8_t flap_frames; /* >0 while showing flap pose */
};

void zappy_init(struct zappy_game *g, uint16_t best, uint32_t seed);
/* Returns true if the press did something (start, flap, restart). */
bool zappy_press(struct zappy_game *g, uint32_t now_ms);
void zappy_step(struct zappy_game *g, uint32_t now_ms);
int16_t zappy_bird_y(const struct zappy_game *g);
```

Rules: TITLE → press starts PLAYING with a flap and pylons placed off the right edge, spaced evenly (`ZAPPY_W/ZAPPY_NUM_PYLONS + ZAPPY_PYLON_W` apart, first at `ZAPPY_W + 40`). PLAYING: gravity each step, press sets `vel_fp` to a fixed upward value (no stacking), bird clamped at y=0 (ceiling does not kill), touching `ZAPPY_H` floor or a pylon's solid part crashes. Pylons move left a fixed px/step; once `x + ZAPPY_PYLON_W < 0`, recycle to the right of the right-most pylon with a new `gap_top` from xorshift in `[ZAPPY_MARGIN, ZAPPY_H - ZAPPY_MARGIN - ZAPPY_GAP_H]`. Score +1 once per pylon when its right edge passes `ZAPPY_BIRD_X`; score saturates at 9999. On crash: phase CRASHED, `crash_ms = now_ms`, if `score > best` then `best = score; new_best = true`. CRASHED: presses before `crash_ms + ZAPPY_CRASH_LOCKOUT_MS` return false; after, a press restarts directly into PLAYING (score 0, new_best false). TITLE step: bird bobs (small sine-free triangle wave), no gravity. Gravity/flap/scroll constants are the builder's to tune; define them in `zappy_core.c`.

- [ ] **Step 1: Write failing tests in `tests/zappy/test_core.c`** (plain `assert`, `main` runs all, prints `ok N tests`). `Makefile`: `test: ; cc -std=c11 -Wall -Wextra -Werror -I../../src/zappy test_core.c ../../src/zappy/zappy_core.c -o test_core && ./test_core`.

Tests (names and assertions):
- `test_init_title`: after `zappy_init(&g, 7, 1)`: `phase == ZAPPY_TITLE`, `best == 7`, `score == 0`.
- `test_press_starts_game`: press at t=0 → returns true, `phase == ZAPPY_PLAYING`, `vel_fp < 0`.
- `test_gravity_pulls_down`: start, step without pressing; assert `zappy_bird_y` after 30 steps > `zappy_bird_y` after 10 steps (the start flap has worn off by then).
- `test_flap_moves_up`: start, 20 steps, record y, press, 3 steps → y less than recorded.
- `test_ceiling_clamps`: start, press every step for 100 steps (with steps between) → `zappy_bird_y(&g) >= 0` and phase still PLAYING unless a pylon was hit (place pylons far right by checking within first 40 steps only).
- `test_floor_crashes`: start, step until phase changes (max 500) → `phase == ZAPPY_CRASHED`.
- `test_pylon_crash`: start; set `bird_y_fp = (ZAPPY_H/2) << 8`, `vel_fp = 0`, `pylons[0].x = ZAPPY_BIRD_X`, `pylons[0].gap_top = ZAPPY_MARGIN` (gap ends at 116, above the bird at 140) → after 1 step `phase == ZAPPY_CRASHED`.
- `test_score_once_per_pylon`: start; put pylon 0 so the bird is inside its gap and its right edge is 1 px right of `ZAPPY_BIRD_X`; hold bird in gap by resetting `bird_y_fp`/`vel_fp` before each step; step until right edge passes → `score == 1`; step 10 more (still holding) → `score == 1`.
- `test_crash_lockout`: crash at known `now`; `zappy_press(&g, crash_ms + 599)` returns false and phase CRASHED; `zappy_press(&g, crash_ms + 600)` returns true, phase PLAYING, `score == 0`.
- `test_new_best`: `init(best=0)`, force `score = 3`, force crash (step until floor) → `best == 3`, `new_best == true`; restart → `new_best == false`, `best == 3`.
- `test_no_new_best_when_lower`: `init(best=10)`, score 3, crash → `best == 10`, `new_best == false`.
- `test_long_run_stays_in_bounds` (Review Focus 3): start; for 100000 steps, before each step set bird into the gap of the nearest pylon (`bird_y_fp = (gap_top + ZAPPY_GAP_H/2 - ZAPPY_BIRD_SIZE/2) << 8`, `vel_fp = 0`); after each step assert every pylon `gap_top` in `[ZAPPY_MARGIN, ZAPPY_H - ZAPPY_MARGIN - ZAPPY_GAP_H]` and `x` in `[-ZAPPY_PYLON_W, ZAPPY_W + 2*ZAPPY_W]`; at end `score > 255` and `score <= 9999`, phase PLAYING.
- `test_deterministic_seed`: two games with seed 42 driven identically produce identical pylon `gap_top`s after 2000 steps.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/zappy test`
Expected: compile/link error (functions undefined).

- [ ] **Step 3: Implement `zappy_core.c`** per the rules above. Fixed-point integer math only (no floats — keeps the firmware lean).

- [ ] **Step 4: Run tests**

Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/zappy test`
Expected: `ok 13 tests`, exit 0.

- [ ] **Step 5: Add `src/zappy/zappy_core.c` to the CMake zappy block; build dongle with feature on**

Run: `scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_ZAPPY_BIRDS=y`
Expected: exit 0 (core compiles under Zephyr with `-Werror`-level warnings clean).

- [ ] **Step 6: Commit (fork)**: add `src/zappy/zappy_core.*`, `tests/zappy/`, `CMakeLists.txt`; message `Zappy Birds: game core with host tests`. Add `tests/zappy/test_core` to `.gitignore`.

---

### Task 3: Renderer (game screen, art, title, crash)

**Files:**
- Create (fork): `src/zappy/zappy_screen.h`, `src/zappy/zappy_screen.c`

**Interfaces:**
- Consumes: `struct zappy_game`, constants from `zappy_core.h`; `theme.h`, `fonts.h` (add `boards/shields/prospector_adapter/include` to the zappy CMake include path).
- Produces:

```c
lv_obj_t *zappy_screen_create(void);               /* once, lazily; returns screen */
void zappy_screen_render(const struct zappy_game *g); /* call every tick on display queue */
```

Objects created once and only moved/shown/hidden per frame (never created/deleted per frame): bird (body circle + bolt object(s) + eye; flap pose when `flap_frames > 0`), per pylon a top and bottom bar each with a NORD8 cap, score label (top center, PLAYING only), title group (`ZAPPY BIRDS`, `press any key`, `BEST n`), game-over group (score, `BEST n`, `NEW BEST` when `new_best`). Crash flash: for the first ~150 ms after `crash_ms`, tint the screen background NORD11/NORD13 then return to NORD0 (renderer may read a ms clock passed via the game's `crash_ms` and `k_uptime_get_32()`). Use fonts already in `fonts.h`; pick sizes that fit 240 px width. Pick only existing LVGL 8.3 widgets enabled by the shield (`lv_obj`, label); add `select` in Kconfig for anything else.

- [ ] **Step 1: Implement `zappy_screen.c` and add it to the CMake zappy block.**

- [ ] **Step 2: Build**

Run: `scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_ZAPPY_BIRDS=y`
Expected: exit 0, no warnings from `src/zappy/`.

- [ ] **Step 3: Commit (fork)**: `Zappy Birds: LVGL renderer and original Nord art`.

(Visual verification happens on device in Task 6; the renderer can't be exercised until Task 4 wires it up.)

---

### Task 4: Mode controller, behaviors, keymap wiring

**Files:**
- Create (fork): `src/zappy/zappy_mode.h`, `src/zappy/zappy_mode.c`, `src/behaviors/behavior_zappy.c`
- Modify (config): `config/behaviors.dtsi`, `config/corne.keymap`, `config/corne_dongle.conf`, `config/west.yml`

**Interfaces:**
- Consumes: `zappy_core.h` API, `zappy_screen_create/render`, `zmk_display_work_q()`, `zmk_keymap_layer_activate/deactivate(zmk_keymap_layer_id_t)`, `zmk_keymap_layer_index_to_id()`.
- Produces (`zappy_mode.h`):

```c
void zappy_mode_toggle(uint8_t layer_index); /* call from behavior context */
void zappy_mode_flap(void);                  /* call from behavior context */
bool zappy_mode_active(void);
uint16_t zappy_mode_best(void);              /* for store, Task 5 */
void zappy_mode_set_best(uint16_t best);     /* store loads at boot, Task 5 */
```
Optional hook used by Task 5: `void zappy_store_save(uint16_t best);` declared in `zappy_mode.h`, provided by Task 5 as a weak no-op default here.

Controller rules:
- `active` is an `atomic_t`; toggle flips it in behavior context, calls `zmk_keymap_layer_activate/deactivate(zmk_keymap_layer_index_to_id(layer_index))` there, stores `layer_index`, then submits an enter or exit `k_work` to `zmk_display_work_q()`.
- Enter work (display queue): if `lv_scr_act()` is NULL, abort and undo (deactivate via system-queue work, clear `active`). Save `lv_scr_act()` as the status screen, create the game screen lazily, `zappy_init(&g, best, k_cycle_get_32() | 1)`, render, `lv_scr_load`, create the 33 ms `lv_timer`.
- Exit work: delete the timer, `lv_scr_load(saved_status_screen)`. The game screen object stays allocated for reuse.
- Enter/exit work items are idempotent against `active`: each checks the current `active` value and the timer's existence, so a fast double toggle ends consistent (Review Focus 2).
- Flap: if `active`, increment an `atomic_t pending_flaps` and record `last_input_ms = k_uptime_get_32()`. Timer callback consumes all pending flaps by calling `zappy_press` once (multiple queued presses in one tick count as one), then `zappy_step`, then `zappy_screen_render`.
- When `zappy_press`/`step` produce `new_best` for the first time in a run, call `zappy_store_save(g.best)`.
- Idle: in the timer, if `k_uptime_get_32() - last_input_ms > 60000`, submit a system-queue `k_work` that performs the same as a toggle-off (deactivate layer, clear `active`, submit exit work). `last_input_ms` is set on enter too.

Behaviors (`behavior_zappy.c`, both compats in one file using `DT_DRV_COMPAT` redefinition, `BEHAVIOR_DT_INST_DEFINE` per instance, guarded by `DT_HAS_COMPAT_STATUS_OKAY`): `zappy_toggle` pressed → `zappy_mode_toggle(binding->param1)`, return `ZMK_BEHAVIOR_OPAQUE`; released → `ZMK_BEHAVIOR_OPAQUE`. `zappy_flap` pressed → `zappy_mode_flap()`, opaque; released → opaque. No HID calls anywhere (Review Focus 1).

Config:
- `config/behaviors.dtsi` (inside the existing `/ { }`):
  `zappy: zappy_toggle { compatible = "zmk,behavior-zappy-toggle"; #binding-cells = <1>; };`
  `zflap: zappy_flap { compatible = "zmk,behavior-zappy-flap"; #binding-cells = <0>; };`
- `config/corne.keymap`: `#define ZAP 5` next to the other layer defines; new last layer `zappy_layer { display-name = "Zappy"; bindings = < 42 × &zflap >; };` with the same box-comment layout as other layers; FUN layer row 2 position 22 (the first `___` right of `&out OUT_TOG`) becomes `&zappy ZAP`; add
  ```
  combos { compatible = "zmk,combos";
    zappy_exit { key-positions = <1 10>; bindings = <&zappy ZAP>; layers = <ZAP>;
                 timeout-ms = <40>; require-prior-idle-ms = <300>; }; };
  ```
- `config/corne_dongle.conf`: `CONFIG_PROSPECTOR_ZAPPY_BIRDS=y` with a one-line comment.
- `config/west.yml`: fork `revision: zappy`, update the trailing comment.

- [ ] **Step 1: Implement controller + behaviors; add both to the CMake zappy block.**

- [ ] **Step 2: Make the config changes above.**

- [ ] **Step 3: Build matrix (Review Focus 4)**

Run: `scripts/build-local.sh all && scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_ZAPPY_BIRDS=n`
Expected: all exit 0. The `=n` dongle build runs last, so then `grep -c zappy_mode_toggle "$ZAPPY_OUT/dongle/zephyr/zephyr.map"` prints `0` (no game code linked). Rebuild `scripts/build-local.sh dongle` afterwards so the on-device uf2 has the game.

- [ ] **Step 4: Commit both repos**: fork `Zappy Birds: mode controller and behaviors`; config `Wire Zappy Birds layer, FUN key and exit combo`.

---

### Task 5: High score persistence + hide layer from roller

**Files:**
- Create (fork): `src/zappy/zappy_store.c`
- Modify (fork): `boards/shields/prospector_adapter/src/widgets/layer_roller.c`

**Interfaces:**
- Consumes: `zappy_mode_set_best(uint16_t)`, the weak `zappy_store_save(uint16_t)` hook.
- Produces: strong `zappy_store_save(uint16_t best)` that submits a system-workqueue `k_work` calling `settings_save_one("zappy/hi", &best_copy, sizeof(uint16_t))` (never writes flash from the display thread); `SETTINGS_STATIC_HANDLER_DEFINE(zappy, "zappy", NULL, h_set, NULL, NULL)` whose `h_set` for key `hi` reads a `uint16_t` and calls `zappy_mode_set_best`. No saved key → best stays 0 (Review Focus 5).

Roller: build the options list for layer indices `0 .. ZMK_KEYMAP_LAYERS_LEN - 1 - CONFIG_PROSPECTOR_ROLLER_HIDDEN_TRAILING_LAYERS` only; in the update callback ignore (keep current selection) any `index` ≥ that visible count. With the feature off the symbol is 0 and behaviour is unchanged.

- [ ] **Step 1: Implement store and roller change; add store to the CMake zappy block.**

- [ ] **Step 2: Build matrix**

Run: `scripts/build-local.sh all && scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_ZAPPY_BIRDS=n`
Expected: all exit 0.

- [ ] **Step 3: Re-run host tests**

Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/zappy test`
Expected: `ok 13 tests`.

- [ ] **Step 4: Commit (fork)**: `Zappy Birds: persist high score; hide game layer from roller`.

---

### Task 6: On-device verification (with the user)

**Files:** none (adjust constants in `zappy_core.c` / renderer only if checks fail, then re-run Task 2 tests).

- [ ] **Step 1: Heap check build.** Build dongle with `-DCONFIG_LV_USE_MEM_MONITOR=y -DCONFIG_ZMK_USB_LOGGING=y` plus a temporary `LOG_INF` of `lv_mem_monitor()` used/peak from the controller's exit path (do not commit the log). Hand the user `$OUT/dongle/zephyr/zmk.uf2` path to flash; ask them to enter the game, play a run, crash, exit, and share the USB log (`! sudo tio /dev/ttyACM0` or similar). Pass: peak used ≤ 14 KB of 16 KB and game delta < 3 KB. If over, shrink objects before raising `CONFIG_LV_Z_MEM_POOL_SIZE`.

- [ ] **Step 2: Flash the normal dongle build and walk the user through this checklist; record each result:**
  1. Status screen looks exactly as before; no "Zappy" in the layer list.
  2. FUN + slot right of OUT toggle → title screen with `ZAPPY BIRDS`, `press any key`, `BEST 0` on first ever run.
  3. With a text editor focused, mash every key including thumbs while playing → nothing typed.
  4. Crash → flash, game-over card; mashing right after crash doesn't instantly restart.
  5. Beat the score, power-cycle the dongle, re-enter → `BEST` shows the new score.
  6. Press Q+P together after a pause → back to status screen; widgets (battery, layer, WPM, indicators) update; typing normal.
  7. Mash Q and P with both hands during play → does not exit.
  8. Enter and immediately exit twice fast → status screen, normal layers.
  9. Enter and leave it alone 60 s → returns to status screen on its own.
  10. Jiggler still works (`&aim AIM_TOG`), BT/USB toggle still works.

- [ ] **Step 3: Tune** physics/feel if the user asks; re-run host tests after any core change; commit `Zappy Birds: tuning`.

- [ ] **Step 4: Ask the user** whether to push the fork `zappy` branch and the config `zappy` branch (needed for GitHub Actions builds). Push only on a yes.
