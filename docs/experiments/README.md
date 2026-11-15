# Experiments

## power-switch-soft-off.patch

Unfinished attempt (Feb 2026, formerly branch `power-switch`) to work around
the Corne halves' inverted on/off switch polarity in firmware: the switch is
read as a GPIO key on `&pro_micro 10` and mapped to ZMK soft-off
(`CONFIG_ZMK_PM_SOFT_OFF`), with the same pin as the wake source.

Caveat: this only works if the switch is wired to a GPIO. A switch in the
battery line cuts power outright, so firmware can't invert it. If it is on a
GPIO, the polarity flags (`GPIO_ACTIVE_HIGH | GPIO_PULL_UP`) are the likely
thing to change, e.g. to `GPIO_ACTIVE_LOW`.

The patch was made against `e6a37b4` (Legacy port), and the keymap has changed
a lot since. Apply it with `git am -3 docs/experiments/power-switch-soft-off.patch`
and expect to resolve conflicts in `config/corne.keymap`.
