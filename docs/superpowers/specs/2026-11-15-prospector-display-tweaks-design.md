# Prospector display tweaks (short design)

Date: 2026-11-15
Status: draft, awaiting approval

## Intent

Clean up the default status screen:

- show only the active layer, with no greyed-out previews of other layers
- move the active layer name up a little
- make the WPM panel's `wpm` legend readable and stop it overlapping the
  number

## Decided (by the user)

- Inactive layer names are removed.
- The active layer moves up.
- WPM legend layout A: the legend sits below the number with a proper gap,
  and is brighter and larger.

## My calls (correct if wrong)

- **Layer roller.** Inactive rows become fully transparent (`text_opa` 0 on
  `LV_PART_MAIN`). The slide animation on a layer change stays. The fade-mask
  draw callback is removed, since it existed only for the dim rows.
- **Height.** `CONFIG_PROSPECTOR_ACTIVE_LAYER_Y` default with the WPM panel
  goes from -42 to -62, about 20 px up. The jiggle, USB and BT indicators in
  `zmk-config/src/prospector_indicators.c` follow automatically, because they
  read the same value. Tuned on the device.
- **WPM legend.** NORD4 (`THEME_WPM_LABEL`) instead of NORD3, in
  `CaskaydiaCove_SemiBold_20` instead of `CaskaydiaCove_Regular_14`. It is
  placed below the number with a positive gap instead of the current -4 px
  overlap. The panel height grows if needed so nothing clips; the space below
  it (down to the battery bar) is free.
- The number's idle/active colours, the graph and the divider are unchanged.

## Approaches

- **Hide the inactive rows in the existing roller (chosen).** Keeps the slide
  animation and the existing layer-change wiring; a small change.
- Replace the roller with a single label. Simpler drawing, but it loses the
  animation and means rewriting widget code that works. Rejected.

## Verification

- Firmware builds for the dongle (WPM panel on) and both halves.
- On the device:
  - only the active layer shows, a bit higher
  - indicators still sit level with the layer name
  - the WPM number and legend don't overlap, and the legend reads clearly
    at a glance
