# ZMK Studio (short design)

Date: 2026-11-15
Status: approved ("position 31, build it"; layout: keep 6-column)

## Intent

Edit the keymap live from ZMK Studio, with no rebuild or reflash, while every
feature built so far keeps working. Stay on ZMK v0.3 until the next release.

## Decided (by the user)

- Studio first; move off v0.3 only when ZMK cuts a new release.
- The `&studio_unlock` key is on the Function layer at position 31 (the M spot).
- Keep the 6-column (42-position) physical layout. Studio shows the 6 phantom
  outer keys, and the layout is not switched inside Studio, because combo
  positions are not remapped.

## Design

- **Dongle only** (it is the central and owns the keymap).
  - Add `CONFIG_ZMK_STUDIO=y` to `config/corne_dongle.conf`.
  - Add the `studio-rpc-usb-uart` snippet to the dongle entry in `build.yaml`.
  - The local build script adds the same snippet for the dongle target.
- **Layer roller:**
  - The options are rebuilt on every layer change, so Studio renames and
    reorders show up without a reboot.
  - The layer whose name equals `CONFIG_PROSPECTOR_ROLLER_HIDDEN_LAYER_NAME`
    (default `"Zappy"` when Zappy Birds is enabled) is skipped. This replaces
    "hide trailing N layers".
  - The list-building logic is pure C and host-tested.
- **Zappy hard mode:** the letter pool is rebuilt at every game start instead of
  once, so keys remapped in Studio are picked up.
- **Unchanged:** `&zappy ZAP` passes a layer id, and Studio keeps ids stable
  across reorder.

## Approaches

- **Rebuild the roller on each layer change (chosen).** Correct after any Studio
  edit, at negligible cost.
- Subscribe to a keymap-changed event. Rejected: none exists in v0.3.

## Verification

- Host tests: roller name list and selection (skips the hidden layer, handles
  the active layer being hidden, unnamed layers, reordering).
- Builds: all targets. Check the dongle's RAM still fits.
- On the device, with Studio:
  - unlock with the key
  - change a key and confirm it works
  - rename and reorder a layer and confirm the roller follows
  - Zappy stays hidden
  - hard mode uses a remapped letter
