/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --font CaskaydiaCoveNerdFontPropo-Regular.ttf -r 0x30-0x39 --size 16 --bpp 1 --format lvgl --no-compress --lv-include lvgl.h -o nv_status_digits_16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef NV_STATUS_DIGITS_16
#define NV_STATUS_DIGITS_16 1
#endif

#if NV_STATUS_DIGITS_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0030 "0" */
    0x3c, 0x66, 0x42, 0xc3, 0xc3, 0xdb, 0xc3, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0031 "1" */
    0x39, 0xf2, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x18,
    0x30, 0x67, 0xf0,

    /* U+0032 "2" */
    0x7d, 0x9c, 0x18, 0x30, 0x60, 0xc3, 0xe, 0x18,
    0x63, 0x7, 0xf0,

    /* U+0033 "3" */
    0x3e, 0x63, 0x3, 0x3, 0x6, 0x38, 0x3e, 0x3,
    0x3, 0x3, 0x6, 0x7c,

    /* U+0034 "4" */
    0x3, 0x19, 0x88, 0xc4, 0x66, 0x33, 0x19, 0x8c,
    0xc6, 0x7f, 0x81, 0x80, 0xc0, 0x60,

    /* U+0035 "5" */
    0x7e, 0x60, 0x60, 0x60, 0x7e, 0x46, 0x3, 0x3,
    0x3, 0xc3, 0xe6, 0x3c,

    /* U+0036 "6" */
    0xe, 0x30, 0x60, 0x60, 0xc0, 0xde, 0xe7, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0037 "7" */
    0xff, 0xc3, 0xc3, 0xc6, 0x6, 0x6, 0x4, 0xc,
    0xc, 0x8, 0x18, 0x18,

    /* U+0038 "8" */
    0x7c, 0xc6, 0xc6, 0xc6, 0x44, 0x38, 0x66, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0xe7, 0x7b, 0x3,
    0x6, 0x6, 0xc, 0x70
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 12, .adv_w = 150, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 23, .adv_w = 150, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 34, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 46, .adv_w = 150, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 60, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 72, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 150, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 48, .range_length = 10, .glyph_id_start = 1,
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
    .bpp = 1,
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
const lv_font_t nv_status_digits_16 = {
#else
lv_font_t nv_status_digits_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 12,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if NV_STATUS_DIGITS_16*/

