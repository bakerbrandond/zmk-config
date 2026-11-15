# Dongle brightness key (short design)

Date: 2026-11-15
Status: approved ("save it", "position 15, build it")

## Intent

A key that cycles the Prospector backlight through a few levels, so the screen
can be dimmed for the room or turned off. The chosen level survives unplugging.

## Decided (by the user)

- Cycles through about 6 values from 0 (backlight off) to 100.
- The level is saved across power cycles.
- The key is on the Function layer at position 15, the left home-row D spot.
  It's a 36-key Corne, so outer-column positions don't exist.

## My calls

- Levels: 0, 5, 15, 30, 60, 100. They're spaced for perceived brightness, and
  the cycle wraps from 100 to 0. A value between levels, such as the boot
  default of 50, steps to the next higher level.
- Boot: the saved level if there is one, otherwise
  `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS` (50).
- Saving: Zephyr settings key `prospector/bl`, written 2 s after the last press,
  so cycling through several levels costs one flash write.
- Only in fixed-brightness mode (`!PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR`). With the
  sensor mode, the behavior is a no-op that still swallows the key.

## Approaches

- **New `&pbl` behavior in the Prospector fork (chosen).** It drives the same
  `pwm-leds` channel that `brightness.c` already uses.
- ZMK's built-in `&bl`. It's built for keyboard LED backlights, not this PWM
  display backlight, and it has no level list. Rejected.

## Verification

- A host test for the level-stepping function.
- Builds: dongle, halves, and the dongle without the key node.
- On the device:
  - each press changes the brightness, and 0 is dark
  - the level survives unplugging
