# Typing pet — design

Date: 2026-11-19
Status: revised 2026-11-19 (pet replaces the WPM panel), awaiting approval

## Intent

A hidden extra on the Prospector dongle, next to Zappy Birds and the matrix
rain: a small virtual pet that lives on the dongle and **reacts to how much you
type**. A Function-layer key swaps the status screen's WPM panel for the pet,
in the same spot, so it sits there while you type, alongside the layer name,
batteries and indicators. It bounces along with your typing
speed, dozes when you stop, and grows into new forms as your lifetime
keystrokes add up. A little care on the side (feeding, a quick mini-game) gives
it some of the classic virtual-pet feel, but it never dies.

The virtual-pet idea is a well-known genre. The name, creature, art and look
are our own. Do **not** copy Tamagotchi (or any other existing pet game): not
its name, characters, egg/device shapes, sprites, icons, sounds or menus.

Working name: **Clackling**, a little critter born from keyboard clacks
(builder's call to change it; keep it original).

## Goals

- Watching it react is the point: its pose and animation follow your recent
  WPM live while you type.
- Growth is earned by typing: lifetime keystroke count drives its stages and
  survives power cycles.
- Light care: feeding and a short mini-game, without any guilt mechanics.
- Nord look, consistent with the rest of the Prospector theme (`theme.h`).

## Non-goals

- Death, sickness, poop/cleaning, or anything that punishes time away.
- Running on the nice!view halves (dongle only).
- Sound.
- A full-screen pet view, or the pet as the screensaver (possible later; not
  now).

## Anti-goals (failure even if it "works")

- Keys failing to reach the computer while the pet is showing (outside the
  ~10 s mini-game).
- Keys leaking to the computer during the mini-game.
- Getting stuck in the mini-game.
- Disturbing the rest of the status screen (layer roller, batteries,
  indicators, clock) when the pet is swapped in or out.
- Flash wear from frequent saves.
- Breaking or slowing normal typing, BLE, the jiggler, Zappy Birds, the matrix
  rain or the status widgets.
- Running the LVGL heap or RAM out (the dongle is at ~67% RAM after the
  2026-11-18 savings; sprites belong in flash).
- Looking like a copy of an existing pet game.

## Approaches

How typing affects the pet:

- **A. Typing is food**: keystrokes fill hunger; idle days starve it. Simple,
  but punishes weekends.
- **B. It mirrors you**: mood follows WPM, growth follows lifetime keystrokes,
  never dies. Most "keyboard pet", no guilt, less tension.
- **C. It wants breaks**: happy with steady typing, grumpy after long
  stretches. A break reminder that sides against long work sessions.

**Chosen: B, plus a little of A** (hunger that only slows growth, and a feed
key).

Where you see it:

- **1. Pet screen you can type over**: replaces the status screen while open;
  keys still go to the computer.
- **2. Small pet on the status screen** plus a full care app. Always visible,
  but crowds a tuned screen.
- **3. Pet as the screensaver** plus a full care app. Only visible when idle.

**First chosen: 1.** Revised by the user the same day: **the pet replaces the
WPM panel** on the status screen, toggled by a key (close to 2, but swapping out
the WPM panel rather than squeezing in beside it, and with no separate care
app).

## Decided behaviour

### Keys (Function layer)

The only free Function-layer keys are , . and / (positions 32–34, bottom row,
right hand). Never bind positions 0, 11, 12, 23, 24 or 35.

| Key | Does |
| --- | --- |
| Function + , | Swap the WPM panel for the pet, and back |
| Function + . | Feed |
| Function + / | Start the mini-game |

Feed and the mini-game only do something while the pet is showing; with the
WPM panel showing they do nothing (and send nothing). Which of the two is
showing is saved, so it survives a reboot *(my call)*.

### Typing passes through

While the pet is showing, no capture layer is active: every key behaves as
normal and reaches the computer. Swapping is purely a display change inside the
status screen (hide one widget, show the other), done on the display work
queue.

### Mood (from WPM)

Mood follows ZMK's WPM (already used by the status screen's WPM panel):

- fast typing → excited / bouncing animation
- steady typing → calm, content
- no typing for a while → sleepy, then asleep after 2 minutes idle

Exact WPM bands and animation timing are the builder's call; the reaction
should be visible within a couple of seconds of a change in typing speed.

### Growth (from lifetime keystrokes)

Every physical key press counts, whether or not the pet is showing (the
dongle is the central, so it sees every press from both halves). That includes
modifiers, layer keys and each key of a combo. Presses made while the
mini-game's capture layer or the Zappy layer is active don't count.
*(Reviewer question; my call: physical presses, not only keys sent to the
computer.)*

Two counters are kept and saved *(reviewer question; my call)*:

- **keys**: the true lifetime press count, shown on screen.
- **growth**: what drives the stages. It goes up by 1 per press, or by ½ while
  the pet is hungry, so store it in half-points. The stage thresholds below are
  growth values.

| Stage | Reached at |
| --- | --- |
| Egg | start |
| Baby (hatches) | 500 keystrokes |
| Child | 10,000 |
| Teen | 100,000 |
| Adult | 1,000,000 |

Each stage has its own sprite. Hatching and each stage-up get a short
celebration animation the next time the pet is shown (or right away if it's
showing).

### Hunger

- A fullness meter drains from full to empty over about 24 hours of dongle
  uptime.
- Function + . refills it (to full, or by a large step; builder's call).
- A hungry (empty) pet looks hungry, and growth counts each keystroke as half
  (keys still count in full).
  Nothing else bad happens.
- Time with the dongle unplugged doesn't count: the pet just naps. Use uptime,
  not wall-clock time.

### Mini-game

Function + / starts a ~10 s side-guessing game: the pet hides left or right,
you press any key on the left or right half to guess, for a handful of rounds
(about 5). For its duration only, a capture layer is active so guesses don't
reach the computer (like Zappy Birds' &zflap layer). It plays inside the pet's
panel: the pet pops out on the left or right side of it. The capture layer is
hidden from the layer roller like the Zappy layer, since the roller stays
visible. When it ends, by finishing
or by a timeout, the layer is released and the screen shows the result. A good
result cheers the pet up: a happy animation, and a few minutes of extra
cheerfulness that raises its mood by one level, then wears off. It isn't saved.
*(Reviewer question; my call: no lasting happiness value, so nothing extra to
write to flash.)* Whether it also nudges hunger or growth is the builder's call,
but keep it small. It must always end by itself;
no way to get stuck.

### The pet panel (204×64, where the WPM panel sits)

The WPM panel is 204×64 px at x 16, y 124 of the 280×240 status screen. The pet
takes exactly that box:

```
 ┌─────────────────────────────────────────┐
 │ ┌──────┐  Child        ♥♥♥♡  ▮▮▮▯       │
 │ │ pet  │  42 WPM · 12,480 keys          │
 │ │ 2×   │  ▓▓▓▓▓▓▓▓░░░░░░  next 100k     │
 │ └──────┘                                │
 └─────────────────────────────────────────┘
```

Left: the pet as chunky pixel art, 24×24 drawn at 2× (48 px). Right: stage
name, mood hearts, fullness pips; current WPM and lifetime keys (so the WPM
number isn't lost when the graph is swapped out); a progress bar to the next
stage. Icons drawn ourselves or from the CaskaydiaCove Nerd Font already in the
build; no emoji font. During the mini-game the right side shows "left or
right?" and the score, and the pet appears at the guessed side of the panel on
reveal.

### Idle and screensaver

- The pet falls asleep after 2 minutes idle. The matrix rain and Zappy Birds
  work exactly as today: they take over the whole screen and hand it back,
  pet included.
- The mini-game ends by itself; if the screensaver or Zappy starts meanwhile
  (it shouldn't, since keys are being pressed), the game is ended and its
  layer released.

### Saving

- Keys, growth, fullness and stage are saved in Zephyr settings (a new
  "pet" subtree), from the system work queue, never the display thread.
- Save at most every 15 minutes, and only if something changed. Also save on a
  stage-up. Losing up to 15 minutes of keystrokes on a power cut is fine.
- Loaded at boot like the Zappy Birds scores.

## Structure (follow the Zappy Birds pattern)

In the fork (`prospector-zmk-module`), under `src/pet/`:

- `pet_core.c/.h`: pure C rules (stages, hunger drain, mood from WPM, growth
  with the hunger halving, mini-game rounds), no Zephyr or LVGL, host-tested in
  `tests/pet/`.
- `pet_panel.c/.h`: the LVGL pet widget, created next to the WPM panel in
  `custom_status_screen.c`, same size and position.
- `pet_mode.c/.h`: the swap, timers, input from ZMK events.
- `pet_store.c`: settings load/save.
- Behaviors in `src/behaviors/`, devicetree bindings in
  `dts/bindings/behaviors/`, a `CONFIG_PROSPECTOR_TYPING_PET` Kconfig option
  (off by default; on in `corne_dongle.conf`), CMake wiring like Zappy's.

In this repo: the keymap bindings and capture layer (hidden from the Prospector
layer list like the Zappy layer), the Function-layer table in the README, and a
README bullet for the pet.

## Left to the builder

- The creature's design and sprites (original; small enough to keep flash use
  modest), stage names, animation frames.
- WPM bands for each mood and animation timing.
- Exact behaviour names and parameters, and how the capture layer is wired.
- Feed amount, mini-game details (round count, timing, scoring, reward).
- The 24 h drain curve (linear is fine).
