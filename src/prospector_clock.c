/*
 * 24-hour clock on the Prospector display, above the active layer name.
 *
 * The dongle has no real-time clock, so the time comes from the computer: a
 * companion app (qmk-hid-host on Windows/macOS/Linux, or scripts/hid-clock.py
 * on Linux) sends [0xAA, hour, minute] over Raw HID (zzeneg/zmk-raw-hid, USB
 * or Bluetooth) whenever the minute changes. The clock counts on from the
 * last packet and hides itself when none has arrived for
 * CONFIG_PROSPECTOR_CLOCK_TIMEOUT_S, e.g. when the app isn't running or the
 * dongle is switched to a computer without it. See src/clock_core.h.
 *
 * Packets arrive on the USB or Bluetooth thread; they only update the state
 * under a spinlock. An LVGL timer (display thread) redraws the label.
 */

#include <lvgl.h>
#include <zephyr/kernel.h>

#include <raw_hid/events.h>
#include <zmk/event_manager.h>

#include "clock_core.h"
#include "prospector_clock.h"

LV_FONT_DECLARE(CaskaydiaCove_SemiBold_20);

#define TICK_MS 1000
#define COLOR_TEXT 0xD8DEE9 // nord4, like the battery numbers
// Centred over the active layer name: the layer roller is 224 px wide on the
// left of the 280 px screen, and the name's centre sits at ACTIVE_LAYER_Y
#define CLOCK_X (112 - 140)
#define CLOCK_Y (CONFIG_PROSPECTOR_ACTIVE_LAYER_Y - 41)

static struct k_spinlock lock;
static struct clock_state state;
static lv_obj_t *label;
static int shown = CLOCK_HIDDEN - 1; // forces the first redraw

static int raw_hid_listener(const zmk_event_t *eh) {
    const struct raw_hid_received_event *ev = as_raw_hid_received_event(eh);
    uint8_t hour, minute;
    if (ev != NULL && clock_parse(ev->data, ev->length, &hour, &minute)) {
        k_spinlock_key_t key = k_spin_lock(&lock);
        clock_set(&state, hour, minute, k_uptime_get());
        k_spin_unlock(&lock, key);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(prospector_clock, raw_hid_listener);
ZMK_SUBSCRIPTION(prospector_clock, raw_hid_received_event);

static void tick(lv_timer_t *timer) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    int minutes = clock_minutes(&state, k_uptime_get(),
                                (int64_t)CONFIG_PROSPECTOR_CLOCK_TIMEOUT_S * 1000);
    k_spin_unlock(&lock, key);

    if (minutes == shown) {
        return;
    }
    shown = minutes;
    if (minutes == CLOCK_HIDDEN) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char text[6];
    clock_format(minutes, text);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void prospector_clock_create(lv_obj_t *screen) {
    clock_init(&state);
    label = lv_label_create(screen);
    lv_obj_set_style_text_font(label, &CaskaydiaCove_SemiBold_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(label, "00:00");
    lv_obj_align(label, LV_ALIGN_CENTER, CLOCK_X, CLOCK_Y);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    tick(NULL);
    lv_timer_create(tick, TICK_MS, NULL);
}
