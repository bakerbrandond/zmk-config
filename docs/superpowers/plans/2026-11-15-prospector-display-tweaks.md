# Prospector Display Tweaks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The status screen shows only the active layer, about 20 px higher, and the WPM legend is readable and doesn't overlap the number.

**Architecture:** Style-only changes to two existing widgets in the Prospector fork (layer roller and WPM panel), plus a Kconfig default.

**Tech Stack:** C, LVGL 8.3, Kconfig; `scripts/build-local.sh`.

**Spec:** `docs/superpowers/specs/2026-11-15-prospector-display-tweaks-design.md`

## Global Constraints

- Fork `~/repos/zmk/prospector-zmk-module` on branch `nord`, config `~/repos/zmk/zmk-config` on branch `jiggler`. These are local branches; never push without the user's go-ahead.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Values from the spec:
  - `PROSPECTOR_ACTIVE_LAYER_Y` with the WPM panel: -62
  - `THEME_WPM_LABEL`: NORD4
  - `THEME_FONT_WPM_LABEL`: `CaskaydiaCove_SemiBold_20`
- No host-testable logic: verification is the build plus the device check. Firmware is UI-only, with nothing to unit test.

## Review Focus

1. **Layer change animation:** switching layers must still slide the new name in, with no blank frame and no ghost of the old name. Owner: Task 1 device check.
2. **Indicators alignment:** the jiggle, USB and BT indicators stay level with the layer name at the new height. Owner: Task 1 device check.
3. **3-digit WPM:** "120" plus the legend fits beside the graph without clipping. Owner: Task 1 device check (type fast, or check the layout math: number 23 px line + 4 px gap + legend 22 px + 6 px pad = 55 px ≤ 64 px panel).

---

### Task 1: Hide inactive layers, raise the layer, fix the WPM legend

**Files (fork):**
- `boards/shields/prospector_adapter/src/widgets/layer_roller.c`: set `lv_obj_set_style_text_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN)`, remove the `mask_event_cb` function and its `lv_obj_add_event_cb` registration.
- `Kconfig`: `PROSPECTOR_ACTIVE_LAYER_Y` `default -62 if PROSPECTOR_WPM_PANEL` (was -42), and update its help text.
- `boards/shields/prospector_adapter/include/theme.h`: `THEME_WPM_LABEL NORD4`, `THEME_FONT_WPM_LABEL CaskaydiaCove_SemiBold_20`.
- `boards/shields/prospector_adapter/src/widgets/wpm_panel.c`: legend aligned `LV_ALIGN_OUT_BOTTOM_LEFT` to the number with offsets `(2, 4)` instead of `(2, -4)`.
- `boards/shields/prospector_adapter/src/custom_status_screen.c`: update the comment that mentions the faded previous layer and the panel covering the next row.

- [ ] **Step 1: Make the edits above.**
- [ ] **Step 2: Build.** Run `scripts/build-local.sh all`. Expected: exit 0, no new warnings from `layer_roller.c` or `wpm_panel.c`. Check that `CONFIG_PROSPECTOR_ACTIVE_LAYER_Y -62` is in the dongle's `autoconf.h`.
- [ ] **Step 3: Commit (fork).** Message: `Prospector: hide inactive layers, raise active layer, readable WPM legend`.
- [ ] **Step 4: Device check with the user.** Flash the dongle uf2 when the user puts it in bootloader mode, then confirm:
  - Focus 1: switching layers slides the new name in, with no ghost of the old one.
  - Focus 2: the indicators sit level with the layer name.
  - Focus 3: the WPM number and legend don't overlap at 3 digits, and the legend reads clearly.
  - Tune the -62 height if the user asks.
