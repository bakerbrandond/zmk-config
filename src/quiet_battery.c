/*
 * Quiet dongle battery: keep the dongle's Bluetooth battery level fixed.
 *
 * ZMK samples the central's battery when it wakes from idle and on a timer,
 * and pushes any change to the Battery Service, which notifies every
 * connected computer. A dongle with no battery reads noise, so it sent a
 * "new" level on most wakes, and that notification woke a sleeping Mac
 * paired on another profile. Wrapping the one call that does it (in
 * zmk/app/src/battery.c) leaves the service in place, so computers don't
 * need to re-pair, but it never notifies.
 */

#include <stdint.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

int __wrap_bt_bas_set_battery_level(uint8_t level) {
    LOG_DBG("Quiet dongle battery: not announcing %u%%", level);
    return 0;
}
