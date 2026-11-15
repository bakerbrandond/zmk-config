/*
 * Extra indicators for the Prospector display, in the right-hand column:
 *   - anti-idle (mouse jiggler): refresh icon, level with the active layer,
 *     teal when on
 *   - active output: USB icon, or Bluetooth icon + profile number (dimmed
 *     while that profile has no host connected), below the jiggle icon
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

#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_PROSPECTOR_ANTI_IDLE_INDICATOR)
#include <anti_idle_generic.h>
#endif
#if IS_ENABLED(CONFIG_PROSPECTOR_OUTPUT_INDICATOR)
#include <zmk/endpoints.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif
#endif

#define POLL_MS 500
#define COLOR_ON 0x00ffe5  // same teal as the Prospector caps word indicator
#define COLOR_OFF 0x202020 // same dim grey as an inactive indicator
#define COLOR_TEXT 0x909090 // same grey as inactive layer names
#define COLOR_DIM 0x404040  // Bluetooth profile selected but not connected

// The Prospector layer roller is aligned LEFT_MID at y = -20 and draws its
// selected row at its vertical center, so y = -20 is the active layer's line.
#define RIGHT_X (-14)
#define ACTIVE_LAYER_Y (-20)
#define ROW_GAP 32 // vertical spacing between our indicators

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
static lv_obj_t *output_label;
static char last_output[16];
static uint32_t last_output_color;

static void update_output(void) {
    char text[16];
    uint32_t color = COLOR_TEXT;
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();

    switch (ep.transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), LV_SYMBOL_USB);
        break;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        // 1-based, like ZMK's built-in output widget (BT_SEL 0 shows "1")
        snprintf(text, sizeof(text), LV_SYMBOL_BLUETOOTH "%d", ep.ble.profile_index + 1);
        if (!zmk_ble_profile_is_connected(ep.ble.profile_index)) {
            color = COLOR_DIM;
        }
        break;
#endif
    default:
        text[0] = '\0';
        break;
    }

    if (strcmp(text, last_output) != 0) {
        strcpy(last_output, text);
        lv_label_set_text(output_label, text);
    }
    if (color != last_output_color) {
        last_output_color = color;
        lv_obj_set_style_text_color(output_label, lv_color_hex(color), LV_PART_MAIN);
    }
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
    jiggle_icon = make_label(screen, LV_SYMBOL_REFRESH, COLOR_OFF, ACTIVE_LAYER_Y);
#endif
#if IS_ENABLED(CONFIG_PROSPECTOR_OUTPUT_INDICATOR)
    output_label = make_label(screen, "", COLOR_TEXT, ACTIVE_LAYER_Y + ROW_GAP);
#endif

    poll(NULL);
    lv_timer_create(poll, POLL_MS, NULL);

    return ret;
}
