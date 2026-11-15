# Corne on Glass: Hardware-Free Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The parts of the screen-keyboard program that need no Yoga Book:
- read `config/corne.keymap` into a model
- a deterministic ZMK-compatible behavior engine that includes the resting rule
- touch-to-key/pad logic against the to-scale layout
- a CLI that checks a keymap and replays recorded touch traces

**Architecture:** A Rust workspace in `corne-glass/` with four crates:
- `keymap`: runs the system C preprocessor with vendored ZMK v0.3 headers, parses the devicetree subset, and builds a typed model.
- `engine`: a pure state machine. Key press/release and time go in, HID outputs come out.
- `touch`: layout geometry, contact filtering and pad gestures. It produces key events and pointer outputs.
- `cli`: the `corne-glass check|replay` binary.

There's no I/O in `engine` or `touch`, and every timer is driven by explicit `tick(t)` calls, so all behavior can be tested from scripted events. The X11 window, the uinput devices, the evdev grab, keymap reload and Corne detection come in a later, hardware plan that consumes these crates.

**Tech Stack:** Rust stable (edition 2021), `thiserror`, `serde`/`serde_json`, `clap` (derive). System `cpp` (GCC). No other runtime dependencies.

**Spec:** `docs/superpowers/specs/2026-11-14-yoga-book-corne-keyboard-design.md` (Phase 3; the hardware-free part)

## Global Constraints

- The source of truth is `config/corne.keymap` in this repo, which is plain ZMK devicetree, pinned by `config/west.yml` to **ZMK v0.3**.
- Supported behaviors: everything the keymap uses, plus `&kp &mt &lt &mo &tog &to &sk &sl &trans &none &caps_word &key_repeat &mmv &msc &mkp &aim &bt &sys_reset &bootloader`, and custom `zmk,behavior-hold-tap`, `-tap-dance`, `-mod-morph` and `-macro` nodes, combos (`zmk,combos`) and `zmk,conditional-layers`. **Anything else is `Error::Unsupported { what, file, line }`**, never a guess.
- ZMK v0.3 values that matter:
  - `global-quick-tap` means `require-prior-idle-ms = quick-tap-ms`.
  - "Prior idle" is measured from the last **non-modifier** key press.
  - `&mt` is hold-preferred, 200 ms. `&lt` is tap-preferred, 200 ms.
  - `&sk` and `&sl`: `release-after-ms` 1000, `&sk` has `ignore-modifiers`, and `&sl` has `quick-release`.
  - `&caps_word` continue-list: `UNDERSCORE BACKSPACE DELETE` (letters are shifted; letters and numbers continue).
  - `&key_repeat` usage pages: keyboard only.
  - `&mmv`: time-to-max 300 ms, exponent 1. `&msc`: time-to-max 300 ms, exponent 0. Delay 0.
- The keymap overrides `ZMK_POINTING_DEFAULT_MOVE_VAL` to 1500 and `..._SCRL_VAL` to 20. The engine emits raw ZMK units, and scaling to this screen is the hardware plan's job.
- Resting rule (`Tuning`):
  - `rest_window_ms = 500`
  - `rest_exempt = positions 36..=41` (thumbs)
  - It applies only to hold-taps at non-exempt positions.
  - Thumbs behave exactly like ZMK.
- Firmware-only behaviors (`&bt`, `&sys_reset`, `&bootloader`) parse fine and do nothing.
- Anti-idle: `&aim AIM_TOG` toggles it, and it's off at start. While it's on, every `interval-ms` (30000) the engine emits `Pointer{dx:0,dy:1}`, then `Pointer{dx:0,dy:-1}` after `tap_ms + wait_ms` (400).
- Layout: the bottom screen is 286.4 × 179.0 mm, the hinge is at y = 0, the margin is 12 mm, the pitch 18 mm, and key caps are 16.5 mm.
- Touch defaults: device units are normalized by the device's `x_max`/`y_max` to the screen in mm.

## Review Focus

- **A key released after the layer changed** (press a key on LOW, release LOW, then release the key) must release the binding that was *pressed*, not the current layer's. Test in Task 5.
- **Two homerow mods held together** (A then F then J) must give Ctrl+Shift+J, not Ctrl+F+J. Test in Task 6.
- **Release without a press**: a stray release, or a contact reclassified as a palm, must not panic or produce output. Test in Task 5 (engine) and Task 13 (touch).
- **Mode switch while keys are held**: `release_all` must release every held usage and clear momentary layers. Otherwise you get a stuck Ctrl when the Corne takes over. Test in Task 5.
- **A contact that slides off its key or into a pad** keeps its original key and never moves the pointer. Test in Task 13.

---

## File structure

```
corne-glass/
  Cargo.toml                     # workspace: crates/*
  vendor/zmk-v0.3/include/dt-bindings/zmk/{keys,hid_usage,hid_usage_pages,modifiers,bt,pointing}.h
  vendor/zmk-anti-idle/include/dt-bindings/anti_idle_mode.h
  vendor/stubs/behaviors.dtsi                       # empty
  vendor/stubs/behaviors/anti_idle_mode.dtsi        # empty
  vendor/stubs/zephyr/dt-bindings/dt-util.h         # #define BIT(n) (1 << (n))
  vendor/README.md                                  # source URLs, tag/commit SHAs, MIT notices
  crates/keymap/src/{lib,preprocess,dts,expr,model,build,keycodes,legend}.rs
  crates/keymap/tests/{fixtures/*.keymap, *.rs}
  crates/engine/src/{lib,types,layers,hold_tap,tap_dance,mod_morph,sticky,caps_word,combos,macros,mouse,anti_idle}.rs
  crates/engine/tests/*.rs
  crates/touch/src/{lib,layout,filter,gestures,trace}.rs
  crates/touch/tests/*.rs
  crates/cli/src/main.rs
```

Position numbering is the keymap's: 42 per layer, row-major. Row 0 is 0–11, row 1 is 12–23, row 2 is 24–35, and the thumbs are 36–41. The outer columns (0, 11, 12, 23, 24, 35) exist in the model but aren't drawn.

---

### Task 1: Workspace, vendored headers, preprocessing

**Files:**
- Create: `corne-glass/Cargo.toml`, `corne-glass/.gitignore` (`target/`), `corne-glass/vendor/**` as listed above, and `crates/keymap/Cargo.toml`
- Create: `crates/keymap/src/lib.rs`, `crates/keymap/src/preprocess.rs`
- Test: `crates/keymap/tests/preprocess.rs`

Vendoring:
- Copy the ZMK files from `zmkfirmware/zmk` tag `v0.3`, `app/include/dt-bindings/zmk/`.
- Copy `anti_idle_mode.h` from `kbrd-space/zmk-anti-idle` at the current `main` commit, and record that SHA in `vendor/README.md`.

**Interfaces:**
- Produces: `pub fn preprocess(keymap: &Path) -> Result<Preprocessed, Error>`.
  - It runs `cpp -nostdinc -undef -x assembler-with-cpp -I <vendor/stubs> -I <vendor/zmk-v0.3/include> -I <vendor/zmk-anti-idle/include> <keymap>`.
  - The vendor root is found at runtime via the `CORNE_GLASS_VENDOR` env var, falling back to a path baked in at compile time (`env!("CARGO_MANIFEST_DIR")/../../vendor`).
- Produces: `pub struct Preprocessed { pub text: String }` and `impl Preprocessed { pub fn origin(&self, out_line: usize) -> (PathBuf, usize) }`, which uses cpp linemarkers (`# <n> "<file>"`) to map an output line back to its source file and line.
- Produces: `pub enum Error { Cpp { stderr: String }, Parse { file: PathBuf, line: usize, msg: String }, Unsupported { what: String, file: PathBuf, line: usize }, Invalid { msg: String } }` (thiserror). It's shared by every later keymap task.

- [ ] **Step 1: Write the failing tests**
  - `preprocesses_real_keymap`: `preprocess("../../../config/corne.keymap")` is Ok. The text contains `hrm` and `&kp`, and no `#include`.
  - `origin_maps_back`: for the output line containing `tapping-term-ms = <280>`, `origin` returns a path ending in `config/behaviors.dtsi` and line 7.
  - `cpp_error_surfaces`: a temp keymap with `#include "missing.h"` gives `Err(Error::Cpp{stderr})`, and stderr contains `missing.h`.
- [ ] **Step 2: Run and confirm they fail.** `cargo test -p keymap --test preprocess` fails to compile (no `preprocess`).
- [ ] **Step 3: Implement it** with `std::process::Command`. A non-zero exit becomes `Error::Cpp`.
- [ ] **Step 4: Run and confirm they pass.** `cargo test -p keymap --test preprocess` gives 3 passed.
- [ ] **Step 5: Commit.** `git add corne-glass && git commit -m "corne-glass: workspace, vendored ZMK headers, preprocessing"`

### Task 2: Devicetree subset parser and expression evaluator

**Files:**
- Create: `crates/keymap/src/expr.rs`, `crates/keymap/src/dts.rs`
- Test: `crates/keymap/tests/dts.rs`

**Interfaces:**
- Produces: `pub fn eval(expr: &str) -> Result<i64, String>`. It handles C integer literals (decimal, hex), parentheses, unary `- ~`, binary `* / + - << >> & ^ |` with C precedence, and `U`/`UL` suffixes, which are ignored.
- Produces: `pub fn parse(src: &Preprocessed) -> Result<Node, Error>` and `pub struct Node { pub name: String, pub label: Option<String>, pub props: Vec<Prop>, pub children: Vec<Node>, pub line: usize }`.
  - `pub struct Prop { pub name: String, pub value: Value, pub line: usize }`
  - `pub enum Value { Bool, Str(Vec<String>), Cells(Vec<Cell>) }`
  - `pub enum Cell { Num(i64), Ref(String) }`
  - Multiple `/ { }` roots merge into one tree.
  - Linemarker lines are consumed (they set the line mapping), and `//` and `/* */` comments are skipped.
  - `/omit-if-no-ref/` and `#binding-cells` are accepted.
- Produces: `pub fn split_bindings(cells: &[Cell]) -> Vec<(String, Vec<i64>)>`, which splits a phandle-array at each `Ref`, e.g. `<&hrm 0x7e0 0x70004 &kp 5>` gives `[("hrm",[..]),("kp",[5])]`.

- [ ] **Step 1: Write the failing tests**
  - `eval("(((0x02 << 24) | ((0x07 << 16) | 0x1E)))") == Ok(0x0207001E)`
  - `eval("((-1500) & 0xFFFF)") == Ok(0xFA24)`
  - `eval("(1 << (2))") == Ok(4)`
  - `eval("1 +") ` is Err.
  - Parse `"/ { a: n { x = <1 (2+3) &kp 4>; s = \"hi\"; b; c { }; }; };"`. The node `n` has label `a`. `x` is `Cells([Num(1),Num(5),Ref("kp"),Num(4)])`, `s` is `Str(["hi"])`, `b` is `Bool`, and it has one child `c`.
  - Two `/ {}` roots merge their children.
  - A missing `;` gives `Error::Parse` with the original file and line, via `origin`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it** with a hand-written tokenizer and a recursive descent parser. Expressions inside `< >` are parenthesized groups, or a bare literal, passed to `eval`. **Step 4: Run and confirm they pass.** `cargo test -p keymap --test dts`
- [ ] **Step 5: Commit.** `git commit -am "corne-glass: devicetree subset parser"` (after `git add`)

### Task 3: Keymap model and validation

**Files:**
- Create: `crates/keymap/src/model.rs`, `crates/keymap/src/build.rs`
- Test: `crates/keymap/tests/model.rs`, `crates/keymap/tests/fixtures/corne.keymap` (a copy of today's `config/corne.keymap`) and `fixtures/behaviors.dtsi` (a copy), `fixtures/unknown.keymap`, `fixtures/combos.keymap`

**Interfaces:**
- Produces: `pub fn load(path: &Path) -> Result<Keymap, Error>`, which is preprocess + parse + build.
- Produces the model types:

```rust
pub struct Keymap { pub layers: Vec<Layer>, pub behaviors: BTreeMap<String, BehaviorDef>,
  pub combos: Vec<Combo>, pub conditional_layers: Vec<CondLayer>, pub anti_idle: Option<AntiIdle> }
pub struct Layer { pub node_name: String, pub display_name: Option<String>, pub bindings: Vec<Binding> }
pub struct Binding { pub behavior: String, pub params: Vec<u32> }   // behavior = label without '&'
pub enum BehaviorDef {
  HoldTap { flavor: Flavor, tapping_term_ms: u32, quick_tap_ms: Option<u32>, require_prior_idle_ms: Option<u32>, bindings: [String; 2] },
  TapDance { tapping_term_ms: u32, bindings: Vec<Binding> },
  ModMorph { bindings: [Binding; 2], mods: u8, keep_mods: u8 },
  Macro { bindings: Vec<Binding>, wait_ms: u32, tap_ms: u32 } }
pub enum Flavor { HoldPreferred, Balanced, TapPreferred, TapUnlessInterrupted }
pub struct Combo { pub positions: Vec<u8>, pub binding: Binding, pub timeout_ms: u32, pub layers: Option<Vec<u8>>, pub require_prior_idle_ms: Option<u32> }
pub struct CondLayer { pub if_layers: Vec<u8>, pub then_layer: u8 }
pub struct AntiIdle { pub interval_ms: u32, pub tap_ms: u32, pub wait_ms: u32 }
impl Keymap { pub fn validate_corne(&self) -> Result<(), Error> }  // every layer has exactly 42 bindings
```

- Builtins (`kp mt lt mo tog to sk sl trans none caps_word key_repeat mmv msc mkp aim bt sys_reset bootloader`) are known by name and are *not* in `behaviors` (except `mt` and `lt`, which are inserted as HoldTap with the Global Constraints values).
- Any other `&label` must resolve to a node with a supported `compatible`. Otherwise you get `Unsupported`, with the file and line of the binding's property.
- The ZMK defaults when a property is absent: combo `timeout-ms` 50, tap-dance `tapping-term-ms` 200, hold-tap flavor `hold-preferred`, macro `wait-ms` 15 and `tap-ms` 30.

- [ ] **Step 1: Write the failing tests** (against `fixtures/corne.keymap`)
  - `layers.len() == 5`, and the node names are `["default_layer","lower_layer","raise_layer","func_layer","mouse_layer"]`
  - Every layer has 42 bindings, so `validate_corne()` is Ok.
  - `layers[0].bindings[13] == Binding{behavior:"hrm", params:vec![0x000700E0, 0x00070004]}` (LCTRL, A)
  - `layers[1].bindings[1] == Binding{behavior:"kp", params:vec![0x0207001E]}` (EXCL = LS(N1))
  - `layers[4].bindings[2] == Binding{behavior:"mmv", params:vec![0xFA24]}` (MOVE_UP with 1500)
  - `layers[3].bindings[25] == Binding{behavior:"bt", params:vec![1, 0]}` (BT_SEL 0)
  - `layers[3].bindings[20] == Binding{behavior:"aim", params:vec![2]}`
  - `behaviors["hrm"] == HoldTap{flavor:Balanced, tapping_term_ms:280, quick_tap_ms:Some(125), require_prior_idle_ms:Some(125), bindings:["kp","kp"]}`
  - `behaviors["td0"]` is a TapDance with 200 ms and bindings `[mo 1, mo 4, tog 4]`.
  - `conditional_layers == [CondLayer{if_layers:vec![1,2], then_layer:3}]`
  - `anti_idle == Some(AntiIdle{interval_ms:30000, tap_ms:200, wait_ms:200})`
  - `combos.is_empty()`
  - `fixtures/unknown.keymap` (one layer binding `&foo`) gives `Err(Unsupported{what:"foo", line, file})`, where the file ends with `unknown.keymap` and the line is the binding's line.
  - `fixtures/combos.keymap` (a 4-key layer, one combo on positions 0 and 1 with `&kp ESC` and timeout 40, `layers = <0>`) parses that combo exactly.
  - `live_config_parses`: `load("../../../config/corne.keymap")` is Ok and passes `validate_corne()`. This catches drift between the fixture and the real file without pinning its contents.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement `build.rs`.** Layers come from the `zmk,keymap` node's children in order. Conditional layers come from `zmk,conditional-layers` children, the anti-idle from `zmk,anti-idle-generic`, and `global-quick-tap` maps to `require_prior_idle_ms = quick_tap_ms`. **Step 4: Run and confirm they pass.** `cargo test -p keymap`
- [ ] **Step 5: Commit.** `git commit -m "corne-glass: typed keymap model with validation"`

### Task 4: Keycode names and legends

**Files:**
- Create: `crates/keymap/src/keycodes.rs`, `crates/keymap/src/legend.rs`
- Test: `crates/keymap/tests/legend.rs`

**Interfaces:**
- Produces: `pub fn decode(param: u32) -> (u8 /*implicit mods*/, u8 /*page*/, u16 /*id*/)`, which splits on ZMK's `mods<<24 | page<<16 | id`.
- Produces: `pub fn name(page: u8, id: u16) -> &'static str`. These are the binding names that engine tests rely on: `A`–`Z`, `N0`–`N9`, `F1`–`F12`, `ESC TAB SPACE BSPC RET DEL CAPS MINUS EQUAL LBKT RBKT BSLH SEMI SQT GRAVE COMMA DOT FSLH LEFT RIGHT UP DOWN`, and the modifiers `LCTRL LSHFT LALT LGUI RCTRL RSHFT RALT RGUI` (0xE0–0xE7). Anything unknown is `"?"`.
- Produces: `pub struct Legend { pub tap: String, pub hold: Option<String>, pub transparent: bool }` and `pub fn legend(km: &Keymap, layer: usize, pos: usize) -> Legend`.
  - Symbols: shifted ones show the character (`EXCL` → `!`, `UNDER` → `_`), and letters are uppercase.
  - A homerow hold-tap gets `hold` = `ctl|sft|alt|gui`.
  - `&mo`, `&tog` and `&to` show the layer node name with `_layer` stripped (`raise`).
  - `&trans` resolves to the next lower layer's legend with `transparent: true`.
  - Firmware-only behaviors and `&none` show `""`.
  - `&mmv` shows an arrow, and `&mkp` shows `L`/`M`/`R`.

- [ ] **Step 1: Write the failing tests**
  - `legend(km,0,13) == Legend{tap:"A", hold:Some("ctl"), transparent:false}`
  - `legend(km,1,1).tap == "!"`
  - `legend(km,0,40).tap == "raise"`
  - `legend(km,1,13) == Legend{tap:"Esc", hold:Some("ctl"), transparent:false}`. Display strings for named keys are title-case short names: Esc, Tab, Spc, Bspc, Ret, Del, Ctrl, Shft, Alt, Gui, Caps.
  - `legend(km,2,0)` is transparent with tap `""` (it resolves to the outer column).
  - `legend(km,2,36) == Legend{tap:"Ctrl", hold:None, transparent:true}` (it falls through to base LCTRL).
  - `legend(km,3,25).tap == ""` (bt)
  - `decode(0x0207001E) == (0x02, 0x07, 0x1E)`
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it** with a hand-written table. **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: keycode names and legends"`

### Task 5: Engine core: types, layers, `&kp`, `&mo`, `&tog`, `&to`, `&trans`, `&none`, conditional layers

**Files:**
- Create: `crates/engine/Cargo.toml` (it depends on `keymap`), and `crates/engine/src/{lib,types,layers}.rs`
- Create: `crates/engine/tests/common/mod.rs` (helpers) and `crates/engine/tests/core.rs`

**Interfaces:**
- Consumes: `keymap::{Keymap, Binding, BehaviorDef, decode, name}`.
- Produces:

```rust
pub type Ms = u64;
#[derive(Clone, Copy, PartialEq, Eq, Debug)] pub struct KeyPos(pub u8);
#[derive(Clone, PartialEq, Debug)] pub enum Output {
  Key { page: u8, id: u16, down: bool },            // implicit mods expanded into separate 0xE0..0xE7 Key outputs
  Pointer { dx: i32, dy: i32 }, Scroll { dx: i32, dy: i32 }, Button { mask: u8, down: bool },
  Layers(u32),                                      // bitmask, emitted whenever active layers change
  ModActive { pos: KeyPos, active: bool },          // hold-tap resolved to hold / released
  AntiIdle(bool) }
pub struct Tuning { pub rest_window_ms: u32, pub rest_exempt: Vec<KeyPos> }
impl Default for Tuning { /* 500, 36..=41 */ }
pub struct Engine { .. }
impl Engine {
  pub fn new(keymap: keymap::Keymap, tuning: Tuning) -> Engine;
  pub fn press(&mut self, pos: KeyPos, t: Ms) -> Vec<Output>;   // calls tick(t) first
  pub fn release(&mut self, pos: KeyPos, t: Ms) -> Vec<Output>; // calls tick(t) first; unknown pos -> no output
  pub fn tick(&mut self, t: Ms) -> Vec<Output>;                 // fires every timer due at <= t, in time order
  pub fn next_deadline(&self) -> Option<Ms>;
  pub fn active_layers(&self) -> u32;
  pub fn release_all(&mut self, t: Ms) -> Vec<Output>;          // releases every held usage/button, clears momentary + sticky layers, keeps toggles
}
```

- The test helper is `fn keys(outs: &[Output]) -> Vec<String>`, which maps `Key` outputs to `"+NAME"`/`"-NAME"` via `keymap::name` and drops the rest. Another helper, `fn run(km, &[(Ms, char /*'p'|'r'|'t'*/, u8)]) -> Vec<Output>`, drives an engine from a script. Tests use `fixtures/corne.keymap` from the keymap crate.
- Bindings resolve **at press time**. The press records `(pos → resolved binding)`, and release uses that record.
- Whenever active layers change, conditional layers are recomputed.

- [ ] **Step 1: Write the failing tests**
  - `plain_key`: press 1 at 0, release 1 at 50 gives `["+Q","-Q"]`.
  - `implicit_mods`: hold 40 (`mo RAS`) at 0, tap 13 at 10/20 (TAB on raise) gives `["+TAB","-TAB"]`. Then hold 37 until LOW is active at 250, tap 1 at 300/320 gives `["+LSHFT","+N1","-N1","-LSHFT"]`.
  - `trans_falls_through`: hold 40, tap 38 (`___` on raise) gives `["+SPACE","-SPACE"]`.
  - `conditional_fun`: hold 37 (LOW active at 200) and hold 40 at 300. `active_layers() == 0b1111`, and tapping 1 gives `["+F1","-F1"]`.
  - `release_after_layer_change` (Review Focus): hold 37 to 250, press 25 at 300 (CAPS), release 37 at 400, release 25 at 500 gives `["+CAPS","-CAPS"]`.
  - `stray_release_is_ignored`: `release(KeyPos(1), 10)` on a fresh engine is an empty vec.
  - `release_all_unsticks`: press 36 at 0 (LCTRL), then `release_all(10)` gives `["-LCTRL"]` and `active_layers() == 1`.
  - `layers_output`: after `mo RAS` is pressed, the outputs contain `Layers(0b101)`.
- [ ] **Step 2: Run and confirm they fail.** `cargo test -p engine --test core`
- [ ] **Step 3: Implement it.** `td0` is needed by these tests. Implement the TapDance *hold* path minimally here (a single tap-hold activates binding 0 once the tapping term expires while held), with full tap-dance in Task 8. `hrm` isn't exercised in this task.
- [ ] **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: engine core and layers"`

### Task 6: Hold-tap (all flavors, quick-tap, prior-idle), `&mt`, `&lt`

**Files:**
- Create: `crates/engine/src/hold_tap.rs`
- Test: `crates/engine/tests/hold_tap.rs`

**Interfaces:**
- Consumes: `Engine` internals from Task 5.
- Hold-tap follows ZMK v0.3 `behavior_hold_tap.c`:
  - Events from later keys are captured while it's undecided, and replayed in order after the decision.
  - Decision moments are key-up, other-key-down, other-key-up, timer and quick-tap.
  - **balanced**: key-up → tap, other-key-up → hold, timer → hold.
  - **hold-preferred**: other-key-down → hold.
  - **tap-preferred**: only the timer → hold.
  - **tap-unless-interrupted**: other-key-down → hold, timer → tap.
  - Quick-tap: if the press is within `require_prior_idle_ms` of the last non-modifier key press, or within `quick_tap_ms` of this same position's last tap, it's an immediate tap.
  - A hold emits `ModActive{pos,true}` with the mod. A release emits `ModActive{pos,false}`.

Tests use `Tuning { rest_window_ms: 10_000, .. }` so the resting rule (Task 7) can't interfere.

- [ ] **Step 1: Write the failing tests**
  - `tap`: press 16 at 0, release at 100 gives `["+F","-F"]`.
  - `balanced_hold_by_other_key_up`: press 16 at 0, press 19 at 150, release 19 at 220, release 16 at 300 gives `["+LSHFT","+J","-J","-LSHFT"]`.
  - `balanced_rolling_is_tap`: press 16 at 0, press 19 at 50, release 16 at 80, release 19 at 120 gives exactly `["+F","-F","+J","-J"]`. F decides tap on its own key-up, then the captured J press replays as an undecided hold-tap, which taps on its release.
  - `timer_hold`: press 16 at 0, tick 300, release 400 gives `["+LSHFT","-LSHFT"]`.
  - `two_mods` (Review Focus): press 13 at 0, 16 at 50, 19 at 100, then release 19 at 150, 16 at 200, 13 at 250. That gives `["+LCTRL","+LSHFT","+J","-J","-LSHFT","-LCTRL"]`.
  - `prior_idle_forces_tap`: tap 1 at 0/30 (Q), then press 16 at 100 (< 125 ms after Q's press), press 19 at 120, release 19 at 150, release 16 at 170. That gives `["+Q","-Q","+F","+J","-J","-F"]`.
  - `mt_hold_preferred`: with an inline fixture (`&mt LSHFT A` at position 0, `&kp B` at position 1), press 0 at 0, press 1 at 10, release 1 at 20, release 0 at 30. That gives `["+LSHFT","+B","-B","-LSHFT"]`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: ZMK hold-tap semantics"`

### Task 7: The resting rule

**Files:**
- Modify: `crates/engine/src/hold_tap.rs`
- Test: `crates/engine/tests/resting.rs`

This applies to a hold-tap at a position **not** in `tuning.rest_exempt`, with `t0` its press time. The states it adds:
- **Undecided → PendingHold**: when the hold-tap's *timer* would decide hold before `t0 + rest_window_ms`, don't emit. Enter PendingHold instead.
- **Another key pressed at `t1 ≤ t0 + rest_window_ms`**: in PendingHold, emit the hold (`+mod`, `ModActive`) and then process the other key. If still Undecided, continue normal ZMK logic.
- **Reaching `t0 + rest_window_ms`** (by tick or by another key press with `t1 >` that time) while Undecided or PendingHold → **Resting**. Captured events are replayed as ordinary presses, and the resting key itself emits nothing, ever.
- **Release in PendingHold** gives the ZMK hold result: `+mod` then `-mod`. **Release while Resting** gives nothing.

- [ ] **Step 1: Write the failing tests** (default `Tuning`)
  - `quick_tap_still_types`: press 16 at 0, release at 100 gives `["+F","-F"]`.
  - `deliberate_mod_within_window`: press 16 at 0, press 19 at 450, release 19 at 480, release 16 at 600 gives `["+LSHFT","+J","-J","-LSHFT"]`.
  - `resting_then_type`: press 16 at 0, press 19 at 900, release 19 at 960, release 16 at 2000 gives `["+J","-J"]`.
  - `resting_lift_sends_nothing`: press 16 at 0, tick 1000, release at 3000 gives `[]`, and no `ModActive` appears.
  - `pending_hold_released`: press 16 at 0, release at 400 gives `["+LSHFT","-LSHFT"]`.
  - `thumbs_exempt`: press 36 at 0, tap 1 at 2000/2050, release 36 at 2100 gives `["+LCTRL","+Q","-Q","-LCTRL"]`.
  - `thumb_layer_long_hold`: press 40 at 0, tap 13 at 1500/1550 gives `["+TAB","-TAB"]`.
  - `resting_does_not_block_others`: press 13 at 0 and 16 at 10 (both rest by 510), then tap 19 at 900/950 gives `["+J","-J"]`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass**, together with Task 6's tests (`cargo test -p engine`). **Step 5: Commit.** `git commit -m "corne-glass: resting rule for homerow hold-taps"`

### Task 8: Tap-dance and mod-morph

**Files:**
- Create: `crates/engine/src/tap_dance.rs`, `crates/engine/src/mod_morph.rs`
- Test: `crates/engine/tests/tap_dance.rs`

Tap-dance semantics (ZMK):
- Each press within `tapping_term_ms` of the previous release increments the count.
- If it's still held when the term expires, press `bindings[count-1]` and hold it until release.
- If it's released and the term expires, tap `bindings[count-1]`.
- Another key's press decides it immediately, with the current count.

Mod-morph: if any mod in `mods` is held at press time, press `bindings[1]` with those mods masked off, except for `keep_mods`. Otherwise press `bindings[0]`.

- [ ] **Step 1: Write the failing tests**
  - `single_hold_low`: press 37 at 0 and tick 200, then `active_layers() & 0b10 != 0`.
  - `double_hold_mouse`: press 37 at 0, release at 50, press at 100, tick 300. Then `active_layers() & 0b10000 != 0`, and pressing 14 then ticking 400 yields at least one `Pointer{dy>0}`.
  - `triple_tap_toggles`: tap 37 at 0/30, 60/90 and 120/150, tick 400. MOS stays active after every key is released. Pressing 37 again (it's `&tog MOS` on the mouse layer) at 500, releasing at 520, turns MOS off.
  - `mod_morph`: with an inline fixture (a mod-morph of `&kp BSPC`/`&kp DEL`, mods shift, at position 0, and `&kp LSHFT` at position 1), press 1, tap 0, release 1. That gives exactly `["+LSHFT","-LSHFT","+DEL","-DEL","+LSHFT","-LSHFT"]`: shift is masked while DEL is down, restored after, then released.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: tap-dance and mod-morph"`

### Task 9: Sticky keys and layers, caps word, key repeat

**Files:**
- Create: `crates/engine/src/sticky.rs`, `crates/engine/src/caps_word.rs`
- Test: `crates/engine/tests/sticky.rs`

**Interfaces:** These use the Global Constraints values. Sticky: active until the next non-modifier key press is released (the `ignore-modifiers` rule), or `release-after-ms` (1000). `&sl` uses `quick-release`. Caps word: letters get shifted, letters, numbers and the continue-list continue, and anything else (or pressing caps word again) ends it. Key repeat re-sends the last keyboard-page usage, including its implicit mods.

- [ ] **Step 1: Write the failing tests**
  - `sticky_shift`: tap 41 at 0/20, then tap 1 at 100/150, gives `["+LSHFT","+Q","-Q","-LSHFT"]`.
  - `sticky_expires`: tap 41 at 0/20, tick 1100, then tap 1 at 1200/1250. That gives `["+LSHFT","-LSHFT","+Q","-Q"]`.
  - `caps_word`: an inline fixture with `&caps_word`, `&kp A`, `&kp MINUS`, `&kp SPACE`. Tapping caps_word, A, MINUS, A, SPACE, A gives exactly `["+LSHFT","+A","-A","-LSHFT","+MINUS","-MINUS","+A","-A","+SPACE","-SPACE","+A","-A"]`. MINUS isn't in the continue-list, so it ends caps word.
  - `key_repeat`: an inline fixture with `&kp LS(A)` and `&key_repeat`. Tap the first, then the second, gives `["+LSHFT","+A","-A","-LSHFT","+LSHFT","+A","-A","-LSHFT"]`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: sticky keys, caps word, key repeat"`

### Task 10: Combos and macros

**Files:**
- Create: `crates/engine/src/combos.rs`, `crates/engine/src/macros.rs`
- Test: `crates/engine/tests/combos.rs` (uses `keymap/tests/fixtures/combos.keymap` from Task 3, plus a macro fixture)

Combo semantics:
- A press of a combo position starts a candidate. It's held back until every position of some combo is pressed within `timeout_ms` of the first press, then that combo's binding is pressed.
- A press of a non-combo key, a release, or a timeout flushes the held-back presses as normal presses, in order, with their original timestamps.
- `layers` restricts which layers the combo is active on.
- The combo's binding is released when any of its positions is released.

Macros: run `bindings` in sequence using `&macro_tap`, `&macro_press`, `&macro_release`, `&macro_wait_time N` and `&macro_tap_time N`. Waits are emitted through timers, not inline.

- [ ] **Step 1: Write the failing tests**
  - `combo_fires`: press 0 at 0, press 1 at 20, release both at 60 gives `["+ESC","-ESC"]`.
  - `combo_timeout_flushes`: press 0 at 0, tick 50 (> 40), release at 60 gives the position 0 key's normal tap.
  - `combo_interrupted`: press 0 at 0, press 2 (non-combo) at 10 gives position 0's binding, then position 2's.
  - `macro_sequence`: a macro of `&macro_tap &kp A &kp B` gives `["+A","-A","+B","-B"]` after ticking past `2*tap_ms + wait_ms`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: combos and macros"`

### Task 11: Mouse keys and anti-idle

**Files:**
- Create: `crates/engine/src/mouse.rs`, `crates/engine/src/anti_idle.rs`
- Test: `crates/engine/tests/mouse.rs`

**Interfaces:**
- While any `&mmv` or `&msc` is held, `next_deadline()` returns a deadline every 10 ms.
- Velocity is `max * min(1, held_ms/300)^exponent` units/s, integrated per tick with fractional remainders carried forward (both axes).
- `&mkp` gives `Button{mask, down}`.
- `&aim` gives `AntiIdle(on)` plus the periodic nudges from the Global Constraints.

- [ ] **Step 1: Write the failing tests** (mouse layer active via a `tog`, i.e. triple-tap 37, or with an inline fixture holding `&mmv MOVE_DOWN` and `&msc SCRL_UP`)
  - `mmv_ramp`: press at 0 and tick to 300. The sum of `Pointer.dy` is 225 ± 1, the sum of dx is 0. Ticking on to 400 adds 150 ± 1.
  - `msc_constant`: press at 0 and tick to 1000 gives a `Scroll.dy` sum of 20 ± 1.
  - `release_stops`: after release, `next_deadline()` is None (unless anti-idle is on), and further ticks emit no `Pointer`.
  - `mkp`: layer 1 position 16 (`&mkp LCLK`) gives `Button{mask:1,down:true}` then `{mask:1,down:false}`.
  - `anti_idle`: tap func position 20 (`&aim AIM_TOG`, reached by holding 37 and 40) at t = 1000. That gives `AntiIdle(true)`. At 31000 you get `Pointer{0,1}`, and at 31400 `Pointer{0,-1}`. Tapping it again gives `AntiIdle(false)`, and nothing more at 61000.
  - `firmware_noops`: func position 25 (`&bt`) and 16 (`&sys_reset`) produce no outputs.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: mouse keys and anti-idle"`

### Task 12: Layout geometry and hit-testing

**Files:**
- Create: `crates/touch/Cargo.toml` (it depends on `engine` for `KeyPos` and `Ms`), and `crates/touch/src/{lib,layout}.rs`
- Test: `crates/touch/tests/layout.rs`

**Interfaces:**
- Produces: `pub struct Mm { pub x: f32, pub y: f32 }`, `pub struct Rect { pub x0: f32, pub y0: f32, pub x1: f32, pub y1: f32 }`, `pub struct KeyRect { pub pos: KeyPos, pub center: Mm }`, `pub enum Zone { Key(KeyPos), CenterPad, BottomPad, None }`.
- Produces: `pub struct Layout { pub keys: Vec<KeyRect>, pub center_pad: Rect, pub bottom_pad: Rect, pub key_size: f32 }`, with `impl Layout { pub fn corne_c() -> Layout; pub fn zone_at(&self, p: Mm) -> Zone; }`.
- Geometry (the to-scale option C mockup):
  - Constants: `W=286.4, H=179.0, MARGIN=12, PITCH=18, KEY=16.5`. Column stagger in units for the left half, columns 0–4: `[0.45, 0.15, 0.0, 0.12, 0.25]`. The right half mirrors it (column c uses `stagger[4-c]`).
  - Left half origin `(12, 12)`. Right half origin `(W-12-5*PITCH, 12) = (184.4, 12)`.
  - Key center: `(x0 + c*18 + 9, y0 + (r + stagger)*18 + 9)`.
  - Thumbs at `y = 12 + 3.55*18 + 9 = 84.9`. Left thumbs (36, 37, 38) at column offsets `2.4, 3.5, 4.6`, right thumbs (39, 40, 41) at `-0.6, 0.5, 1.6`, relative to their half's origin.
  - The hit box is the 18 × 18 cell centered on each key. Gaps resolve to the nearest key within its cell, otherwise to a pad or None.
  - `center_pad = Rect{131.2, 12, 155.2, 96.8}` and `bottom_pad = Rect{12, 100.8, 274.4, 167.0}`.
  - Only the 36 drawn positions exist (the outer columns are absent).

- [ ] **Step 1: Write the failing tests**
  - `keys.len() == 36`
  - The center of position 13 is (39.0, 41.7), position 1 is (21.0, 29.1), position 10 is (265.4, 29.1), position 36 is (64.2, 84.9), and position 40 is (202.4, 84.9). All within 0.01.
  - `zone_at(Mm{39.0,41.7}) == Key(KeyPos(13))`
  - `zone_at(Mm{143.0,50.0}) == CenterPad`
  - `zone_at(Mm{143.0,140.0}) == BottomPad`
  - `zone_at(Mm{2.0,2.0}) == None`
  - No two key cells overlap: for every pair of keys, |dx| ≥ 18 or |dy| ≥ 18 (within 0.01). Check this pairwise within each half.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: to-scale layout and hit-testing"`

### Task 13: Contact filtering (palm, hand-down, dead zone) to key events

**Files:**
- Create: `crates/touch/src/filter.rs`, plus the start of `gestures.rs`, with the `TouchTuning` and `TouchEvent` types
- Test: `crates/touch/tests/filter.rs`

**Interfaces:**
- Produces: `pub enum TouchEvent { Down { id: u32, t: Ms, p: Mm, palm: bool }, Move { id: u32, t: Ms, p: Mm, palm: bool }, Up { id: u32, t: Ms } }`.
- Produces: `pub enum TouchOutput { KeyPress(KeyPos, Ms), KeyRelease(KeyPos, Ms), Pointer { dx: i32, dy: i32 }, Scroll { dx: i32, dy: i32 }, Button { mask: u8, down: bool } }`.
- Produces: `pub struct TouchTuning { pub hand_down_window_ms: u32 /*30*/, pub hand_down_min_contacts: usize /*3*/, pub bottom_dead_zone_mm: f32 /*0.0*/, pub tap_max_ms: u32 /*180*/, pub tap_max_move_mm: f32 /*1.5*/, pub pointer_counts_per_mm: f32 /*4.0*/, pub scroll_mm_per_step: f32 /*3.0*/ }` with `Default`.
- Produces: `pub struct Touch` with:
  - `new(Layout, TouchTuning)`
  - `feed(&mut self, ev: TouchEvent) -> Vec<TouchOutput>`
  - `tick(&mut self, t: Ms) -> Vec<TouchOutput>`
  - `horizon(&self) -> Option<Ms>` (the earliest down time still held back)
  - `next_deadline(&self) -> Option<Ms>`

Rules:
- A key contact's `KeyPress` is held back `hand_down_window_ms`, and is emitted with its **original down time**.
- If `hand_down_min_contacts` or more downs land within the window, all of those contacts are ignored for their lifetime.
- A `palm: true` contact on Down or Move is ignored for its lifetime. If its key was already pressed, emit `KeyRelease` at that time.
- The zone is fixed at Down: sliding off a key keeps the key, and a key contact never moves the pointer.
- Downs with `p.y > 179.0 - bottom_dead_zone_mm` are ignored.
- Drivers must never tick the engine past `horizon()`. The `replay` CLI in Task 15 demonstrates this.

- [ ] **Step 1: Write the failing tests**
  - `key_tap`: Down at 0 on key 13, Up at 80. After `tick(30)` you get `KeyPress(13,0)`, and the Up gives `KeyRelease(13,80)`.
  - `hand_down_ignored`: Downs at 0, 5 and 10 on keys 13, 14 and 15, ticked to 100, then Ups give no outputs at all.
  - `two_keys_not_hand_down`: Downs at 0 and 10 on keys 1 and 2 give two `KeyPress` outputs.
  - `palm_mid_press` (Review Focus): Down at 0 on key 38, tick 40 (pressed), then Move at 60 with `palm: true` gives `KeyRelease(38,60)`. The following Up gives nothing.
  - `slide_off_key` (Review Focus): Down on key 13, then Move into the center pad, then Up gives exactly `[KeyPress(13,0), KeyRelease(13,t)]`, with no `Pointer`.
  - `horizon`: right after a key Down at t = 0 (no tick), `horizon() == Some(0)`. After `tick(30)` it's `None`.
  - `stray_up`: Up for an unknown id gives `[]`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: contact filtering"`

### Task 14: Pad gestures

**Files:**
- Modify: `crates/touch/src/gestures.rs`
- Test: `crates/touch/tests/gestures.rs`

Both pads use the same rules (my call):
- One contact moving gives `Pointer`, at `pointer_counts_per_mm` with remainders carried.
- One contact lifted within `tap_max_ms` and moved less than `tap_max_move_mm` gives `Button{1,down}` then `Button{1,up}`.
- Two contacts down together (within `tap_max_ms`) and both lifted as a tap gives button 2 (right).
- Two contacts moving gives `Scroll`, one step per `scroll_mm_per_step` of the average movement. The sign follows the finger (natural scrolling is off).
- Pad contacts need no hand-down holdback, but palm-flagged contacts are ignored.

- [ ] **Step 1: Write the failing tests**
  - `pointer_move`: Down in the bottom pad, Move +10 mm in x gives a total `Pointer.dx` of 40 ± 1.
  - `tap_click`: Down, then Up 100 ms later with no movement, gives `[Button{mask:1,down:true}, Button{mask:1,down:false}]`.
  - `slow_lift_no_click`: Up at 400 ms gives no Button.
  - `two_finger_right_click`: gives mask 2.
  - `two_finger_scroll`: both contacts move +9 mm in y gives a `Scroll.dy` total of 3.
  - `palm_on_pad`: a palm-flagged Down plus Move gives `[]`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it.** **Step 4: Run and confirm they pass.** **Step 5: Commit.** `git commit -m "corne-glass: pad gestures"`

### Task 15: Trace format and the `corne-glass` CLI

**Files:**
- Create: `crates/touch/src/trace.rs`, `crates/cli/Cargo.toml`, `crates/cli/src/main.rs`
- Test: `crates/touch/tests/trace.rs`, `crates/cli/tests/cli.rs`, `crates/cli/tests/fixtures/rest_then_j.jsonl`

**Interfaces:**
- The trace format (JSON Lines; the Phase 2 touch spike must write this):
  - First line: `{"device":"<name>","x_max":30182,"y_max":18864}`.
  - Then one event per line: `{"t_ms":<u64>,"kind":"down"|"move"|"up","id":<u32>,"x":<i32>,"y":<i32>,"palm":<bool>}`, in raw device units. `x` and `y` are omitted on `up`.
- Produces: `pub fn read_trace(r: impl BufRead) -> Result<Vec<TouchEvent>, String>`, which normalizes to mm with `x_mm = x / x_max * 286.4` and `y_mm = y / y_max * 179.0`.
- CLI:
  - `corne-glass check <keymap>` prints each layer as a 3×10 + 6 grid of legends (from `keymap::legend`). It exits 0, or prints the `Error` as `<file>:<line>: <msg>` and exits 1.
  - `corne-glass replay --keymap <path> --trace <file>` feeds the trace through `Touch` and then `Engine`. It ticks the engine only up to `min(now, touch.horizon())`, and prints the engine's outputs one per line as `<t_ms> <output>`, where Key outputs use the `"+NAME"`/`"-NAME"` format.

- [ ] **Step 1: Write the failing tests**
  - `read_trace` scales `x = 30182` to 286.4 and `y = 18864` to 179.0, and rejects a missing header with Err.
  - `check_ok`: `corne-glass check ../../../config/corne.keymap` exits 0, and its stdout contains `Q` and `raise`.
  - `check_error`: run against `keymap/tests/fixtures/unknown.keymap`, it exits 1 and stderr matches `unknown.keymap:\d+: .*foo`.
  - `replay_resting`: the fixture `rest_then_j.jsonl` (a finger down on F's center at 0 and held until 2000, another down on J at 900 and up at 960, coordinates converted with the inverse of the normalization) gives exactly the Key lines `+J` and `-J`.
- [ ] **Step 2: Run and confirm they fail.** **Step 3: Implement it** with clap derive. **Step 4: Run and confirm they pass.** `cargo test --workspace` passes everything.
- [ ] **Step 5: Commit.** `git commit -m "corne-glass: trace format and check/replay CLI"`
