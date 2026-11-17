/*
 * Battery and connection icons for the nice!view on each Corne half, in
 * CaskaydiaCove Nerd Font glyphs instead of the stock drawn battery bar and
 * Montserrat wifi / close symbols.
 *
 * Upright, the strip is the top 20 rows of the screen (68 px across):
 *   [battery glyph][percentage]                    [connection glyph]
 * Battery: Material Design battery-outline, -10 … -90, battery, swapped for
 * the battery-charging-* glyph of the same level while on USB power.
 * Connection: check-circle-outline while this half is connected to the
 * central (the Prospector dongle), close-circle-outline when it isn't.
 * Each glyph's ink is centred in the 20 rows.
 *
 * Off by default (CONFIG_NICE_VIEW_SNOW_STATUS): the Prospector dongle
 * already shows battery and connection for both halves.
 *
 * Hooking: src/nice_view_snow.c (the wrapped zmk_widget_status_init) hides the
 * stock status canvas and calls nice_view_status_create() after creating its
 * own scene canvas, so this strip sits on top of the scene's top 20 rows. The stock widget keeps listening
 * and drawing into its hidden canvas; this file has its own listeners.
 *
 * Orientation: LVGL sees a 160x68 landscape screen; upright row py (0..19
 * from the top) and column px (0..67) live at LVGL (x = 159 - py, y = px).
 * The strip's canvas covers LVGL x 140..159.
 */

#include <stdio.h>

#include <lvgl.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "nice_view_status.h"

LV_FONT_DECLARE(nv_status_icons_20);
LV_FONT_DECLARE(nv_status_digits_16);

#define STRIP_W 68 // upright columns
#define STRIP_H 20 // upright rows
#define GAP 3      // between the battery glyph and the percentage

#define FG (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_white() : lv_color_black())
#define BG (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_black() : lv_color_white())

// Indexed by level 0..10 (round(percent / 10))
static const uint32_t battery_glyphs[11] = {
    0xF008E, 0xF007A, 0xF007B, 0xF007C, 0xF007D, 0xF007E,
    0xF007F, 0xF0080, 0xF0081, 0xF0082, 0xF0079,
};
static const uint32_t charging_glyphs[11] = {
    0xF089F, 0xF089C, 0xF0086, 0xF0087, 0xF0088, 0xF089D,
    0xF0089, 0xF089E, 0xF008A, 0xF008B, 0xF0085,
};
#define GLYPH_CONNECTED 0xF05E1    // check-circle-outline
#define GLYPH_DISCONNECTED 0xF015A // close-circle-outline

static lv_obj_t *canvas;
static lv_color_t cbuf[STRIP_H * STRIP_W]; // LVGL layout: STRIP_H wide (x), STRIP_W tall (y)

static struct {
    uint8_t level;
    bool charging;
    bool connected;
} state;

static inline void put_px(int px, int py, lv_color_t c) {
    if (px < 0 || px >= STRIP_W || py < 0 || py >= STRIP_H) {
        return;
    }
    cbuf[px * STRIP_H + (STRIP_H - 1 - py)] = c;
}

/*
 * Blit one glyph with its ink's left edge at upright column left, centred in
 * the strip's rows. Fonts are 1 bpp and uncompressed, so the bitmap is
 * box_w * box_h bits, MSB first, rows not padded. Returns the ink width, or
 * -1 if the font lacks the glyph.
 */
static int draw_glyph(const lv_font_t *font, uint32_t letter, int left) {
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(font, &g, letter, 0)) {
        return -1;
    }
    const uint8_t *bits = lv_font_get_glyph_bitmap(font, letter);
    if (bits == NULL) {
        return -1;
    }
    int top = (STRIP_H - g.box_h) / 2;
    for (int y = 0; y < g.box_h; y++) {
        for (int x = 0; x < g.box_w; x++) {
            uint32_t i = (uint32_t)y * g.box_w + x;
            if ((bits[i / 8] >> (7 - i % 8)) & 1) {
                put_px(left + x, top + y, FG);
            }
        }
    }
    return g.box_w;
}

static int glyph_width(const lv_font_t *font, uint32_t letter) {
    lv_font_glyph_dsc_t g;
    return lv_font_get_glyph_dsc(font, &g, letter, 0) ? g.box_w : 0;
}

// Digits laid out by their advance widths; the first digit's ink starts at left
static void draw_number(int value, int left) {
    char text[4];
    int n = snprintf(text, sizeof(text), "%d", value);
    lv_font_glyph_dsc_t g;
    int pen = left;
    for (int i = 0; i < n; i++) {
        if (!lv_font_get_glyph_dsc(&nv_status_digits_16, &g, text[i], 0)) {
            continue;
        }
        if (i == 0) {
            pen -= g.ofs_x;
        }
        draw_glyph(&nv_status_digits_16, text[i], pen + g.ofs_x);
        pen += g.adv_w;
    }
}

static void render(void) {
    if (canvas == NULL) {
        return;
    }
    for (int i = 0; i < STRIP_H * STRIP_W; i++) {
        cbuf[i] = BG;
    }

    int level = (MIN(state.level, 100) + 5) / 10;
    uint32_t battery = (state.charging ? charging_glyphs : battery_glyphs)[level];
    int w = draw_glyph(&nv_status_icons_20, battery, 0);
    draw_number(state.level, (w > 0 ? w : 0) + GAP);

    uint32_t conn = state.connected ? GLYPH_CONNECTED : GLYPH_DISCONNECTED;
    draw_glyph(&nv_status_icons_20, conn, STRIP_W - glyph_width(&nv_status_icons_20, conn));

    lv_obj_invalidate(canvas);
}

struct battery_state {
    uint8_t level;
    bool charging;
};

static void battery_update(struct battery_state s) {
    state.level = s.level;
    state.charging = s.charging;
    render();
}

static struct battery_state battery_get(const zmk_event_t *eh) {
    return (struct battery_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .charging = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(nv_status_battery, struct battery_state, battery_update, battery_get)
ZMK_SUBSCRIPTION(nv_status_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(nv_status_battery, zmk_usb_conn_state_changed);
#endif

struct conn_state {
    bool connected;
};

static void conn_update(struct conn_state s) {
    state.connected = s.connected;
    render();
}

static struct conn_state conn_get(const zmk_event_t *eh) {
    return (struct conn_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(nv_status_conn, struct conn_state, conn_update, conn_get)
ZMK_SUBSCRIPTION(nv_status_conn, zmk_split_peripheral_status_changed);

void nice_view_status_create(lv_obj_t *parent) {
    canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, cbuf, STRIP_H, STRIP_W, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    nv_status_battery_init();
    nv_status_conn_init();
}
