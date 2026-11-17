/*
 * Snowfall for the nice!view on each Corne half (split peripherals).
 *
 * Shows one of the stock nice!view scenes over the whole screen (the left
 * half gets the mountain, the right half the balloon, by default) and lets
 * snow fall in front of it. Each key press on this half drops a white flake
 * from above that key's column; flakes fall to the bottom of the frame and
 * build a drift there that follows where you type. On top of that a light snow falls all the
 * time while the keyboard is active, and it gets heavier the faster you type
 * (extra flakes anywhere, more of them big). The drift is limited in depth and
 * melt slowly once typing stops. When the keyboard goes idle the light snow
 * stops (these screens never blank, so it would otherwise run all night);
 * flakes still in the air land, the drift melts, then the LVGL timer pauses until
 * the keyboard is active again.
 *
 * Hooking: ZMK's nice_view custom_status_screen.c calls
 * zmk_widget_status_init() (defined in peripheral_status.c), so the linker
 * wraps it (see CMakeLists.txt). We call the real one, hide its art image
 * and its battery/connection canvas (the Prospector dongle shows both), and
 * draw our own canvas over the whole screen. With NICE_VIEW_SNOW_STATUS,
 * src/nice_view_status.c draws CaskaydiaCove battery/connection icons over
 * the top 20 rows instead.
 *
 * Orientation: the nice!view is mounted upright, but LVGL sees a 160x68
 * landscape screen; the stock widget draws its status square upright and
 * rotates it 90 degrees clockwise into the right-hand 68x68; the stock art
 * is 140x68 in LVGL coordinates, leaving the top 20 upright rows for the
 * icons. With those hidden, the scene is stretched to 160 rows by drawing
 * the art's top band of sky (rows 2..21, under the 2-row frame top) twice,
 * so the frame and sky reach the top of the screen. In upright terms (px across
 * 0..67, py down 0..159) a pixel lives at LVGL (x = 159 - py, y = px).
 */

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/activity.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>

#include "nice_view_status.h"

// Upright art area
#define ART_W 68
#define ART_H 140              // stock art rows
#define EXT 20                 // extra sky rows where the stock icons were
#define SCENE_H (ART_H + EXT)  // upright rows on screen
#define FLOOR (SCENE_H - 1)    // the frame's bottom line; the drift sits on it

#define FRAME_MS 50          // ~20 fps while flakes are falling
#define MELT_MS 2000         // snow caps lose 1 px per column this often when idle
#define IDLE_BEFORE_MELT_MS 5000
#define MAX_FLAKES 64
#define SNOW_MIN 1           // a dusting always remains at the bottom
#define SNOW_MAX 10


// Light snow: one flake per this many frames on average (~1.2 s at 20 fps)
#define LIGHT_FRAMES 24
// Typing heat: +HEAT_PER_KEY per key press, decays by 1/HEAT_DECAY per frame
// (~2 s time constant), so it settles near 2 * keys/s * HEAT_PER_KEY.
#define HEAT_PER_KEY 256
#define HEAT_DECAY 40
// Extra flakes per frame = heat / HEAT_FLAKE_DIV (8 keys/s -> ~4 extra/s)
#define HEAT_FLAKE_DIV (256 * 80)
#define HEAT_MAX (HEAT_PER_KEY * 32)

#define KEY_COLUMNS 6        // columns per half on a Corne
#define KEYMAP_COLUMNS 12    // columns in the full 6-column transform

#define SKY lv_color_black()
#define SNOW lv_color_white()

// Stock art (nice_view widgets/art.c): 140x68, 1 bit per pixel, 2-entry
// palette (8 bytes) then rows of 18 bytes, MSB first. Bit 1 is the light
// background of the stock artwork, bit 0 the ink.
LV_IMG_DECLARE(mountain);
LV_IMG_DECLARE(balloon);
#define ART_PALETTE_BYTES 8
#define ART_STRIDE 18

/*
 * Skylines in stock-art rows: for each column, the first row of the scene's
 * ground (ART_H = nothing). The night style draws its ridge line along it
 * and dithers below it. Derived offline from the stock bitmaps; if ZMK
 * changes the art, only the night style's ridge is affected.
 *   mountain: the big dark mountain plus the pine trees (bit-1 shapes whose
 *             top is below row 60), inside the frame.
 *   balloon:  the first pixel of any ink shape larger than 40 px, counting
 *             8-connected pixels that have an ink 4-neighbour (so dithered
 *             clouds and lone birds don't catch snow).
 */
static const uint8_t ground_mountain[ART_W] __maybe_unused = {
    140, 140, 140,  54,  53,  52,  51,  50,  50,  49,  48,  47,  47,  46,  45,  44,  43,
     43,  42,  41,  39,  38,  37,  36,  35,  32,  30,  29,  28,  27,  26,  24,  22,  21,
     20,  22,  25,  27,  28,  29,  30,  31,  33,  34,  35,  35,  35,  35,  36,  36,  43,
     44,  45,  46,  47,  48,  48,  49,  50,  51,  52,  52,  53,  54,  58, 140, 140, 140,
};
static const uint8_t ground_balloon[ART_W] __maybe_unused = {
    105, 103, 101, 100,  98,  97,  96,  98, 100, 102, 104, 106, 107, 108, 108, 109,  54,
     49,  38,  23,  20,  18,  17,  16,  15,  14,  13,  13,  12,  12,  12,  11,  11,  11,
     11,  11,  11,  11,  12,  12,  12,  13,  13,  14,  15,  16,  17,  18,  20,  23, 105,
    106, 106, 107, 107, 106, 106, 105, 104, 103, 101, 100,  99, 100, 102, 103, 104, 104,
};

enum scene_style {
    STYLE_INVERTED, // ink white on a black sky
    STYLE_DITHERED, // light parts dithered grey, dark parts black
    STYLE_NIGHT,    // black sky with stars and a moon, white ridge, dithered detail below
};

struct scene {
    const lv_img_dsc_t *art;
    const uint8_t *ground;
    enum scene_style style;
    uint8_t fall_x_min, fall_x_max;
};

#if IS_ENABLED(CONFIG_NICE_VIEW_SNOW_SCENE_BALLOON)
// Night sky: the light background goes black, the ink (balloon, clouds,
// mountains) white.
static const struct scene scene = {&balloon, ground_balloon, STYLE_INVERTED, 1, 66};
#elif IS_ENABLED(CONFIG_NICE_VIEW_SNOW_SCENE_MOUNTAIN_NIGHT)
// Night mountain: the sun's rays thin out into stars around a crescent moon,
// the skyline becomes a white ridge line, and the pines and the moonlit flank
// below it stay dithered grey on a black mountain.
static const struct scene scene = {&mountain, ground_mountain, STYLE_NIGHT, 3, 64};
#else
// The dark mountain stays black; sun rays, pines and frame turn dithered grey.
// Flakes fall between the frame's inner edges.
static const struct scene scene = {&mountain, ground_mountain, STYLE_DITHERED, 3, 64};
#endif

#define FALL_X_MIN (scene.fall_x_min)
#define FALL_X_MAX (scene.fall_x_max)

// Prefix of the stock struct zmk_widget_status (nice_view peripheral_status.h);
// only ->obj is used here.
struct zmk_widget_status_prefix {
    sys_snode_t node;
    lv_obj_t *obj;
};
int __real_zmk_widget_status_init(struct zmk_widget_status_prefix *widget, lv_obj_t *parent);

struct flake {
    int32_t x_q8; // upright x, 8.8 fixed point
    int32_t y_q8; // upright y, 8.8 fixed point (139 << 8 overflows int16)
    uint8_t speed_q8;
    bool big;     // 5x5 star instead of a 3x3 cross
};

static lv_obj_t *canvas;
static lv_color_t cbuf[SCENE_H * ART_W]; // LVGL layout: SCENE_H wide (x), ART_W tall (y)
static lv_timer_t *timer;

static struct flake flakes[MAX_FLAKES];
static int flake_count;
static uint8_t snow[ART_W]; // drift depth on the bottom of the frame, per column
static int64_t last_key_ms;
static int64_t last_melt_ms;

// Key presses arrive on another thread; the display thread drains these.
static atomic_t pending[KEY_COLUMNS];
static atomic_t active = ATOMIC_INIT(1); // keyboard not idle: light snow falls
static int32_t heat;                     // recent typing speed, see HEAT_*

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// Art pixel for scene row py: the frame top and the first EXT sky rows, then
// those sky rows again and the rest of the stock art
static inline bool art_bit(int px, int py) {
    int ay = py < 2 + EXT ? py : py - EXT;
    int lx = (ART_H - 1) - ay, ly = px;
    const uint8_t *rows = scene.art->data + ART_PALETTE_BYTES;
    return (rows[ly * ART_STRIDE + lx / 8] >> (7 - lx % 8)) & 1;
}

static inline void put_px(int px, int py, lv_color_t c) {
    if (px < 0 || px >= ART_W || py < 0 || py >= SCENE_H) {
        return;
    }
    cbuf[px * SCENE_H + (SCENE_H - 1 - py)] = c;
}

// Top of the drift in column x
static inline int top(int x) { return FLOOR - snow[x]; }

// Scene row of the ground's first row in column x (SCENE_H = none)
static inline int ground_y(int x) { return scene.ground[x] < ART_H ? scene.ground[x] + EXT : SCENE_H; }

// More big flakes as typing heats up: 1 in 5 when calm, up to about 1 in 2
static bool big_flake(void) { return (int32_t)(rnd() % (HEAT_MAX * 2)) < HEAT_MAX * 2 / 5 + heat / 2; }

// A flake above one key column
static void spawn(int column) {
    if (flake_count >= MAX_FLAKES) {
        return;
    }
    int span = FALL_X_MAX - FALL_X_MIN + 1;
    int col_w = span / KEY_COLUMNS;
    struct flake *f = &flakes[flake_count++];
    f->x_q8 = (FALL_X_MIN + column * col_w + (int)(rnd() % col_w)) << 8;
    f->y_q8 = 0;
    f->speed_q8 = 140 + rnd() % 116; // 0.55-1.0 px per frame (fits uint8_t)
    f->big = big_flake();
}

// A flake anywhere across the sky: small and slow for the light snow, like a
// key flake for the extra snow typing adds
static void spawn_anywhere(bool light) {
    if (flake_count >= MAX_FLAKES) {
        return;
    }
    struct flake *f = &flakes[flake_count++];
    f->x_q8 = (FALL_X_MIN + (int)(rnd() % (FALL_X_MAX - FALL_X_MIN + 1))) << 8;
    f->y_q8 = 0;
    f->speed_q8 = light ? 90 + rnd() % 70 : 140 + rnd() % 116; // light: 0.35-0.6 px per frame
    f->big = light ? false : big_flake();
}

// Settle a flake in column x, sliding it toward a lower neighbor on steep spots
static void land(int x) {
    for (int i = 0; i < 3; i++) {
        int left = x > FALL_X_MIN ? top(x - 1) : -1;
        int right = x < FALL_X_MAX ? top(x + 1) : -1;
        if (left > top(x) + 1 && left >= right) {
            x--;
        } else if (right > top(x) + 1) {
            x++;
        } else {
            break;
        }
    }
    if (snow[x] < SNOW_MAX && top(x) > 0) {
        snow[x]++;
    }
}

static void draw_flake(const struct flake *f) {
    int x = f->x_q8 >> 8, y = f->y_q8 >> 8;
    int arm = f->big ? 2 : 1;
    for (int d = -arm; d <= arm; d++) {
        put_px(x + d, y, SNOW);
        put_px(x, y + d, SNOW);
    }
}

#define MOON_X 50
#define MOON_Y 14
#define MOON_R 7
#define MOON_CUT 3 // the crescent's shadow disc is shifted this far right

static bool night_white(int px, int py, bool bit) {
    bool dither = bit && ((px + py) & 1) == 0;
    if (px < FALL_X_MIN || px > FALL_X_MAX || py < 2) {
        return dither; // frame
    }
    int gy = ground_y(px);
    if (py < gy) { // sky
        int dx = px - MOON_X, dy = py - (MOON_Y + EXT), cx = dx - MOON_CUT, cy = dy + 1;
        if (dx * dx + dy * dy <= MOON_R * MOON_R && cx * cx + cy * cy > MOON_R * MOON_R) {
            return true;
        }
        // a sparse, fixed subset of the sun's rays become stars
        uint32_t h = ((uint32_t)px * 73856093u) ^ ((uint32_t)py * 19349663u);
        return bit && h % 23 == 0;
    }
    return py == gy || dither; // ridge line, then detail below it
}

static void render(void) {
    for (int px = 0; px < ART_W; px++) {
        for (int py = 0; py < SCENE_H; py++) {
            bool bit = art_bit(px, py);
            bool white;
            switch (scene.style) {
            case STYLE_INVERTED:
                white = !bit;
                break;
            case STYLE_NIGHT:
                white = night_white(px, py, bit);
                break;
            default: // 50% checker so white snow stands out on it
                white = bit && ((px + py) & 1) == 0;
                break;
            }
            put_px(px, py, white ? SNOW : SKY);
        }
    }
    for (int x = FALL_X_MIN; x <= FALL_X_MAX; x++) {
        for (int k = 1; k <= snow[x]; k++) {
            put_px(x, FLOOR - k, SNOW);
        }
    }
    for (int i = 0; i < flake_count; i++) {
        draw_flake(&flakes[i]);
    }
    lv_obj_invalidate(canvas);
}

static bool snow_above_min(void) {
    for (int x = FALL_X_MIN; x <= FALL_X_MAX; x++) {
        if (snow[x] > SNOW_MIN) {
            return true;
        }
    }
    return false;
}

static void tick(lv_timer_t *t) {
    int64_t now = k_uptime_get();
    bool changed = false;

    for (int c = 0; c < KEY_COLUMNS; c++) {
        int n = atomic_set(&pending[c], 0);
        while (n-- > 0) {
            spawn(c);
            heat = MIN(heat + HEAT_PER_KEY, HEAT_MAX);
            changed = true;
        }
    }

    bool snowing = atomic_get(&active);
    if (snowing && rnd() % LIGHT_FRAMES == 0) {
        spawn_anywhere(true);
        changed = true;
    }
    if (snowing && (int32_t)(rnd() % HEAT_FLAKE_DIV) < heat) {
        spawn_anywhere(false);
        changed = true;
    }
    heat -= (heat + HEAT_DECAY - 1) / HEAT_DECAY;

    for (int i = 0; i < flake_count;) {
        struct flake *f = &flakes[i];
        f->y_q8 += f->speed_q8;
        f->x_q8 += ((int)(rnd() % 3) - 1) * 64; // wobble -1/4, 0, +1/4 px
        if (f->x_q8 < FALL_X_MIN << 8) {
            f->x_q8 = FALL_X_MIN << 8;
        } else if (f->x_q8 > FALL_X_MAX << 8) {
            f->x_q8 = FALL_X_MAX << 8;
        }

        int x = f->x_q8 >> 8, y = f->y_q8 >> 8;
        if (y + 1 >= top(x) || y >= FLOOR) {
            land(x);
            flakes[i] = flakes[--flake_count];
        } else {
            i++;
        }
        changed = true;
    }

    // Melt once typing stops, even while the light snow is still falling
    bool idle = now - last_key_ms > IDLE_BEFORE_MELT_MS;
    if (idle && snow_above_min() && now - last_melt_ms >= MELT_MS) {
        for (int x = 0; x < ART_W; x++) {
            if (snow[x] > SNOW_MIN) {
                snow[x]--;
            }
        }
        last_melt_ms = now;
        changed = true;
    }

    if (changed) {
        render();
    }

    // Nothing falling and no light snow: tick slowly while caps still have to
    // melt, else stop
    if (flake_count == 0 && !snowing) {
        if (snow_above_min()) {
            lv_timer_set_period(timer, idle ? MELT_MS : FRAME_MS);
        } else {
            lv_timer_pause(timer);
        }
    }
}

static void wake_work_cb(struct k_work *work) {
    if (timer != NULL) {
        lv_timer_set_period(timer, FRAME_MS);
        lv_timer_resume(timer);
    }
}
static K_WORK_DEFINE(wake_work, wake_work_cb);

static int key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Column within the full 12-column transform; thumbs (row 3) start at
    // keymap column 3, so their positions map onto columns 3..8.
    int pos = ev->position;
    int col = pos < 3 * KEYMAP_COLUMNS ? pos % KEYMAP_COLUMNS : 3 + (pos - 3 * KEYMAP_COLUMNS);
    int local = col % KEY_COLUMNS; // right half positions are offset by 6

    atomic_inc(&pending[local]);
    last_key_ms = k_uptime_get();
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &wake_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(nice_view_snow, key_listener);
ZMK_SUBSCRIPTION(nice_view_snow, zmk_position_state_changed);

// Light snow only while the keyboard is active (idle after CONFIG_ZMK_IDLE_TIMEOUT)
static int activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    bool now_active = ev->state == ZMK_ACTIVITY_ACTIVE;
    atomic_set(&active, now_active);
    if (now_active && zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &wake_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(nice_view_snow_activity, activity_listener);
ZMK_SUBSCRIPTION(nice_view_snow_activity, zmk_activity_state_changed);

int __wrap_zmk_widget_status_init(struct zmk_widget_status_prefix *widget, lv_obj_t *parent) {
    int ret = __real_zmk_widget_status_init(widget, parent);

    // Child 0 is the battery/connection canvas, child 1 the stock art image
    for (int i = 0; i < 2; i++) {
        lv_obj_t *stock = lv_obj_get_child(widget->obj, i);
        if (stock != NULL) {
            lv_obj_add_flag(stock, LV_OBJ_FLAG_HIDDEN);
        }
    }

    rng_state = sys_rand32_get() | 1;
    for (int x = 0; x < ART_W; x++) {
        snow[x] = x >= FALL_X_MIN && x <= FALL_X_MAX ? SNOW_MIN : 0;
    }

    canvas = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(canvas, cbuf, SCENE_H, ART_W, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    render();

    if (IS_ENABLED(CONFIG_NICE_VIEW_SNOW_STATUS)) {
        nice_view_status_create(widget->obj);
    }

    atomic_set(&active, zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    timer = lv_timer_create(tick, FRAME_MS, NULL);

    return ret;
}
