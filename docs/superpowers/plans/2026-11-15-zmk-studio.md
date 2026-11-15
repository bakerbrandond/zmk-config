# ZMK Studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Spec:** `docs/superpowers/specs/2026-11-15-zmk-studio-design.md`

## Global Constraints

- 36-key Corne: never bind positions 0, 11, 12, 23, 24 or 35.
- Fork and config are both on `main`. Push only when the user asks.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

### Task 1: Roller name list (host-tested)

Files: create `boards/shields/prospector_adapter/src/widgets/roller_names.{h,c}`
and `tests/roller/{test_roller.c,Makefile}`.

Produces:

```c
/* Builds "\n"-joined visible layer names into buf (size len) from names[0..n)
 * (NULL/"" = unnamed -> its index as text), skipping any equal to hidden
 * (may be NULL). Returns the visible row of `active`, or -1 if active is
 * hidden/out of range. all_caps uppercases names. */
int roller_build(const char *const *names, int n, int active, const char *hidden,
                 bool all_caps, char *buf, size_t len);
```

Tests:
- `test_all_visible`: ["Default", "Lower"] with active 1 gives "Default\nLower" and returns 1.
- `test_hidden_skipped`: ["A", "Zappy", "B"] with hidden "Zappy" and active 2 gives "A\nB" and returns 1.
- `test_active_hidden`: active 1 (Zappy) returns -1.
- `test_unnamed_uses_index`: [NULL, "X"] gives "0\nX".
- `test_caps`: all_caps gives "DEFAULT".
- `test_truncates_safely`: a 10-byte buffer stays NUL-terminated and doesn't overflow.

Steps:
- [ ] Write the tests.
- [ ] Run them and confirm they fail.
- [ ] Implement it.
- [ ] Run them and confirm `ok 6 tests`.
- [ ] Commit.

### Task 2: Wire the roller, the hard-mode pool, and Studio

- **Roller:**
  - The update callback rebuilds the options with `roller_build` on every layer change.
  - It keeps the current selection when -1 is returned.
  - Kconfig: `PROSPECTOR_ROLLER_HIDDEN_LAYER_NAME` (string, default `"Zappy"`
    if `PROSPECTOR_ZAPPY_BIRDS`, else `""`). Remove `PROSPECTOR_ROLLER_HIDDEN_TRAILING_LAYERS`.
- **Zappy:** `show_game` calls `build_pool()` every time.
- **Config:**
  - `corne_dongle.conf`: add `CONFIG_ZMK_STUDIO=y`.
  - `build.yaml`: add `snippet: studio-rpc-usb-uart` to the dongle entry.
  - `scripts/build-local.sh`: add the same snippet for the dongle target, merged
    with any `-DSNIPPET=` in the extra args.
  - Keymap: Function-layer position 31 → `&studio_unlock`.

Steps:
- [ ] Build: `scripts/build-local.sh all`. Expected: exit 0.
- [ ] Check RAM.
- [ ] Run all host suites.
- [ ] Commit both repos.

### Task 3: Device check with Studio (with the user)

- [ ] Run the spec's on-device verification list.
