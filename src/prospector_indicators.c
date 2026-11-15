/*
 * Extra indicators for the Prospector display, in the right-hand column:
 *   - anti-idle (mouse jiggler): refresh icon, level with the active layer,
 *     teal when on
 *   - outputs, below the jiggle icon: a USB icon and a Bluetooth icon with
 *     the active profile number (always shown, 1-based). Each is white while
 *     it's the output in use, grey while connected but not in use, and dim
 *     while not connected. ZMK only switches with &out when both are ready.
 *
 * The Prospector module owns the status screen. Instead of forking it, the
 * linker wraps its zmk_widget_battery_bar_init(), which the module calls once
 * with the screen as parent (see CMakeLists.txt). We call the real one, then
 * add our labels to that screen. (zmk_display_status_screen() itself can't
 * be wrapped: ZMK calls it from the same file that defines a weak fallback,
 * and --wrap only redirects cross-object references.)
 *
 * The anti-idle module raises no event on toggle, so an LVGL timer polls
 * state; the output indicator shares that timer. The timer runs inside the
 * display thread, so touching LVGL objects from it is safe.
 */

#include <lvgl.h>
#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_PROSPECTOR_ANTI_IDLE_INDICATOR)
#include <anti_idle_generic.h>
#endif
#if IS_ENABLED(CONFIG_PROSPECTOR_OUTPUT_INDICATOR)
#include <zmk/endpoints.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif
#endif

#define POLL_MS 500
// Nord palette, matching theme.h in bakerbrandond/prospector-zmk-module
// (that header is private to the module's build, so the values live here too)
#define COLOR_ON 0x8FBCBB     // nord7 frost teal: jiggle on
#define COLOR_OFF 0x3B4252    // nord1: jiggle off
#define COLOR_ACTIVE 0x88C0D0 // nord8 frost: output currently in use
#define COLOR_TEXT 0x81A1C1   // nord9 frost: output connected, not in use
#define COLOR_DIM 0x434C5E    // nord2: output not connected

// The Prospector layer roller draws its selected row at its vertical center;
// the module exports where that is (it moves up when the WPM panel is on).
#define RIGHT_X (-14)
#ifdef CONFIG_PROSPECTOR_ACTIVE_LAYER_Y
#define ACTIVE_LAYER_Y CONFIG_PROSPECTOR_ACTIVE_LAYER_Y
#else
#define ACTIVE_LAYER_Y (-20)
#endif
#define ROW_GAP 32 // vertical spacing between our indicators
#define INDICATOR_Y_SHIFT (-16) // stack sits a little above the layer name's level

struct zmk_widget_battery_bar;
int __real_zmk_widget_battery_bar_init(struct zmk_widget_battery_bar *widget, lv_obj_t *parent);

#if IS_ENABLED(CONFIG_PROSPECTOR_ANTI_IDLE_INDICATOR)
static lv_obj_t *jiggle_icon;
static int last_jiggle = -1;

static void update_jiggle(void) {
    bool on = false;
    if (zmk_anti_idle_get_state(&on) < 0 || (int)on == last_jiggle) {
        return;
    }
    last_jiggle = on;
    lv_obj_set_style_text_color(jiggle_icon, lv_color_hex(on ? COLOR_ON : COLOR_OFF),
                                LV_PART_MAIN);
}
#endif

#if IS_ENABLED(CONFIG_PROSPECTOR_OUTPUT_INDICATOR)
static lv_obj_t *usb_label;
static lv_obj_t *ble_label;

static void set_color(lv_obj_t *label, uint32_t color, uint32_t *last) {
    if (color != *last) {
        *last = color;
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    }
}

static uint32_t output_color(bool in_use, bool ready) {
    return in_use ? COLOR_ACTIVE : (ready ? COLOR_TEXT : COLOR_DIM);
}

static void update_output(void) {
    static uint32_t last_usb_color, last_ble_color;
    static int last_profile = -1;
    enum zmk_transport selected = zmk_endpoints_selected().transport;

#if IS_ENABLED(CONFIG_ZMK_USB)
    bool usb_ready = zmk_usb_is_hid_ready();
#else
    bool usb_ready = false;
#endif
    set_color(usb_label, output_color(usb_ready && selected == ZMK_TRANSPORT_USB, usb_ready),
              &last_usb_color);

#if IS_ENABLED(CONFIG_ZMK_BLE)
    int profile = zmk_ble_active_profile_index();
    bool ble_ready = zmk_ble_active_profile_is_connected();
    if (profile != last_profile) {
        last_profile = profile;
        // 1-based, like ZMK's built-in output widget (BT_SEL 0 shows "1")
        lv_label_set_text_fmt(ble_label, LV_SYMBOL_BLUETOOTH "%d", profile + 1);
    }
    set_color(ble_label, output_color(ble_ready && selected == ZMK_TRANSPORT_BLE, ble_ready),
              &last_ble_color);
#endif
}
#endif

static void poll(lv_timer_t *timer) {
#if IS_ENABLED(CONFIG_PROSPECTOR_ANTI_IDLE_INDICATOR)
    update_jiggle();
#endif
#if IS_ENABLED(CONFIG_PROSPECTOR_OUTPUT_INDICATOR)
    update_output();
#endif
}

static lv_obj_t *make_label(lv_obj_t *screen, const char *text, uint32_t color, lv_coord_t y) {
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, RIGHT_X, y);
    return label;
}

int __wrap_zmk_widget_battery_bar_init(struct zmk_widget_battery_bar *widget, lv_obj_t *screen) {
    int ret = __real_zmk_widget_battery_bar_init(widget, screen);

#if IS_ENABLED(CONFIG_PROSPECTOR_ANTI_IDLE_INDICATOR)
    jiggle_icon = make_label(screen, LV_SYMBOL_REFRESH, COLOR_OFF, ACTIVE_LAYER_Y + INDICATOR_Y_SHIFT);
#endif
#if IS_ENABLED(CONFIG_PROSPECTOR_OUTPUT_INDICATOR)
    usb_label = make_label(screen, LV_SYMBOL_USB, COLOR_DIM, ACTIVE_LAYER_Y + INDICATOR_Y_SHIFT + ROW_GAP);
    ble_label = make_label(screen, "", COLOR_DIM, ACTIVE_LAYER_Y + INDICATOR_Y_SHIFT + 2 * ROW_GAP);
#endif

    poll(NULL);
    lv_timer_create(poll, POLL_MS, NULL);

    return ret;
}
