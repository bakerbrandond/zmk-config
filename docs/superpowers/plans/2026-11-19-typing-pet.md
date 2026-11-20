# Typing Pet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Clackling, an original typing pet that can be swapped in for the Prospector status screen's WPM panel. It reacts to your typing, grows with your lifetime keystrokes, and can be fed and played with.

**Architecture:** It follows the Zappy Birds pattern in the fork (`~/repos/zmk/prospector-zmk-module`, `src/pet/`):
- Pure-C rules in `pet_core` and the sprite data in `pet_sprites`, both host-tested.
- An LVGL widget in `pet_panel`, sized and placed like the WPM panel.
- A controller in `pet_mode`, which swaps the two widgets on the display work queue and keeps the pet's life ticking on the system work queue.
- Settings persistence in `pet_store`, and two behaviors.

This repo (`~/repos/zmk/zmk-config`) adds the keymap bindings, the mini-game capture layer, the dongle `.conf` and the README.

**Tech Stack:** ZMK v0.3 (Zephyr 3.5, LVGL 8), C11, host tests with `cc` and `make`, and the Docker build via `scripts/build-local.sh`.

**Spec:** `docs/superpowers/specs/2026-11-19-typing-pet-design.md` (in this repo)

## Global Constraints

- **Original work:** don't copy Tamagotchi or any other pet game (name, characters, egg or device shapes, sprites, icons, sounds or menus). The name is Clackling.
- **Keys:**
  - Function + , = swap the WPM panel for the pet and back (saved), Function + . = feed, Function + / = mini-game. These are positions 32, 33 and 34 on the `func_layer`.
  - Never bind positions 0, 11, 12, 23, 24 or 35.
  - Feed and play do nothing, and send nothing, while the WPM panel is showing.
- **Panel:** the pet takes the WPM panel's exact box (204×64 at x 16, y 124). Swapping touches no other widget.
- **Typing while shown:** no capture layer is active while the pet is showing, except during the mini-game.
- **Stages (growth values):** Egg at 0, Baby at 500, Child at 10,000, Teen at 100,000, Adult at 1,000,000.
- **Growth:** +1 per press, or +½ while hungry. Store it in half-points.
- **Keys:** the true press count, always +1.
- **Fullness:** drains full to empty over 24 h of dongle uptime. Unplugged time doesn't count.
- **Idle:** asleep after 2 min idle. The matrix rain and Zappy Birds are unchanged: they take over the whole screen and hand it back, pet included.
- **Mini-game:** about 10 s, about 5 rounds, guess left or right half, played inside the pet panel. It uses a capture layer named "Pet", hidden from the layer roller like "Zappy", and always ends by itself.
- **Mini-game reward:** mood +1 level for a few minutes. Not saved.
- **Hearts:** show the current mood level (WPM plus the mini-game boost). They're not a separate stat.
- **Counting:** count physical presses, excluding presses while the Zappy layer or the mini-game layer is active.
- **Saving:** Zephyr settings subtree `pet`, from the system work queue. At most every 15 min, only if something changed, plus on a stage-up.
- **Kconfig:** `CONFIG_PROSPECTOR_TYPING_PET`, default n, set to y in `config/corne_dongle.conf`.
- **Repo rules:**
  - Commits are signed with the user's SSH key and need no extra flags.
  - Push only when the user asks.
  - Flash only after the user says the dongle is in bootloader mode.
  - Never commit the untracked `docs/superpowers/*/2026-11-15-ploopy-trackpad-*` files.
- **Commit trailer:** end commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **uint32 wrap and long uptimes:**
   - `k_uptime_get_32()` wraps after 49.7 days.
   - Hunger drain, idle checks and save timing should use differences of unsigned ms (`now - then`), which wrap safely, and must never go negative or jump.
   - A test in Task 1 covers drain across a wrap.
2. **Very large counts:** `growth_half` reaches 2,000,000 at Adult and keeps rising. Stage lookup must clamp at Adult and the "next" milestone must report none. Covered by tests in Task 1.
3. **Corrupt or old saved data:** a settings blob of the wrong length or version must be ignored, leaving a fresh egg, and must not crash. Covered by a test in Task 2.
4. **The mini-game must always end:**
   - Guesses after it ends, or several guesses in one round, must not extend it.
   - A hard cap ends it even if `pet_game_step` is called late.
   - Covered by tests in Task 3.
5. **A mini-game interrupted by Zappy Birds or the screensaver:**
   - The game must end, and the "Pet" layer must be released, if Zappy opens or the matrix rain starts mid-game.
   - No key may get stuck on the capture layer.
   - The pure rules are covered by the hard cap test in Task 3. The wiring is checked by hand in Task 7.

---

### Task 1: Pet rules (stages, growth, hunger, feed)

**Files:**
- Create: `prospector-zmk-module/src/pet/pet_core.h`, `prospector-zmk-module/src/pet/pet_core.c`
- Test: `prospector-zmk-module/tests/pet/test_core.c`, `prospector-zmk-module/tests/pet/Makefile`, `prospector-zmk-module/tests/pet/.gitignore` (containing `test_core` and `test_sprites`)

**Interfaces:**
- Produces:
```c
#define PET_FULL_MS 86400000u        /* 24 h */
enum pet_stage { PET_EGG, PET_BABY, PET_CHILD, PET_TEEN, PET_ADULT, PET_STAGES };
struct pet_state {
    uint32_t keys;        /* true lifetime presses */
    uint32_t growth_half; /* growth in half-points */
    uint32_t fullness_ms; /* 0..PET_FULL_MS */
    uint8_t shown_stage;  /* last stage celebrated on screen */
};
void pet_init(struct pet_state *p);                  /* fresh egg, full */
enum pet_stage pet_stage_of(uint32_t growth_half);
/* growth value (whole points) of the next stage, 0 if Adult */
uint32_t pet_next_threshold(uint32_t growth_half);
bool pet_hungry(const struct pet_state *p);          /* fullness_ms == 0 */
/* returns true if the stage went up */
bool pet_add_presses(struct pet_state *p, uint32_t n);
void pet_elapse(struct pet_state *p, uint32_t elapsed_ms);
void pet_feed(struct pet_state *p);                  /* back to PET_FULL_MS */
```

- [ ] **Step 1: Write the failing tests** in `tests/pet/test_core.c`, using the `RUN(...)` style of `tests/roller/test_roller.c`:
  - `test_init_is_full_egg`: after `pet_init`, `keys == 0`, `growth_half == 0`, `fullness_ms == PET_FULL_MS`, `pet_stage_of(0) == PET_EGG`.
  - `test_stage_thresholds`: `pet_stage_of(2*499) == PET_EGG`, `pet_stage_of(2*500) == PET_BABY`, `pet_stage_of(2*10000) == PET_CHILD`, `pet_stage_of(2*100000) == PET_TEEN`, `pet_stage_of(2*1000000) == PET_ADULT`, `pet_stage_of(UINT32_MAX) == PET_ADULT`.
  - `test_next_threshold`: `pet_next_threshold(0) == 500`, `pet_next_threshold(2*500) == 10000`, `pet_next_threshold(2*1000000) == 0`.
  - `test_fed_press_counts_full`: 3 presses give `keys == 3` and `growth_half == 6`.
  - `test_hungry_press_counts_half`: with `fullness_ms = 0`, 3 presses give `keys == 3` and `growth_half == 3`.
  - `test_add_presses_reports_stage_up`: at `growth_half = 2*499`, `pet_add_presses(p, 1)` returns true; at `2*500`, it returns false.
  - `test_growth_saturates`: at `growth_half = UINT32_MAX - 1`, adding 10 presses leaves `growth_half == UINT32_MAX` and `keys` still +10.
  - `test_drain_24h`: `pet_elapse(p, PET_FULL_MS/2)` leaves fullness at half; a further `pet_elapse(p, PET_FULL_MS)` leaves 0, with no underflow; `pet_hungry` is then true.
  - `test_feed_refills`: after draining to 0, `pet_feed` sets `fullness_ms == PET_FULL_MS` and `pet_hungry` is false.
  - `test_elapse_wrap_delta`: with `then = UINT32_MAX - 999` and `now = 1000`, `pet_elapse(p, now - then)` drains exactly 2000 ms.

  `Makefile`, after `tests/zappy/Makefile`:
  ```make
  test:
  	cc -std=c11 -Wall -Wextra -Werror -I../../src/pet test_core.c ../../src/pet/pet_core.c -o test_core && ./test_core
  .PHONY: test
  ```

- [ ] **Step 2: Run the tests to verify they fail**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: compile FAIL (`pet_core.h` not found).

- [ ] **Step 3: Implement `pet_core.h` and `pet_core.c`.** Keep them free of Zephyr and LVGL, like `zappy_core`. Thresholds go in a `static const uint32_t` table of growth values (whole points), compared against `growth_half / 2`.

- [ ] **Step 4: Run the tests to verify they pass**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: PASS, every test is listed, exit 0.

- [ ] **Step 5: Commit** (fork)
  ```bash
  cd ~/repos/zmk/prospector-zmk-module
  git add src/pet/pet_core.[ch] tests/pet
  git commit -m "Typing pet: stages, growth and hunger rules

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 2: Mood, idle, saving rules and the save format

**Files:**
- Modify: `prospector-zmk-module/src/pet/pet_core.h`, `prospector-zmk-module/src/pet/pet_core.c`
- Test: `prospector-zmk-module/tests/pet/test_core.c`

**Interfaces:**
- Consumes: `struct pet_state` from Task 1.
- Produces:
```c
#define PET_SLEEP_IDLE_MS 120000u
#define PET_SAVE_EVERY_MS 900000u
#define PET_BOOST_MS 300000u
#define PET_BLOB_LEN 14
#define PET_BLOB_VERSION 1
enum pet_mood { PET_ASLEEP, PET_SLEEPY, PET_CALM, PET_HAPPY, PET_EXCITED };
/* hearts shown = mood level (0..4) */
enum pet_mood pet_mood_of(int wpm, uint32_t idle_ms, bool boost);
bool pet_save_due(uint32_t since_save_ms, bool dirty);
void pet_pack(const struct pet_state *p, uint8_t out[PET_BLOB_LEN]);
bool pet_unpack(const uint8_t *buf, size_t len, struct pet_state *out); /* false: *out untouched */
```
- **Mood bands (my call):**
  - `idle_ms >= PET_SLEEP_IDLE_MS` → ASLEEP, which overrides the boost.
  - Otherwise wpm ≥ 60 → EXCITED, ≥ 25 → HAPPY, > 0 → CALM, and 0 → SLEEPY.
  - The boost raises the mood one level, capped at EXCITED.
- **Blob layout:** version (1 byte), keys (4), growth_half (4) and fullness_ms (4), little-endian, then shown_stage (1). `pet_unpack` clamps `fullness_ms` to `PET_FULL_MS` and `shown_stage` to `PET_ADULT`.

- [ ] **Step 1: Write the failing tests**
  - `test_mood_bands`: `pet_mood_of(80,0,false)==PET_EXCITED`, `(30,0,false)==PET_HAPPY`, `(5,0,false)==PET_CALM`, `(0,1000,false)==PET_SLEEPY`, `(0,PET_SLEEP_IDLE_MS,false)==PET_ASLEEP`.
  - `test_boost_raises_one`: `(5,0,true)==PET_HAPPY`, `(80,0,true)==PET_EXCITED`, `(0,PET_SLEEP_IDLE_MS,true)==PET_ASLEEP`.
  - `test_save_due`: `pet_save_due(PET_SAVE_EVERY_MS, false)` is false, `(PET_SAVE_EVERY_MS-1, true)` is false and `(PET_SAVE_EVERY_MS, true)` is true.
  - `test_pack_roundtrip`: the state {123456, 2*10001, 4321, PET_CHILD} survives pack and unpack unchanged.
  - `test_unpack_rejects`: wrong length (13), version 2, and a NULL-free buffer of zeros with a wrong version each return false and leave `out` unchanged (pre-fill it with a sentinel).
  - `test_unpack_clamps`: `fullness_ms = 0xFFFFFFFF` and `shown_stage = 9` unpack to `PET_FULL_MS` and `PET_ADULT`.

- [ ] **Step 2: Run the tests to verify they fail**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: compile FAIL (undeclared `pet_mood_of`, among others).

- [ ] **Step 3: Implement** the functions above in `pet_core.c`.

- [ ] **Step 4: Run the tests to verify they pass**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: PASS, exit 0.

- [ ] **Step 5: Commit**
  ```bash
  git add src/pet/pet_core.[ch] tests/pet/test_core.c
  git commit -m "Typing pet: mood, idle and save rules

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 3: Mini-game rules

**Files:**
- Modify: `prospector-zmk-module/src/pet/pet_core.h`, `prospector-zmk-module/src/pet/pet_core.c`
- Test: `prospector-zmk-module/tests/pet/test_core.c`

**Interfaces:**
- Produces:
```c
#define PET_GAME_ROUNDS 5
#define PET_ROUND_MS 2000u
#define PET_REVEAL_MS 600u
#define PET_RESULT_MS 2000u
#define PET_GAME_CAP_MS 16000u   /* hard stop from start */
#define PET_GAME_WIN 3           /* score that earns the boost */
enum pet_side { PET_LEFT, PET_RIGHT };
enum pet_game_phase { PET_GAME_OFF, PET_GAME_ROUND, PET_GAME_REVEAL, PET_GAME_RESULT };
struct pet_game {
    enum pet_game_phase phase;
    uint8_t round, score;
    enum pet_side hide;      /* where the pet hides this round */
    bool last_hit;           /* REVEAL: was the guess right */
    uint32_t start_ms, phase_end_ms, rng;
};
/* Corne 42-key positions: columns 0-5 of rows 0-2 and thumbs 36-38 are left */
enum pet_side pet_side_of_position(uint8_t pos);
void pet_game_start(struct pet_game *g, uint32_t seed, uint32_t now_ms);
void pet_game_guess(struct pet_game *g, enum pet_side side, uint32_t now_ms); /* ROUND only */
void pet_game_step(struct pet_game *g, uint32_t now_ms);
/* true while the capture layer must stay on (ROUND or REVEAL) */
bool pet_game_capturing(const struct pet_game *g);
bool pet_game_won(const struct pet_game *g); /* RESULT and score >= PET_GAME_WIN */
```
- **Flow:**
  1. ROUND, with a deadline of `PET_ROUND_MS`.
  2. A guess, or the deadline (a miss), leads to REVEAL for `PET_REVEAL_MS`.
  3. REVEAL leads to the next ROUND, or to RESULT after round 5. RESULT lasts `PET_RESULT_MS` and then turns OFF.
  4. If `now - start_ms >= PET_GAME_CAP_MS` in any phase, the game goes straight to OFF.
- **RNG:** xorshift32, as in Zappy; a seed of 0 becomes 1.

- [ ] **Step 1: Write the failing tests**
  - `test_side_of_position`: positions 0, 5, 12, 29 and 36–38 are LEFT; 6, 11, 18, 35 and 39–41 are RIGHT.
  - `test_correct_guess_scores`: after start, guessing `g.hide` gives REVEAL with `last_hit` true and `score == 1`.
  - `test_wrong_guess_no_score`: guessing the other side gives REVEAL with `score == 0`.
  - `test_second_guess_ignored`: a second guess while in REVEAL changes neither the score nor `phase_end_ms`.
  - `test_round_timeout_is_miss`: stepping to `start + PET_ROUND_MS` without a guess gives REVEAL with `last_hit` false.
  - `test_full_game_ends`: guess right every round and step through the reveals. After 5 rounds the phase is RESULT, `pet_game_won` is true and `pet_game_capturing` is false. Stepping `PET_RESULT_MS` later gives OFF.
  - `test_hard_cap`: start, then call `pet_game_step(g, start + PET_GAME_CAP_MS)` once while in ROUND. The phase becomes OFF.
  - `test_guess_when_off_ignored`: a guess on an OFF game stays OFF with `score == 0`.

- [ ] **Step 2: Run the tests to verify they fail**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: compile FAIL (`pet_game_start` undeclared).

- [ ] **Step 3: Implement** the mini-game in `pet_core.c`.

- [ ] **Step 4: Run the tests to verify they pass**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: PASS, exit 0.

- [ ] **Step 5: Commit**
  ```bash
  git add src/pet/pet_core.[ch] tests/pet/test_core.c
  git commit -m "Typing pet: left-or-right mini-game rules

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 4: Sprites (original Clackling art as text)

**Files:**
- Create: `prospector-zmk-module/src/pet/pet_sprites.h`, `prospector-zmk-module/src/pet/pet_sprites.c`
- Test: `prospector-zmk-module/tests/pet/test_sprites.c`. Add a second target to `tests/pet/Makefile`, so that `make test` builds and runs both.

**Interfaces:**
- Produces:
```c
#define PET_SPRITE_W 24
#define PET_SPRITE_H 24
/* palette chars: '.' transparent, '#' outline, 'o' body, '*' accent, 'w' eye/shine */
enum pet_pose { PET_POSE_IDLE, PET_POSE_BOUNCE, PET_POSE_SLEEP, PET_POSE_HUNGRY,
                PET_POSE_CHEER, PET_POSES };
/* PET_SPRITE_H strings of PET_SPRITE_W chars; Egg uses one art for all poses
 * except a wobble (BOUNCE) and a crack (CHEER = hatching) */
const char *const *pet_sprite(enum pet_stage stage, enum pet_pose pose);
```
- **The art is the builder's design, and must be original:**
  - Clackling is a small round critter with a keycap-shaped head bump.
  - Each stage adds features, for example ears, then a tail, then keycap "spines" along the back.
  - Draw it from scratch: no egg-and-device shapes or characters from existing pet games.
  - Five poses for each of the five stages = 25 sprites, which can share rows. That's about 25 × 24 × 24 = 14 KB of flash at most.

- [ ] **Step 1: Write the failing tests** in `test_sprites.c`:
  - `test_all_sprites_present`: `pet_sprite(s, p)` is non-NULL for every stage and pose.
  - `test_sprite_shape`: every sprite has exactly `PET_SPRITE_H` rows, each of `strlen == PET_SPRITE_W`, using only the chars `.#o*w`.
  - `test_sprite_not_blank`: every sprite has at least 20 non-`.` pixels.
  - `test_stages_differ`: the IDLE sprites of the five stages are all different (compare row by row).

- [ ] **Step 2: Run the tests to verify they fail**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: compile FAIL (`pet_sprites.h` not found).

- [ ] **Step 3: Draw the sprites** in `pet_sprites.c` as `static const char *const` row arrays, plus a lookup table.

- [ ] **Step 4: Run the tests to verify they pass, and preview the sprites**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/pet test`
  Expected: PASS.

  Then print each IDLE sprite to the terminal, using a throwaway loop in `test_sprites.c` guarded by `#ifdef PREVIEW`, and show them to the user. Adjust the art if they ask.

- [ ] **Step 5: Commit**
  ```bash
  git add src/pet/pet_sprites.[ch] tests/pet
  git commit -m "Typing pet: Clackling sprites for every stage and pose

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 5: Hide several layer names from the roller

**Files:**
- Modify:
  - `prospector-zmk-module/boards/shields/prospector_adapter/src/widgets/roller_names.c`
  - `roller_names.h`, in the same directory
  - `Kconfig`: `PROSPECTOR_ROLLER_HIDDEN_LAYER_NAME`
- Test: `prospector-zmk-module/tests/roller/test_roller.c`

**Interfaces:**
- `roller_build(..., const char *hidden, ...)` keeps its signature. `hidden` becomes a comma-separated list of names, for example `"Zappy,Pet"`. A single name still works, and NULL or `""` hides nothing.
- **Kconfig:** the help text says the value is a comma-separated list. The defaults become:
  - `"Zappy,Pet"` if both `PROSPECTOR_ZAPPY_BIRDS` and `PROSPECTOR_TYPING_PET` are on
  - `"Zappy"` if only Zappy is on
  - `"Pet"` if only the pet is on
  - `""` otherwise

- [ ] **Step 1: Write the failing tests**
  - `test_hidden_list`: with names {"A","Zappy","B","Pet"}, `roller_build(names, 4, 2, "Zappy,Pet", ...)` returns 1 and builds `"A\nB"`.
  - `test_hidden_list_active`: with active = 3 (Pet), it returns -1.
  - `test_hidden_no_prefix_match`: the hidden list `"Pet"` doesn't hide a layer named `"Petal"`, and `"Zappy,Pet"` doesn't hide `"Zap"`.

- [ ] **Step 2: Run the tests to verify they fail**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/roller test`
  Expected: an assertion FAIL in `test_hidden_list`.

- [ ] **Step 3: Implement** whole-name matching against each comma-separated entry, with no allocation.

- [ ] **Step 4: Run the tests to verify they pass**

  Run: `make -C ~/repos/zmk/prospector-zmk-module/tests/roller test`
  Expected: PASS, including the existing roller tests.

- [ ] **Step 5: Commit**
  ```bash
  git add boards/shields/prospector_adapter/src/widgets/roller_names.[ch] tests/roller Kconfig
  git commit -m "Layer roller: hide a comma-separated list of layer names

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 6: Pet panel, mode, store, behaviors and build wiring (fork)

**Files:**
- Create:
  - `src/pet/pet_panel.c`, `src/pet/pet_panel.h`
  - `src/pet/pet_mode.c`, `src/pet/pet_mode.h`
  - `src/pet/pet_store.c`
  - `src/behaviors/behavior_pet.c`
  - `dts/bindings/behaviors/zmk,behavior-pet.yaml`, `dts/bindings/behaviors/zmk,behavior-pet-key.yaml`
- Modify:
  - `Kconfig`: add `PROSPECTOR_TYPING_PET`, after `PROSPECTOR_ZAPPY_BIRDS`.
  - `CMakeLists.txt`: add a block like Zappy's.
  - `boards/shields/prospector_adapter/src/custom_status_screen.c`: create the pet panel at the WPM panel's size and position, then call `pet_mode_attach()`.
  - `src/zappy/zappy_mode.c` and `src/matrix/matrix_mode.c`: call the weak `pet_mode_end_game()` when they take over the screen.

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces (`pet_mode.h`):
```c
enum pet_cmd { PET_CMD_TOGGLE = 0, PET_CMD_FEED = 1, PET_CMD_PLAY = 2 };
void pet_mode_command(enum pet_cmd cmd, uint8_t capture_layer); /* behavior context */
void pet_mode_guess(uint8_t position);                          /* &petkey press */
bool pet_mode_shown(void);                  /* pet showing instead of the WPM panel */
/* display queue, from custom_status_screen.c: the two widgets to swap */
void pet_mode_attach(lv_obj_t *wpm_panel, lv_obj_t *pet_panel);
void pet_mode_end_game(void);               /* weak no-op elsewhere; ends game, frees layer */
void pet_mode_load(const struct pet_state *saved, bool shown);  /* settings, at boot */
void pet_store_save(const struct pet_state *p);                 /* weak no-op; pet_store.c */
void pet_store_save_shown(bool shown);                          /* weak no-op; pet_store.c */
```
- Produces (`pet_panel.h`):
```c
lv_obj_t *pet_panel_create(lv_obj_t *parent);   /* opaque THEME_BG box, hidden */
void pet_panel_render(const struct pet_view *v); /* snapshot struct from pet_mode */
```
  `struct pet_view` is defined in `pet_panel.h` and holds: stage, mood, fullness pips, keys, wpm, progress (0–100), next threshold, game phase, score, side, and a celebrate flag.
- **Behaviors:**
  - `zmk,behavior-pet` takes two parameters, `&pet CMD LAYER`; LAYER is used only by PLAY.
  - `zmk,behavior-pet-key` takes none; it calls `pet_mode_guess(event.position)`.
  - Both return `ZMK_BEHAVIOR_OPAQUE`, and are no-ops when `CONFIG_PROSPECTOR_TYPING_PET=n`.
  - Both are built whenever their devicetree nodes exist, as Zappy's are.
- **Threads:**
  - **Shared state:** `struct pet_state`, `struct pet_game` and `shown` sit behind a `k_mutex`.
  - **Key presses:** a `zmk_position_state_changed` listener (pressed only) adds to an atomic pending count. It skips presses while `zappy_mode_active()`, or while the mini-game is capturing.
  - **Life tick:** a `k_work_delayable` on the system work queue runs every 1 s while the pet is shown and every 60 s otherwise. Under the mutex it:
    - applies the pending presses with `pet_add_presses` (a stage-up forces a save)
    - calls `pet_elapse` with the uptime delta
    - calls `pet_store_save` when `pet_save_due` says so
  - **Swap:** a TOGGLE flips `shown`, saves it at once with `pet_store_save_shown` (the user pressed a key, so the write is rare), and submits a sync work item to `zmk_display_work_q()`. That work item:
    - hides one of the two attached widgets with `LV_OBJ_FLAG_HIDDEN` and shows the other
    - starts a 100 ms lv_timer while the pet is shown, and deletes it when it's hidden
    The WPM panel's own timer keeps running, so its graph history stays continuous.
  - **The 100 ms timer:**
    - snapshots the state under the mutex into a `struct pet_view`
    - computes the mood from `zmk_wpm_get_state()`, the idle time and the boost
    - steps the game
    - releases the capture layer when `pet_game_capturing` turns false
    - calls `pet_panel_render`
  - **Refusals:** FEED and PLAY are ignored while the pet isn't shown. PLAY is ignored while a game is running.
  - **Ending the game:**
    - The capture layer is activated on PLAY and deactivated as soon as the game stops capturing.
    - It's also deactivated on TOGGLE back to the WPM panel, and in `pet_mode_end_game()`.
    - Zappy calls `pet_mode_end_game()` in `show_game()`; the matrix rain calls it when the rain starts.
  - **Boost:** a won game sets `boost_until = now + PET_BOOST_MS`.
  - **Idle time:** measured from the last key press seen by the listener (any press, counted or not).
  - **Boot:** if the saved `shown` is true, the sync runs once `pet_mode_attach` has been called.
- **Panel** (`pet_panel.c`, 204×64):
  - Every object is created once and later only moved, re-texted or hidden.
  - The background is opaque `THEME_BG`, because the layer roller sits underneath, as it does for the WPM panel.
  - **Pet image:** a 24×24 `lv_canvas` with an RGB565 buffer (about 1.2 KB of RAM), filled from `pet_sprite()` with Nord colours (`#` NORD3 outline, `o` NORD8, `*` NORD13, `w` NORD6, `.` transparent, which shows `THEME_BG`). It's shown at 2× (48 px) with `lv_img_set_zoom(…, 512)` and antialiasing off, at the panel's left, vertically centred.
  - **Fallback**, if zoom isn't crisp on hardware: a 48×48 canvas drawn in 2×2 blocks (4.6 KB of RAM). Record which one was used as a Ruling.
  - **Right side, three rows:**
    - stage name, hearts (mood level 0–4) and four fullness pips
    - `NN WPM · 12,480 keys`
    - an `lv_bar` towards the next stage, then `next 100k`, or `max` at Adult
  - **Mini-game:** the right side reads "left or right?" with the score. On reveal, the pet canvas moves to the left or right edge of the panel; at RESULT it moves back with the final score.
  - **Celebration:** when `shown_stage < pet_stage_of(growth_half)`, play the CHEER pose for about 3 s, then set `shown_stage`.
- **Store** (`pet_store.c`):
  - Keys `pet/state` (the `pet_pack` blob) and `pet/shown` (1 byte), saved from the system work queue like `zappy_store.c`.
  - The settings handler checks lengths, calls `pet_unpack`, then `pet_mode_load`.
- **Kconfig:** `PROSPECTOR_TYPING_PET`: bool, default n, `depends on SHIELD_PROSPECTOR_ADAPTER && PROSPECTOR_WPM_PANEL && ZMK_WPM`, `select LV_USE_CANVAS`, `select LV_USE_LABEL`, `select LV_USE_BAR`.

- [ ] **Step 1: Write the files above.** Add the fork tests to the verification: `make -C tests/pet test`, `make -C tests/roller test` and `make -C tests/zappy test`.

- [ ] **Step 2: Build the dongle with the pet on**, before the keymap uses it, to check that it compiles and links. Run from `~/repos/zmk/zmk-config`:
  `scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_TYPING_PET=y > "$WS_LOG" 2>&1; tail -30 "$WS_LOG"` (with `WS_LOG` set to a file in the scratchpad)
  Expected: the build succeeds, and the memory report shows RAM under 75%.

- [ ] **Step 3: Build with the pet off**, as a regression check:
  `scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_TYPING_PET=n`
  Expected: the build succeeds, with flash and RAM within 1 KB of today's (487.6 KB flash and 176.2 KB RAM).

- [ ] **Step 4: Run the host tests**
  `for t in pet roller zappy; do make -C ~/repos/zmk/prospector-zmk-module/tests/$t test || break; done`
  Expected: all PASS.

- [ ] **Step 5: Commit** (fork)
  ```bash
  git add src/pet src/behaviors/behavior_pet.c dts/bindings/behaviors/zmk,behavior-pet*.yaml Kconfig CMakeLists.txt boards/shields/prospector_adapter/src/custom_status_screen.c src/matrix/matrix_mode.c src/zappy/zappy_mode.c
  git commit -m "Typing pet: panel in place of the WPM panel, life tick, saving and behaviors

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 7: Keymap, dongle config, README, hardware check (zmk-config)

**Files:**
- Modify:
  - `config/behaviors.dtsi`: add `pet: typing_pet` (`zmk,behavior-pet`, `#binding-cells = <2>`) and `petkey: typing_pet_key` (`zmk,behavior-pet-key`, `#binding-cells = <0>`), with `#define PET_TOGGLE 0`, `PET_FEED 1` and `PET_PLAY 2`.
  - `config/corne.keymap`:
    - add `#define PET 6`
    - bind func_layer positions 32, 33 and 34 to `&pet PET_TOGGLE 0`, `&pet PET_FEED 0` and `&pet PET_PLAY PET`
    - add a `pet_layer` with `display-name = "Pet"` and all 42 keys `&petkey`
  - `config/corne_dongle.conf`: add `CONFIG_PROSPECTOR_TYPING_PET=y`, with a one-line comment.
  - `README.md`:
    - three rows in the Function-layer table
    - a "Clackling" bullet in the Prospector section
    - `src/pet` and `tests/pet` mentions where the layout table lists fork paths. The table only lists this repo's files, so mention the pet in the Prospector bullet only.
- **Roller:** Task 5 already hides "Pet" through the Kconfig default, so there's nothing to set here.

- [ ] **Step 1: Make the edits above.** Check that positions 32–34 on func_layer are currently `___`, and that no binding touches positions 0, 11, 12, 23, 24 or 35.

- [ ] **Step 2: Build the dongle**
  `scripts/build-local.sh dongle`
  Expected: the build succeeds, and RAM stays under 75%.

- [ ] **Step 3: Regenerate the keymap diagram** if `scripts/draw-keymap.sh` works offline. Otherwise leave it to CI, as the README says.

- [ ] **Step 4: Commit both repos.** Push nothing.
  ```bash
  git add config/behaviors.dtsi config/corne.keymap config/corne_dongle.conf README.md
  git commit -m "Dongle: Clackling typing pet on Function + , . /

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

- [ ] **Step 5: Hardware check.** Flash only after the user says the dongle is in bootloader mode. The uf2 is at `~/.cache/zmk-zappy-ws/out/<target>/zephyr/zmk.uf2`, copied to the `XIAO-SENSE` drive. Then ask the user to check:
  1. Function + , swaps the WPM panel for the pet and back. Nothing else on the status screen moves, and typing still reaches the computer.
  2. Its mood follows typing speed, and it falls asleep after 2 minutes. The matrix rain still starts, and hands back to the pet.
  3. Function + . refills its fullness.
  4. During Function + /, guesses don't reach the computer, the layer roller doesn't show "Pet", and typing returns after the result.
  5. Zappy Birds started from the pet returns to the status screen with the pet still in place.
  6. After replugging the dongle, the pet is still the one showing, and the key count is kept once 15 minutes have passed since the last save. A stage-up saves at once.
