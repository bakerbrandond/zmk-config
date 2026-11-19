/*
 * Strict output: send key, consumer and mouse reports only to the output the
 * user selected (Function + L, or the &out behaviors).
 *
 * Stock ZMK falls back to the other output when the selected one isn't
 * connected: with Bluetooth selected and its host gone, keys go to whatever
 * the dongle is plugged into over USB, and a sleeping USB host is even woken
 * up for them. Here those reports are dropped instead, until the selected
 * output is back.
 *
 * ZMK keeps the selection private, so this follows it: the saved value is read
 * once from settings, and the select/toggle calls are wrapped at link time.
 * The per-transport send functions are wrapped too, which is where reports
 * for the wrong output are dropped. Everything inside endpoints.c (including
 * releasing held keys when the output changes) goes through those, so a
 * stray release isn't sent to the wrong host either.
 */

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zmk/endpoints.h>
#include <zmk/hid.h>

#include "strict_output.h"
#include "strict_output_core.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ZMK's own default before anything is saved (app/src/endpoints.c)
static atomic_t selected = ATOMIC_INIT(ZMK_TRANSPORT_USB);
static atomic_t loaded = ATOMIC_INIT(0);

// Settings storage is set up in ZMK's main(). If nothing is saved yet, the
// lookup is retried until this long after boot, then ZMK's default stands.
#define LOAD_SETTLE_MS 5000

static int load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                   void *param) {
    if (!settings_name_steq(key, "preferred", NULL)) {
        return 0;
    }
    uint8_t buf[8];
    ssize_t n = read_cb(cb_arg, buf, MIN(len, sizeof(buf)));
    int value;
    if (n > 0 && strict_output_decode(buf, (size_t)n, sizeof(enum zmk_transport), &value)) {
        atomic_set(&selected, value);
        *(bool *)param = true;
    }
    return 0;
}

// Read the saved selection the first time it's needed
static void ensure_loaded(void) {
    if (atomic_get(&loaded)) {
        return;
    }
    bool found = false;
    int err = settings_load_subtree_direct("endpoints", load_cb, &found);
    if (found || (err == 0 && k_uptime_get() > LOAD_SETTLE_MS)) {
        atomic_set(&loaded, 1);
        LOG_DBG("Strict output: selected transport %d", (int)atomic_get(&selected));
    }
}

enum zmk_transport strict_output_selected(void) {
    ensure_loaded();
    return (enum zmk_transport)atomic_get(&selected);
}

static bool allowed(enum zmk_transport sending) {
    bool ok = strict_output_allows(sending, strict_output_selected());
    if (!ok) {
        LOG_DBG("Strict output: dropping report for transport %d", sending);
    }
    return ok;
}

// Selection changes

int __real_zmk_endpoints_select_transport(enum zmk_transport transport);
int __wrap_zmk_endpoints_select_transport(enum zmk_transport transport) {
    ensure_loaded();
    atomic_set(&selected, transport);
    return __real_zmk_endpoints_select_transport(transport);
}

int __real_zmk_endpoints_toggle_transport(void);
int __wrap_zmk_endpoints_toggle_transport(void) {
    ensure_loaded();
    atomic_set(&selected, strict_output_toggled(atomic_get(&selected), ZMK_TRANSPORT_USB,
                                                ZMK_TRANSPORT_BLE));
    return __real_zmk_endpoints_toggle_transport();
}

// Sends. Dropped reports return 0 so ZMK doesn't log them as failures.

int __real_zmk_usb_hid_send_keyboard_report(void);
int __wrap_zmk_usb_hid_send_keyboard_report(void) {
    return allowed(ZMK_TRANSPORT_USB) ? __real_zmk_usb_hid_send_keyboard_report() : 0;
}

int __real_zmk_usb_hid_send_consumer_report(void);
int __wrap_zmk_usb_hid_send_consumer_report(void) {
    return allowed(ZMK_TRANSPORT_USB) ? __real_zmk_usb_hid_send_consumer_report() : 0;
}

int __real_zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *report);
int __wrap_zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *report) {
    return allowed(ZMK_TRANSPORT_BLE) ? __real_zmk_hog_send_keyboard_report(report) : 0;
}

int __real_zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *report);
int __wrap_zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *report) {
    return allowed(ZMK_TRANSPORT_BLE) ? __real_zmk_hog_send_consumer_report(report) : 0;
}

#if IS_ENABLED(CONFIG_ZMK_POINTING)
int __real_zmk_usb_hid_send_mouse_report(void);
int __wrap_zmk_usb_hid_send_mouse_report(void) {
    return allowed(ZMK_TRANSPORT_USB) ? __real_zmk_usb_hid_send_mouse_report() : 0;
}

int __real_zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *report);
int __wrap_zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *report) {
    return allowed(ZMK_TRANSPORT_BLE) ? __real_zmk_hog_send_mouse_report(report) : 0;
}
#endif
