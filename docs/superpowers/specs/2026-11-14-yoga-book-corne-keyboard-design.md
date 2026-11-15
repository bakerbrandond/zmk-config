# Corne on Glass: Yoga Book 9i screen keyboard

Date: 2026-11-14
Status: design, awaiting approval

## Intent

The owner types on a 36-key Corne running the ZMK layout in `config/corne.keymap`
of this repo (`~/repos/zmk/zmk-config`). They have
a Lenovo Yoga Book 9i (model **13IRU8**, machine type 82YQ): two 13.3" 2880x1800
OLED touchscreens and a detachable Bluetooth keyboard. On Windows the bottom
screen offers Lenovo's own software keyboard, which has a conventional layout.

They want to put Arch Linux on the machine and have the bottom screen become their
Corne: the same split 36-key layout, the same layers, and the same behaviors,
including homerow mods. When their real Corne is plugged in, the bottom screen
should become an ordinary second display.

The layout is what's different. This is about keeping one muscle memory across
the real board and the laptop, not about inventing a new touch keyboard.

## Goals

1. Arch Linux + bspwm (X11) running well on the 13IRU8, with both screens and
   both touchscreens usable.
2. A screen keyboard on the bottom display that reproduces `corne.keymap`,
   read directly from the file, so edits to the keymap carry over without
   hand-porting.
3. Homerow mods that are usable on glass. **The failure that would make them give
   up: accidental mods firing when they rest fingers or wrists.** Resting must be
   safe.
4. Touchpad areas on the same screen (see layout).
5. Automatic switch to "normal second display" when the real Corne connects.

## Non-goals

- A kernel driver. "Driver" here means a userspace program. Keystrokes go out
  through uinput, so every app sees a real keyboard.
- ZMK behaviors beyond what `corne.keymap` uses plus ZMK's common core set
  (see "Behaviors to reproduce").
- Wayland support. bspwm/X11 was chosen deliberately, knowing the Linux community
  on this machine uses Hyprland. The design shouldn't make a later Wayland port
  impossible, but that port is not in scope.
- The fingerprint reader, which doesn't work on Linux and isn't needed.
- Lenovo's Windows keyboard modes and widgets.

## Anti-goals

- Any mod (especially a lone GUI or Alt) firing from a finger or wrist that was
  merely resting.
- A layout that differs from `corne.keymap` in a way their fingers would notice.
- The keyboard window being covered by tiled windows, or touches on the bottom
  screen leaking to the desktop as clicks while the keyboard is up.

## Constraints and hardware facts (from research, Nov 2026)

Primary community source: https://github.com/pybe/Omarchy-LenovoYogaBook9
(Arch on 13IRU8, kernel 7.1.9–7.2.5, Hyprland).

- **Displays:** `eDP-1` is the top panel and `eDP-2` the bottom. The top panel is
  mounted 180° with no kernel orientation quirk, so it needs an xrandr rotate.
  The two backlights are separate (`intel_backlight` for eDP-1,
  `card1-eDP-2-backlight` for eDP-2). Brightness keys only drive the first, so a
  sync script is needed.
- **Touch:** everything comes from one internal USB device, `17ef:6161`
  ("INGENIC Gadget Serial and keyboard"). Kernel ≥6.19 (fix 822bc5b3744b,
  `MT_QUIRK_YOGABOOK9I`) splits it into separate input devices named
  "…Touchscreen Top", "…Touchscreen Bottom", "…Stylus Top", "…Stylus Bottom"
  and "…Emulated Touchpad". **Devices must be matched by name**, since they share
  vendor/product IDs. Up to 10 contacts per screen at 100 units/mm
  (X 0–30182, Y 0–18864).
- **No contact size or pressure for fingers.** Each contact reports only tip,
  confidence, ID, X and Y. The confidence bit may surface as
  `ABS_MT_TOOL_TYPE = MT_TOOL_PALM`, but that's unconfirmed. Palm and rest
  detection cannot use contact size. (The descriptor was decoded from a 14IAH10
  capture with the same device, so it needs verifying on the 13IRU8.)
- **Lenovo keyboard dock detection is probably unavailable.** The kernel
  `lenovo-yb9-kbdock` driver matches only the 14IAH10, and the 13IRU8 has no
  `SW_TABLET_MODE`.
- **Sensors:** the accelerometer works through iio-sensor-proxy. The hinge sensor
  reads 0. Rotation logic exists in pybe's `yoga-autorotate` (the top transform
  is the bottom one + 180°, with a ~4 s hold before switching), but it drives
  `hyprctl` and needs porting to xrandr/xinput.
- **Audio:** the TAS2781 calibration fix is in pybe issue #14 and
  https://github.com/MOYF-beta/Yoga-Book-9-13IRU8-Linux-DSP-Fix.
- **Possible ~55 s touch watchdog** on the device's CDC-ACM port. pybe PR #18
  runs a drain daemon on 82YQ. Verify, and adopt the daemon if it's needed.
- **Secure Boot:** disable it to install. Re-enabling with sbctl works. The
  Lenovo BT keyboard doesn't work in the initramfs, so a LUKS prompt needs a USB
  keyboard (the Corne works) or TPM unlock.
- **X11 is untested by anyone on this model since 2023.** Expect to be first.

## Phase 1: Arch + bspwm on the Yoga Book

Deliverable: a reproducible install (a written runbook plus config files and
scripts) that ends with:

- Arch on a current 7.x kernel, with Secure Boot handled as above.
- bspwm + sxhkd on X11, with both monitors configured (eDP-1 rotated inverted)
  and the top screen as the primary.
- Each touchscreen and stylus mapped to its own output via
  `xinput map-to-output` by device name.
- Brightness sync across both backlights.
- Rotation for book and tent use: pybe's logic ported to xrandr + xinput.
- Audio fix applied, and Wi-Fi/BT working.
- The watchdog check done, with the daemon added if needed.
- Suspend tested and its result recorded. It's unknown on this model, so a
  "doesn't work" result is acceptable but must be written down.

**Linux only (decided, 2026-11-15).** Windows is wiped and the whole disk goes
to Arch. The Windows license lives in the firmware (ACPI MSDM table), so a
later reinstall from Microsoft's USB media activates on its own; nothing is
lost for good. What Windows would have offered is covered by a Windows install
USB kept in a drawer, for Lenovo BIOS updates (which ship as Windows
installers) and for warranty diagnostics.

**Before wiping (required, in the last Windows session):**
- Update the BIOS to Lenovo's latest release.
- Set battery charge limit and performance mode in Lenovo Vantage. These live
  in the firmware/EC; Linux can also change them later via `ideapad_laptop`.
- Make a Windows install USB (Microsoft's Media Creation Tool, 8 GB+) and keep it.
- Record a known-good hardware baseline: both speakers, both touchscreens
  (including a touch after a minute idle), both styluses, webcam, microphone,
  the Bluetooth keyboard, and suspend/resume. This is what Linux results get
  compared against.
- Copy off anything worth keeping. The disk is erased.

Secure Boot is still re-enabled with sbctl *keeping Microsoft's keys*, so the
Windows USB can boot for firmware updates without turning Secure Boot off.

Exact partitioning, encryption, bootloader and the rest of the dotfiles are
left to the builder, kept conventional. The owner uses Nix in this repo, but the target
is plain Arch.

## Phase 2: touch spike (throwaway)

Before building the keyboard, record real touch data on the bottom screen: a
small tool that grabs "…Touchscreen Bottom" and logs every contact event with
timestamps. The owner then types normally, rests fingers on the home row, sets
their wrists down, and lays both hands flat.

Questions it must answer:
- Does the confidence/palm bit ever fire for wrists or palms?
- What do resting contacts look like: jitter, drift, or contacts that drop and
  reappear?
- How simultaneous is a hand landing?
- Where do wrists land relative to the bottom edge?
- Does the device keep working past 55 s (the watchdog)?

The output is a short findings note and starting values for the rest and palm
thresholds. The spike's code is thrown away.

## Phase 3: the screen keyboard program

Language: **Rust** (agreed).

### Architecture (decided)

- A userspace daemon. It reads the bottom touchscreen's evdev device directly
  and takes an **exclusive grab** while the keyboard is shown, so X never sees
  those touches.
- It emits key events through a **uinput virtual keyboard**, and pointer motion,
  scroll and buttons through a **uinput virtual pointer**.
- It draws on eDP-2 as an X11 **dock window** (`_NET_WM_WINDOW_TYPE_DOCK`) with
  struts reserving the bottom screen, so bspwm never tiles over it.

### Layout (option C, chosen from to-scale mockups)

The bottom screen is ~286 × 179 mm, with the hinge at the top edge.

- Two halves, straight (not splayed), each with the Corne column stagger and
  three thumb keys. **Push them as far apart as possible while staying
  comfortably on screen**: about 12 mm from the side and top edges in the
  mockup. Keys at ~18 mm pitch, matching the Corne.
- **Center strip** between the halves (from the top margin down to the thumb
  row): one finger moves the pointer, two fingers scroll.
- **Bottom band**, full width below the keys: the main touchpad. One finger
  moves the pointer, one-finger tap left-clicks, two-finger tap right-clicks.
- Wrist and palm contacts on either pad must not move the pointer. That uses the
  same detection as the keys.
- Only the 36 inner keys are shown. `corne.keymap` has 42 positions per layer,
  with the outer columns set to `___`.

Mockup: `.superpowers/brainstorm/*/content/layout.html`, option C.

### Keymap source

- Read `config/corne.keymap` (and its includes) directly, and reload it on save.
  If a reload fails to parse, keep the last good layout and show the error
  (my call).
- The file is plain ZMK devicetree (no helper macros). It includes the local
  `config/behaviors.dtsi` (which defines `hrm` and `td0`), ZMK headers
  (`behaviors.dtsi`, `dt-bindings/zmk/keys.h`, `bt.h`, `pointing.h`), and the
  zmk-anti-idle module (`behaviors/anti_idle_mode.dtsi`,
  `dt-bindings/anti_idle_mode.h`). Versions are pinned in `config/west.yml`
  (ZMK v0.3, anti-idle `main`). How to parse it is the builder's call. Running
  the real C preprocessor with vendored, pinned headers, then parsing the
  resulting devicetree, is the likely route.
- Constructs the program doesn't support produce a clear "not supported: X at
  line N" error, never a silent guess.

### Behaviors to reproduce (from corne.keymap, as of 2026-11-14)

**Scope (my call):** everything `corne.keymap` uses today, plus ZMK's common
core set, so ordinary keymap edits carry over: `&kp` with modifier functions
(`LS(x)` etc.), `&mt`, `&lt`, `&mo`, `&tog`, `&to`, `&sk`, `&sl`, `&trans`,
`&none`, `&caps_word`, `&key_repeat`, custom hold-tap / tap-dance / mod-morph /
macro nodes, combos, and conditional layers. Anything else is an error, as
described above.

What the file uses now:
- Five layers: default, lower, raise, func, mouse.
- `hrm` hold-tap (`config/behaviors.dtsi`): balanced flavor, tapping-term
  280 ms, quick-tap 125 ms, `global-quick-tap`.
  - Default layer: A S D F = LCtrl LGui LAlt LShift. J K L ; = RShift RAlt
    RGui RCtrl. Note that the right side uses the *right* mods; keep that
    (RAlt can be AltGr under some XKB layouts, and the host layout is US).
  - Lower layer: `hrm` on Esc (LCtrl), and on _ + [ ] (RShift RAlt RGui
    RCtrl). Its S D F are mouse buttons (`&mkp RCLK MCLK LCLK`).
- **The resting rule** (decided), layered on top of hold-tap: see the section
  below.
- `td0` tap-dance (200 ms): tap gives momentary LOW, double gives momentary
  MOS, triple toggles MOS.
- Thumbs: left = `&kp LCTRL`, `&td0`, `&kp SPACE`. Right = `&kp BSPC`,
  `&mo RAS`, `&sk LSHFT`.
- Conditional layer: LOW + RAS gives FUN.
- No combos or macros are defined today. They are still supported in case
  they're added later.
- Mouse keys (`&mmv`, `&msc`, `&mkp`): send real pointer movement, scroll and
  clicks through the virtual pointer. The file overrides
  `ZMK_POINTING_DEFAULT_MOVE_VAL` (1500) and `..._SCRL_VAL` (20); scale those to
  feel similar on this screen (the builder tunes the exact factor).
- **Anti-idle** (`&aim AIM_TOG` on func, plus an `anti_idle` node at 30 s
  intervals that nudges the pointer down then up): **my call** is to emulate
  it. `AIM_TOG` toggles a periodic tiny pointer nudge with the node's timings
  through the virtual pointer, off by default.
- `&bt ...`, `&sys_reset`, `&bootloader`: firmware-only. Draw them blank and
  have them do nothing.

### The resting rule

The owner's thumbs **don't** rest on the glass, but their **palms do**.

- **Thumb keys get no resting rule.** `&kp LCTRL`, `&td0`, `&mo RAS`,
  `&sk LSHFT`, Space and Backspace behave exactly as in ZMK: a hold is a hold
  for as long as it lasts.
- The resting rule applies only to `hrm` keys (and to any future hold-tap on a
  non-thumb key).
- **Palm rejection** is the priority for everything near the bottom: thumb
  keys and the bottom touchpad band. A palm contact must never press a thumb
  key or move the pointer. The spike's main job is finding a signal that tells
  palms from thumbs (palm-flag bit, position, a cluster of contacts, or how
  they land).

- A homerow `hrm` key becomes a mod only if another key lands within a short
  window after it touched down. The starting guess is ~500 ms; the spike
  tunes it.
- A key under a finger longer than that is **resting**, and stays resting
  until it's lifted (decided). It sends nothing while resting and nothing on
  lift: no letter and no bare mod. A deliberate mod therefore means pressing
  the next key within the window.
- A quick tap types the letter as usual. A tap released between the tapping
  term and the rest window follows ZMK's balanced-flavor result.
- A long-held plain key shouldn't autorepeat unintentionally. The exact policy
  is the builder's, informed by the spike.
- Hand-down events (several contacts landing near-simultaneously) and
  palm-flagged contacts are ignored. Dead zones near the bottom edge get added
  if the spike shows they're needed.

### Feedback and look

- A key flashes on press.
- A homerow key changes color while it's acting as a mod.
- Held or toggled layer keys get the same color treatment (my call).
- Legends show the active layer. Homerow keys show a small mod label (my call).
- Dark theme, roughly as in the mockup (my call). Anything beyond that is the
  builder's.

### Modes

- **Keyboard mode** (default when no Corne is connected): the program grabs the
  touchscreen, maps the dock window, and sets struts.
- **Corne mode:** the Corne connects through a Prospector dongle, which is the
  central, so the host sees the dongle rather than the halves. The owner isn't
  sure whether the dongle will stay plugged in. **My call:** detection is based
  on activity, which works either way. The program enters Corne mode on the
  first keystroke arriving from the ZMK device. It leaves Corne mode when that
  device disappears (unplugged, or BT disconnect), or on the manual toggle. It
  never leaves on an idle timeout, because flipping the bottom screen would
  re-tile windows under the owner. On entering Corne mode, the program releases
  the grab, unmaps the window, and drops the struts. eDP-2 is then a normal
  bspwm monitor, and touch on it works as a normal touchscreen.
- A **manual toggle** (an sxhkd hotkey, plus a small on-screen target) overrides
  either mode. That also covers the Lenovo BT keyboard, which can't be detected.

## Left to the builder

- Where the code lives. Suggested: a new top-level directory in this repo, so it
  versions alongside the keymap (my call, easy to change).
- The keymap parsing approach (see above), the rendering library, and the
  details of touch to key hit-testing.
- Exact thresholds, informed by the spike.
- Packaging (a systemd user service or bspwmrc start), the install runbook
  format, and how Phase 1 configs are stored.
- Rotation behavior while the keyboard is up. Keeping the keyboard only in
  normal laptop orientation is acceptable.
- Testing strategy. The behavior engine (hold-tap, tap-dance, combos, resting
  rule) should be testable without hardware against recorded touch traces,
  including the ones from the spike.
