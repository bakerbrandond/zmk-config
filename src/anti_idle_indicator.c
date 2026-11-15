/*
 * Anti-idle (mouse jiggler) indicator for the Prospector display.
 *
 * The Prospector module owns the status screen. Instead of forking it, the
 * linker wraps its zmk_widget_battery_bar_init(), which the module calls once
 * with the screen as parent (see CMakeLists.txt). We call the real one, then
 * add a small icon to that screen. (zmk_display_status_screen() itself can't
 * be wrapped: ZMK calls it from the same file that defines a weak fallback,
 * and --wrap only redirects cross-object references.) The anti-idle module
 * raises no event on toggle, so an LVGL timer polls its state. The timer runs
 * inside the display thread, so touching LVGL objects from it is safe.
 */

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <anti_idle_generic.h>

#define POLL_MS 500
#define COLOR_ON 0x00ffe5  // same teal as the Prospector caps word indicator
#define COLOR_OFF 0x202020 // same dim grey as an inactive indicator

struct zmk_widget_battery_bar;
int __real_zmk_widget_battery_bar_init(struct zmk_widget_battery_bar *widget, lv_obj_t *parent);

static lv_obj_t *icon;
static int last_state = -1;

static void update_icon(lv_timer_t *timer) {
    bool on = false;
    if (zmk_anti_idle_get_state(&on) < 0 || (int)on == last_state) {
        return;
    }
    last_state = on;
    lv_obj_set_style_text_color(icon, lv_color_hex(on ? COLOR_ON : COLOR_OFF), LV_PART_MAIN);
}

int __wrap_zmk_widget_battery_bar_init(struct zmk_widget_battery_bar *widget, lv_obj_t *screen) {
    int ret = __real_zmk_widget_battery_bar_init(widget, screen);

    icon = lv_label_create(screen);
    lv_label_set_text(icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_OFF), LV_PART_MAIN);
    // Right column, level with the active layer name. The Prospector layer
    // roller is aligned LEFT_MID at y = -20 and draws its selected row at its
    // vertical center, so y = -20 puts the icon on the same line.
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, -14, -20);

    update_icon(NULL);
    lv_timer_create(update_icon, POLL_MS, NULL);

    return ret;
}
