/*******************************************************************************
 * Size: 20 px
 * Bpp: 4
 * Hand edit: adv_w 188 -> 320 (20 px, like the Montserrat symbols). The Nerd
 * Font cell is narrower than the icon, so a right-aligned label spilled ~7 px
 * to the right. Redo this edit if the file is regenerated.
 * Opts: --font /usr/share/fonts/truetype/CaskaydiaCoveNerdFont-Regular.ttf -r 0xF04B3 --size 20 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o /home/bbd/repos/zmk/zmk-config/src/fonts/icon_sleep_off_20.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef ICON_SLEEP_OFF_20
#define ICON_SLEEP_OFF_20 1
#endif

#if ICON_SLEEP_OFF_20

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F04B3 "󰒳" */
    0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0xaa,
    0xaa, 0x10, 0xeb, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xdf, 0xff, 0xf2, 0x5, 0xfb, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x7, 0xfc, 0x0, 0x5, 0xfb, 0x0,
    0x0, 0x0, 0x0, 0x3, 0xfe, 0x10, 0x0, 0x6,
    0xfb, 0x3, 0xff, 0xf8, 0x1, 0xef, 0x30, 0x0,
    0x0, 0x6, 0xfb, 0x3, 0xff, 0x60, 0xbf, 0xb6,
    0x60, 0x0, 0x0, 0x6, 0xfa, 0x3, 0xb0, 0xd,
    0xff, 0xff, 0x2b, 0xbb, 0xbb, 0x6, 0xfa, 0x0,
    0x0, 0x34, 0x44, 0x40, 0xff, 0xff, 0xf0, 0x1e,
    0xfa, 0x0, 0x0, 0x0, 0x0, 0x0, 0x9, 0xf8,
    0x5, 0xff, 0xfa, 0x0, 0x0, 0x0, 0x0, 0x7,
    0xfa, 0x0, 0x39, 0x9a, 0xfa, 0x0, 0x0, 0x0,
    0x5, 0xfc, 0x0, 0x0, 0x0, 0x7, 0xf9, 0x0,
    0x0, 0x0, 0xff, 0xcb, 0xb0, 0x0, 0x0, 0x7,
    0xf9, 0x0, 0x0, 0xf, 0xff, 0xff, 0x0, 0x0,
    0x0, 0x7, 0xf9, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x7, 0xf6, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x5, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 320, .box_w = 19, .box_h = 16, .ofs_x = 0, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 984243, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LV_VERSION_CHECK(8, 0, 0)
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LV_VERSION_CHECK(8, 0, 0)
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LV_VERSION_CHECK(8, 0, 0)
const lv_font_t icon_sleep_off_20 = {
#else
lv_font_t icon_sleep_off_20 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if ICON_SLEEP_OFF_20*/

