# Yoga Book 9i Arch + bspwm Install Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A reproducible Linux-only Arch Linux + bspwm (X11) install on the Lenovo Yoga Book 9i 13IRU8, with both screens, both touchscreens, styluses, brightness, rotation, audio and Bluetooth working, captured as a runbook plus scripts and dotfiles in this repo.

**Architecture:** Everything lives in `yogabook/` in this repo. Small POSIX-ish bash scripts in `yogabook/bin/` each own one hardware concern (displays, touch mapping, brightness, rotation). Every script has a pure "plan" mode (prints the commands it would run), which is tested on any Linux box with fake `xrandr`/`xinput`/`brightnessctl` on `PATH`. Dotfiles are symlinked by an install script. Hardware-only work (the OS install itself, audio, watchdog, Secure Boot) is written as runbook tasks with exact verification commands. Results go in `yogabook/HARDWARE-NOTES.md`.

**Tech Stack:** bash, xrandr, xinput, brightnessctl, iio-sensor-proxy (`monitor-sensor`), bspwm, sxhkd, systemd-boot, LUKS2, sbctl.

**Spec:** `docs/superpowers/specs/2026-11-14-yoga-book-corne-keyboard-design.md` (Phase 1)

## Global Constraints

- Model 13IRU8 (machine type 82YQ). `eDP-1` = top panel, `eDP-2` = bottom panel, both 2880x1800@60.
- The top panel is mounted 180°: eDP-1 is always the bottom panel's transform + 180°.
- Kernel ≥ 6.19. This is required for the split touch devices "…Touchscreen Top/Bottom" and "…Stylus Top/Bottom" (commit 822bc5b3744b).
- Touch and stylus devices share USB ID `17ef:6161`, so **match by name suffix**, never by ID or prefix.
- Backlights: `intel_backlight` (eDP-1) and `card1-eDP-2-backlight` (eDP-2).
- **Linux only: Windows is wiped**, but only after every "Before wiping" step in Task 6 is checked off.
- Secure Boot is re-enabled with sbctl **keeping Microsoft keys** (`enroll-keys -m`), so the Windows install USB still boots for BIOS updates and warranty diagnostics.
- Rotation must hold steady for **4 s** before it's applied.
- X11 + bspwm, started with `startx` from tty1. No display manager.
- The scripts must not touch the bottom-screen touchscreen when the `corne-glass` daemon owns it. That's a later plan; for now, `yb-touchmap` maps it like any touchscreen.

## Review Focus

- **Device names with an unexpected prefix**, or a device that's missing: scripts match on suffix, warn on stderr, and exit 0 so bspwm startup continues. Test in Task 3.
- **Scripts run at X startup before the outputs or input devices exist**: `yb-touchmap` with no matching devices must warn and exit 0, never fail bspwmrc. Test in Task 3.
- **Brightness driven to 0** makes the OLED look dead: always use a 1% floor (`brightnessctl -n1`). Test in Task 4.
- **Wiping before the last Windows session is finished**: once the disk is erased, the BIOS update path, Vantage settings, and the Windows hardware baseline are gone until a reinstall. The "Before wiping" steps in Task 6 are a gate before the disk is touched.
- **Rotation flapping** while carrying the device: only an orientation held ≥ 4 s is applied. Test in Task 2.

---

### Task 1: Scaffold, test runner, and `yb-displays`

**Files:**
- Create: `yogabook/bin/yb-displays`
- Create: `yogabook/lib/layout.sh` (shared orientation → xrandr arguments)
- Create: `yogabook/test/run.sh` (test runner: runs every `test/*_test.sh`, prints PASS/FAIL, exits non-zero on any failure)
- Create: `yogabook/test/helpers.sh` (`assert_eq expected actual msg`; `fake_bin name script`, which creates an executable in a temp dir prepended to `PATH`)
- Test: `yogabook/test/displays_test.sh`

**Interfaces:**
- Produces: `yb_xrandr_args <orientation>`, where orientation ∈ `normal|left-up|right-up|bottom-up`. It's sourced from `lib/layout.sh` and prints the xrandr arguments on one line.
- Produces: `yb-displays [--dry-run] [orientation]` (default `normal`). It runs `xrandr <args>`, or prints `xrandr <args>` with `--dry-run`.

The orientation table is derived, not measured. Task 7 verifies it on hardware.

| orientation | eDP-2 (bottom) | eDP-1 (top) |
|---|---|---|
| normal | `--rotate normal --pos 0x1800` | `--rotate inverted --pos 0x0` |
| left-up | `--rotate left --pos 0x0` | `--rotate right --pos 1800x0` |
| right-up | `--rotate right --pos 1800x0` | `--rotate left --pos 0x0` |
| bottom-up | `--rotate inverted --pos 0x0` | `--rotate normal --pos 0x1800` |

Both outputs always get `--mode 2880x1800`, and eDP-1 always gets `--primary`.

- [ ] **Step 1: Write the failing test** in `yogabook/test/displays_test.sh`

```bash
. "$(dirname "$0")/helpers.sh"
out=$("$BIN/yb-displays" --dry-run)
assert_eq "xrandr --output eDP-1 --mode 2880x1800 --rotate inverted --pos 0x0 --primary --output eDP-2 --mode 2880x1800 --rotate normal --pos 0x1800" "$out" "normal layout"
out=$("$BIN/yb-displays" --dry-run left-up)
assert_eq "xrandr --output eDP-1 --mode 2880x1800 --rotate right --pos 1800x0 --primary --output eDP-2 --mode 2880x1800 --rotate left --pos 0x0" "$out" "left-up layout"
"$BIN/yb-displays" --dry-run sideways 2>/dev/null; assert_eq 2 "$?" "unknown orientation exits 2"
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `bash yogabook/test/run.sh`
Expected: FAIL, `yb-displays: No such file or directory`

- [ ] **Step 3: Implement `lib/layout.sh` and `bin/yb-displays`** using the table above. An unknown orientation prints usage to stderr and exits 2.

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `bash yogabook/test/run.sh`
Expected: `PASS displays_test.sh`, exit 0

- [ ] **Step 5: Commit**

```bash
git add yogabook/
git commit -m "yogabook: add display layout script and test runner"
```

### Task 2: `yb-rotate` with a 4 s hold

**Files:**
- Create: `yogabook/bin/yb-rotate`
- Test: `yogabook/test/rotate_test.sh`

**Interfaces:**
- Consumes: `yb_xrandr_args` (Task 1), `yb-touchmap` (Task 3; called after each applied rotation, and skipped if it's not on `PATH`).
- Produces: `yb-rotate --decide`, which reads `<epoch_ms> <orientation>` lines on stdin and prints one line `<epoch_ms> <orientation>` each time an orientation has been held ≥ 4000 ms and differs from the last applied one. The start state is `normal`. A decision is emitted only when a later line, with a timestamp ≥ candidate start + 4000 and the same orientation, proves the hold lasted. EOF emits nothing.
- Produces: `yb-rotate --watch`, which pipes `monitor-sensor` through a timestamping filter into the same decision logic, then runs `yb-displays <orientation>` and `yb-touchmap`. The parser looks for `Accelerometer orientation changed: <o>`, plus the initial `=== Has accelerometer (orientation: <o>)`.

- [ ] **Step 1: Write the failing test**

```bash
. "$(dirname "$0")/helpers.sh"
out=$(printf '0 left-up\n1000 normal\n2000 left-up\n6500 left-up\n' | "$BIN/yb-rotate" --decide)
assert_eq "6000 left-up" "$out" "applies only after a 4 s hold; ignores the 1 s flap"
out=$(printf '0 normal\n9000 normal\n' | "$BIN/yb-rotate" --decide)
assert_eq "" "$out" "no output when orientation never changes"
out=$(printf '0 bottom-up\n4000 bottom-up\n4100 right-up\n8200 right-up\n' | "$BIN/yb-rotate" --decide)
assert_eq $'4000 bottom-up\n8100 right-up' "$out" "two successive rotations"
```

- [ ] **Step 2: Run and confirm it fails.** `bash yogabook/test/run.sh` should give `FAIL rotate_test.sh`.
- [ ] **Step 3: Implement `yb-rotate`.** The decision time printed is the candidate's first timestamp + 4000 ms. An orientation equal to the last applied one clears the candidate. `--watch` uses `date +%s%3N` for timestamps.
- [ ] **Step 4: Run and confirm it passes.** `bash yogabook/test/run.sh` should give `PASS rotate_test.sh`.
- [ ] **Step 5: Commit.** `git commit -am "yogabook: add rotation with 4s hold"` (after `git add yogabook/`)

### Task 3: `yb-touchmap`

**Files:**
- Create: `yogabook/bin/yb-touchmap`
- Test: `yogabook/test/touchmap_test.sh`

**Interfaces:**
- Produces: `yb-touchmap [--dry-run]`. For each name in `xinput list --name-only` that ends with `Touchscreen Top` or `Stylus Top`, it maps to eDP-1. Names ending in `Touchscreen Bottom` or `Stylus Bottom` map to eDP-2. The command is `xinput map-to-output "<full name>" <output>`. Every expected suffix that's missing prints `yb-touchmap: no device matching '<suffix>'` to stderr. It always exits 0.

- [ ] **Step 1: Write the failing test**

```bash
. "$(dirname "$0")/helpers.sh"
fake_bin xinput 'if [ "$1" = list ]; then printf "%s\n" "Virtual core pointer" "INGENIC Gadget Serial and keyboard Touchscreen Top" "INGENIC Gadget Serial and keyboard Touchscreen Bottom" "INGENIC Gadget Serial and keyboard Stylus Bottom"; else echo "xinput $*" >> "$FAKE_LOG"; fi'
"$BIN/yb-touchmap" 2>"$TMP/err"; rc=$?
assert_eq 0 "$rc" "exits 0 even with a missing device"
assert_eq $'xinput map-to-output INGENIC Gadget Serial and keyboard Touchscreen Top eDP-1\nxinput map-to-output INGENIC Gadget Serial and keyboard Touchscreen Bottom eDP-2\nxinput map-to-output INGENIC Gadget Serial and keyboard Stylus Bottom eDP-2' "$(cat "$FAKE_LOG")" "maps by suffix"
assert_eq "yb-touchmap: no device matching 'Stylus Top'" "$(cat "$TMP/err")" "warns on missing"
fake_bin xinput 'if [ "$1" = list ]; then echo "Virtual core pointer"; fi'
"$BIN/yb-touchmap" 2>/dev/null; assert_eq 0 "$?" "no devices at all still exits 0"
```

(`helpers.sh` defines `TMP` and `FAKE_LOG`, a fresh file per test.)

- [ ] **Step 2: Run and confirm it fails.**
- [ ] **Step 3: Implement it.** Iterate names with `while IFS= read -r`, and quote names when calling `xinput`.
- [ ] **Step 4: Run and confirm it passes.**
- [ ] **Step 5: Commit.** `git add yogabook/ && git commit -m "yogabook: map touch and stylus devices to outputs by name"`

### Task 4: `yb-brightness`

**Files:**
- Create: `yogabook/bin/yb-brightness`
- Test: `yogabook/test/brightness_test.sh`

**Interfaces:**
- Produces: `yb-brightness up|down|sync`.
  - `up` runs `brightnessctl -n1 -d intel_backlight set 5%+`.
  - `down` runs `brightnessctl -n1 -d intel_backlight set 5%-`.
  - All three then read the percentage with `brightnessctl -m -d intel_backlight` (CSV field 4, e.g. `40%`) and run `brightnessctl -n1 -d card1-eDP-2-backlight set <pct>`.
  - Anything else exits 2.

- [ ] **Step 1: Write the failing test**

```bash
. "$(dirname "$0")/helpers.sh"
fake_bin brightnessctl 'echo "brightnessctl $*" >> "$FAKE_LOG"; case "$*" in *-m*) echo "intel_backlight,backlight,400,40%,1000";; esac'
"$BIN/yb-brightness" up
assert_eq $'brightnessctl -n1 -d intel_backlight set 5%+\nbrightnessctl -m -d intel_backlight\nbrightnessctl -n1 -d card1-eDP-2-backlight set 40%' "$(cat "$FAKE_LOG")" "up then sync"
"$BIN/yb-brightness" bogus 2>/dev/null; assert_eq 2 "$?" "bad arg"
```

- [ ] **Step 2: Run and confirm it fails.** **Step 3: Implement it.** **Step 4: Run and confirm it passes.**
- [ ] **Step 5: Commit.** `git add yogabook/ && git commit -m "yogabook: sync brightness across both panels"`

### Task 5: Dotfiles and their installer

**Files:**
- Create: `yogabook/dotfiles/bspwm/bspwmrc`, which runs:
  - `yb-displays`, then `yb-touchmap`
  - `bspc monitor eDP-1 -d 1 2 3 4 5` and `bspc monitor eDP-2 -d 6 7 8`
  - `pgrep -x yb-rotate >/dev/null || yb-rotate --watch &`
- Create: `yogabook/dotfiles/sxhkd/sxhkdrc`, with:
  - `XF86MonBrightnessUp` → `yb-brightness up`, and `XF86MonBrightnessDown` → `yb-brightness down`
  - `super + Return` → `alacritty`, and `super + space` → `rofi -show drun`
  - the usual bspwm focus, move and close bindings
  - a commented placeholder line `# super + k : corne-glass toggle (added by the keyboard plan)`
- Create: `yogabook/dotfiles/xinitrc` (`sxhkd & exec bspwm`) and `yogabook/dotfiles/zprofile-snippet` (`[ -z "$DISPLAY" ] && [ "$XDG_VTNR" = 1 ] && exec startx`)
- Create: `yogabook/install-dotfiles.sh`, which:
  - symlinks `bspwmrc` → `$HOME/.config/bspwm/bspwmrc`, `sxhkdrc` → `$HOME/.config/sxhkd/sxhkdrc`, and `xinitrc` → `$HOME/.xinitrc`
  - symlinks each `yogabook/bin/*` → `$HOME/.local/bin/`
  - appends the zprofile snippet to `$HOME/.zprofile` only if it isn't already present
  - is idempotent
- Test: `yogabook/test/dotfiles_test.sh`

- [ ] **Step 1: Write the failing test.** Run the installer twice with `HOME=$TMP/home`. Assert each symlink target with `readlink`, assert that `$HOME/.zprofile` contains the snippet exactly once (`grep -c startx` = 1), and assert `bspwmrc` is executable.
- [ ] **Step 2: Run and confirm it fails.** **Step 3: Implement it.** **Step 4: Run and confirm it passes.**
- [ ] **Step 5: Commit.** `git add yogabook/ && git commit -m "yogabook: bspwm/sxhkd dotfiles and installer"`

### Task 6: Runbook part 1, from the last Windows session to first boot (on the hardware)

**Files:**
- Create: `yogabook/README.md`, the runbook. Every step below goes in it as written, with its verification command.
- Create: `yogabook/HARDWARE-NOTES.md` (sections: Windows baseline, Install, Displays, Touch, Rotation, Brightness, Audio, Bluetooth, Watchdog, Suspend, Power, Secure Boot. Each gets a status of works/partial/broken plus notes.)

The decisions this task fixes:
- The whole disk is wiped to a new GPT with two partitions: a **1 GiB vfat ESP** mounted at `/boot`, and LUKS2 with the rest.
- Linux root is ext4 inside LUKS2, unlocked by passphrase from a USB keyboard. The Corne's Prospector dongle on USB works, as does any USB keyboard. No TPM unlock.
- systemd-boot, installed to the ESP at `/boot`.
- zram swap via `zram-generator`. No hibernation.
- `mkinitcpio` HOOKS: `(base systemd keyboard autodetect microcode modconf kms sd-vconsole block sd-encrypt filesystems fsck)`. `keyboard` comes before `autodetect` so USB keyboards work at the LUKS prompt.
- Packages (pacstrap):
  - System: `base linux linux-firmware intel-ucode sof-firmware alsa-ucm-conf networkmanager sudo zsh git vim`
  - Desktop: `xorg-server xorg-xinit xorg-xrandr xorg-xinput bspwm sxhkd alacritty rofi brightnessctl iio-sensor-proxy`
  - Audio, Bluetooth, swap, Secure Boot: `pipewire pipewire-pulse wireplumber bluez bluez-utils zram-generator sbctl`
  - Debugging and later work: `evtest libinput rustup`

Steps:
- [ ] **Step 1: Before wiping, in Windows (last session).** **Gate: every item is checked off before Step 2.** Write results down off-machine (phone or paper); they go into HARDWARE-NOTES.md "Windows baseline" in Step 7.
  - Update the BIOS to the latest release from Lenovo's support page (or Vantage). Record the version shown in the BIOS setup screen.
  - In Lenovo Vantage, set the battery charge limit (Conservation Mode) and the performance mode you want. Record both.
  - Make a Windows install USB with Microsoft's Media Creation Tool (8 GB+). Label it and keep it; it's the path for future BIOS updates and warranty diagnostics.
  - Hardware baseline, each recorded as works/broken: both speakers (left/right), both touchscreens, a touch on the bottom screen after 60 s idle, both styluses on both screens, webcam, microphone, the Bluetooth keyboard, and sleep then wake.
  - Copy off any files worth keeping. The whole disk is erased in Step 4.
- [ ] **Step 2: In the BIOS,** disable Secure Boot. Boot the current Arch ISO from USB.
- [ ] **Step 3: Confirm the Windows license is in firmware** before wiping: on the ISO, `ls /sys/firmware/acpi/tables/MSDM` exists. Record it. If it's missing, stop and find the product key before going on.
- [ ] **Step 4: Wipe and partition the whole disk.**
  - `sgdisk --zap-all` on the internal NVMe (check the device name with `lsblk` first; the USB sticks must not be the target).
  - Create the 1 GiB ESP (`ef00`, `mkfs.fat -F32`) and the LUKS partition with the rest.
  - Verify with `lsblk -f`: exactly those two partitions on the internal disk.
- [ ] **Step 5: Install.**
  - `cryptsetup luksFormat --type luks2`, then open it and create ext4.
  - Mount root at `/mnt` and the ESP at `/mnt/boot`.
  - pacstrap the packages above and genfstab.
  - In the chroot: set the locale, timezone, `hwclock --systohc --utc`, and hostname. Create the user with sudo and zsh, and set the HOOKS above.
  - Run `bootctl install`. Write a loader entry with `rd.luks.name=<uuid>=root root=/dev/mapper/root`.
  - Enable `NetworkManager`, `bluetooth`, and `systemd-zram-setup@zram0`.
- [ ] **Step 6: Reboot and verify.**
  - systemd-boot shows the Arch entry, and the LUKS prompt accepts the passphrase from a USB keyboard.
  - In Arch, `uname -r` is ≥ 6.19.
  - `grep -E 'Touchscreen (Top|Bottom)|Stylus (Top|Bottom)' /proc/bus/input/devices` lists all four.
  - Record the exact device names in HARDWARE-NOTES.md.
- [ ] **Step 7: Fill in "Windows baseline"** in HARDWARE-NOTES.md from the Step 1 notes (BIOS version, Vantage settings, each baseline result).
- [ ] **Step 8: Commit the runbook and notes.** `git add yogabook/README.md yogabook/HARDWARE-NOTES.md && git commit -m "yogabook: install runbook"`

### Task 7: Runbook part 2, verifying the desktop on hardware

- [ ] **Step 1:** Clone this repo on the Yoga Book, run `yogabook/install-dotfiles.sh`, log in on tty1, and confirm bspwm starts.
- [ ] **Step 2: Displays.** Both panels are upright, eDP-1 is above eDP-2, and moving the pointer off the top screen's bottom edge enters the bottom screen. If the orientation is wrong, fix the table in `lib/layout.sh` and its test, then re-run `bash yogabook/test/run.sh`.
- [ ] **Step 3: Touch.** A finger and a stylus on each screen land under the contact on *that* screen. Check `xinput list-props "<name>" | grep "Coordinate Transformation"` after `yb-touchmap`.
- [ ] **Step 4: Rotation.** Run `monitor-sensor`, physically turn the device to each orientation, and record the reported string. Then let `yb-rotate --watch` apply each one and confirm both screens and touch are correct. If the sensor's reported orientation doesn't match the physical one, correct the table and its test, and note why in HARDWARE-NOTES.md.
- [ ] **Step 5: Brightness.** The brightness keys move both panels together and never go fully black.
- [ ] **Step 6: Bluetooth.** Pair the Lenovo keyboard (`bluetoothctl`, device "yb9-kb-keyboard") and pair the Corne if you use it without the dongle. Record the results.
- [ ] **Step 7: Commit** the fixes and notes: `git commit -am "yogabook: hardware verification fixes"`

### Task 8: Runbook part 3, audio, watchdog, suspend, and Secure Boot (on hardware)

- [ ] **Step 1: Audio.** Apply the TAS2781 calibration fix from pybe issue #14 (https://github.com/pybe/Omarchy-LenovoYogaBook9) and, if needed, https://github.com/MOYF-beta/Yoga-Book-9-13IRU8-Linux-DSP-Fix. Write the exact commands you ran into the runbook. Verify with `speaker-test -c2 -t wav`: both channels are audible. If bass routing is still broken, note it as partial.
- [ ] **Step 2: Touch watchdog.**
  - Run `evtest` on "…Touchscreen Bottom" and touch it every ~10 s for 3 minutes.
  - If events stop around 55 s, install the CDC-ACM drain approach from pybe PR #18 as a systemd service `yb-acm-drain.service`, with its unit file committed under `yogabook/systemd/`. Re-run the 3-minute check.
  - Record the result either way.
- [ ] **Step 3: Suspend.** Run `systemctl suspend`, wake the machine, and check that both screens, touch, Wi-Fi and audio come back. Record the result. A "broken" result is acceptable; recording it isn't optional.
- [ ] **Step 3b: Power.** Check the Vantage settings carried over: `cat /sys/bus/platform/drivers/ideapad_acpi/*/conservation_mode` matches what was set in Windows (1 = on), and `cat /sys/firmware/acpi/platform_profile` with `platform_profile_choices` shows the profiles. Record both under Power. Compare every Linux result with the "Windows baseline" section; anything that worked there and not here is a Linux issue, not a hardware fault.
- [ ] **Step 4: Secure Boot.**
  - In the BIOS, put Secure Boot into Setup Mode (clear the keys).
  - In Arch, run `sbctl create-keys && sbctl enroll-keys -m`, then `sbctl sign -s` on `/boot/EFI/systemd/systemd-bootx64.efi`, `/boot/EFI/BOOT/BOOTX64.EFI` and `/boot/vmlinuz-linux`.
  - Check with `sbctl verify`: everything is signed.
  - Enable Secure Boot. Arch boots, and the Windows install USB boots to its first screen (proves the Microsoft keys are kept; back out without installing).
- [ ] **Step 5: Commit.** `git add yogabook/ && git commit -m "yogabook: audio, watchdog, suspend, secure boot notes"`
