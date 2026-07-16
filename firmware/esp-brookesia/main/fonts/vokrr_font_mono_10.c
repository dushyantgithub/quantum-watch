/*******************************************************************************
 * Size: 10 px
 * Bpp: 2
 * Opts: --font /tmp/vokrr-fonts/IBMPlexMono-Regular.ttf --size 10 --bpp 2 --format lvgl --range 0x20-0x7E --lv-include lvgl.h -o firmware/esp-brookesia/main/fonts/vokrr_font_mono_10.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef VOKRR_FONT_MONO_10
#define VOKRR_FONT_MONO_10 1
#endif

#if VOKRR_FONT_MONO_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x50, 0x79, 0x40, 0xa0,

    /* U+0022 "\"" */
    0x65, 0x6, 0x71,

    /* U+0023 "#" */
    0x4, 0x41, 0xf3, 0xa, 0x9f, 0x15, 0x8f, 0x32,
    0x89, 0xf, 0x0,

    /* U+0024 "$" */
    0x9, 0xa, 0xe8, 0x17, 0x40, 0x28, 0x35, 0xd0,
    0xd, 0x4, 0x41, 0x83, 0xd6, 0x97, 0x40,

    /* U+0025 "%" */
    0x68, 0xb, 0x81, 0x40, 0xf5, 0x32, 0x4, 0xa9,
    0x23, 0xb6, 0x3, 0x80,

    /* U+0026 "&" */
    0x2a, 0x9, 0xa0, 0x7f, 0x31, 0x34, 0xe1, 0x2,
    0x15, 0xd8, 0x80,

    /* U+0027 "'" */
    0x50, 0x14,

    /* U+0028 "(" */
    0x14, 0x6d, 0x3, 0xfd, 0xa3, 0x82, 0x40,

    /* U+0029 ")" */
    0x50, 0xe0, 0xa0, 0x7f, 0x9c, 0xd5, 0x0,

    /* U+002A "*" */
    0x2, 0x82, 0x40, 0x85, 0x40, 0x97, 0x1, 0xa2,
    0x0,

    /* U+002B "+" */
    0xf, 0xca, 0xa, 0xb0, 0x2b, 0x7, 0xc0,

    /* U+002C "," */
    0x56, 0x28,

    /* U+002D "-" */
    0x7c, 0x80,

    /* U+002E "." */
    0x5, 0x0,

    /* U+002F "/" */
    0xd, 0x7, 0x61, 0xa4, 0x3f, 0x9c, 0x36, 0x84,
    0xe1, 0xb4, 0x36, 0x18,

    /* U+0030 "0" */
    0x1a, 0x20, 0xe9, 0x8e, 0xd, 0x2, 0x83, 0x28,
    0xe, 0xd, 0x3a, 0x60,

    /* U+0031 "1" */
    0x7, 0x5, 0x61, 0xa0, 0xff, 0xe5, 0xd1, 0x90,

    /* U+0032 "2" */
    0x2a, 0x42, 0xa8, 0x10, 0x7c, 0xa0, 0x9e, 0x5,
    0xc0, 0x51, 0x44,

    /* U+0033 "3" */
    0x2a, 0x41, 0xd4, 0x4, 0x3d, 0x70, 0x57, 0x7,
    0xea, 0xc0,

    /* U+0034 "4" */
    0xb, 0x82, 0xa0, 0x72, 0x15, 0x2, 0x70, 0xde,
    0xa2, 0xab, 0x28,

    /* U+0035 "5" */
    0x3a, 0x80, 0xd4, 0x1f, 0x9a, 0x20, 0xab, 0x2,
    0x1d, 0x50, 0x0,

    /* U+0036 "6" */
    0x3, 0x4, 0xf0, 0x54, 0xc, 0xd1, 0x12, 0x98,
    0x87, 0xba, 0x0,

    /* U+0037 "7" */
    0x7a, 0x69, 0x50, 0xb0, 0xa0, 0xdc, 0x1f, 0xd6,
    0x19, 0x40,

    /* U+0038 "8" */
    0x2b, 0x5, 0x40, 0xfe, 0xfc, 0x3f, 0x90, 0xf2,
    0x50, 0x40,

    /* U+0039 "9" */
    0x1a, 0x20, 0xe9, 0x88, 0x65, 0x80, 0xa2, 0xa4,
    0xd, 0xe0, 0xa1, 0x0,

    /* U+003A ":" */
    0xac, 0x1a, 0x80,

    /* U+003B ";" */
    0xac, 0x5, 0x62, 0x80,

    /* U+003C "<" */
    0xc, 0x86, 0xb0, 0x56, 0xb, 0xd, 0x12, 0x13,
    0x0,

    /* U+003D "=" */
    0x2b, 0x2, 0xb0, 0x2b, 0x0,

    /* U+003E ">" */
    0x10, 0xee, 0xd, 0x58, 0x36, 0x13, 0x10, 0x52,
    0x0,

    /* U+003F "?" */
    0x1a, 0x20, 0xea, 0x4, 0x1e, 0x68, 0x17, 0x21,
    0x41, 0xda, 0x0,

    /* U+0040 "@" */
    0x1a, 0x20, 0xe9, 0x8e, 0xc1, 0xbb, 0x43, 0xc9,
    0x4d, 0x32, 0x80, 0xa9, 0x0,

    /* U+0041 "A" */
    0x5, 0x3, 0xf9, 0xf2, 0xd, 0x58, 0x1a, 0x20,
    0x68, 0x96, 0xe,

    /* U+0042 "B" */
    0x3a, 0x80, 0xd1, 0xf, 0xcd, 0xc0, 0x6f, 0x21,
    0xf3, 0x41,

    /* U+0043 "C" */
    0x1a, 0x81, 0x56, 0x4, 0x8, 0x87, 0x21, 0xe4,
    0x8, 0x2a, 0xc0,

    /* U+0044 "D" */
    0x3a, 0x21, 0x54, 0x1c, 0xa0, 0xd8, 0x76, 0x1c,
    0xa0, 0x54, 0x0,

    /* U+0045 "E" */
    0x3a, 0x80, 0xd4, 0x1f, 0x9a, 0x80, 0xd4, 0x1f,
    0x9a, 0x80,

    /* U+0046 "F" */
    0x3a, 0x80, 0xd4, 0x1f, 0x9a, 0x80, 0xd4, 0x1f,
    0xfc, 0x10,

    /* U+0047 "G" */
    0x1a, 0x81, 0x56, 0x30, 0x10, 0xd4, 0xd, 0x1,
    0x80, 0x82, 0xa4,

    /* U+0048 "H" */
    0x20, 0x41, 0xff, 0xc2, 0x68, 0x81, 0xa2, 0x1f,
    0xfc, 0x10,

    /* U+0049 "I" */
    0x2f, 0x40, 0xac, 0x1f, 0xfc, 0xea, 0xc0,

    /* U+004A "J" */
    0x1a, 0x63, 0x40, 0xff, 0xe2, 0xa0, 0x4e, 0xa0,

    /* U+004B "K" */
    0x20, 0x48, 0x56, 0x81, 0x10, 0x30, 0x76, 0x84,
    0x9e, 0xe, 0x50,

    /* U+004C "L" */
    0xc3, 0xff, 0xa5, 0x50,

    /* U+004D "M" */
    0x70, 0x68, 0x14, 0x15, 0xe8, 0x3f, 0xa8, 0x1f,
    0xfc, 0x20,

    /* U+004E "N" */
    0x30, 0x41, 0x41, 0x94, 0x1d, 0x21, 0x98, 0x39,
    0x41, 0xa0, 0x0,

    /* U+004F "O" */
    0x1a, 0x20, 0xea, 0x1c, 0x12, 0x1f, 0xfc, 0x7,
    0x6, 0x9d, 0x30,

    /* U+0050 "P" */
    0x3a, 0x80, 0xd0, 0x43, 0xe6, 0x82, 0x35, 0x7,
    0xff, 0x4,

    /* U+0051 "Q" */
    0x1a, 0x20, 0xe9, 0x8e, 0xd, 0xf, 0xb0, 0x62,
    0xf8, 0xfc, 0x14, 0x40,

    /* U+0052 "R" */
    0x3a, 0x80, 0xd0, 0x43, 0xe6, 0xc4, 0x62, 0xd,
    0x40, 0xc8, 0x0,

    /* U+0053 "S" */
    0x2b, 0x5, 0x4c, 0x8, 0x10, 0x53, 0x80, 0xd8,
    0x88, 0x77, 0x41,

    /* U+0054 "T" */
    0xa7, 0xab, 0xf0, 0x7f, 0xf4, 0x80,

    /* U+0055 "U" */
    0x20, 0x41, 0xff, 0xce, 0x40, 0x82, 0x9c, 0x0,

    /* U+0056 "V" */
    0x50, 0x16, 0xc1, 0xa2, 0x4, 0x8, 0x10, 0x73,
    0x81, 0x5a, 0x12, 0x10,

    /* U+0057 "W" */
    0x83, 0x41, 0xed, 0xa3, 0x87, 0xff, 0x1, 0xfc,
    0x8b, 0x80,

    /* U+0058 "X" */
    0x60, 0x4a, 0xc, 0x97, 0x40, 0x90, 0xff, 0x55,
    0x88, 0x4a,

    /* U+0059 "Y" */
    0x90, 0x35, 0x2, 0x84, 0xa8, 0xd, 0x10, 0xa8,
    0x1f, 0xfc, 0x20,

    /* U+005A "Z" */
    0x2a, 0xd2, 0xa5, 0x5, 0xc1, 0x50, 0x24, 0x42,
    0xa0, 0x49, 0x52,

    /* U+005B "[" */
    0xa8, 0x14, 0xf, 0xfe, 0x75, 0x0,

    /* U+005C "\\" */
    0x20, 0xec, 0x39, 0x83, 0xfe, 0xd0, 0xce, 0x1d,
    0xa1, 0x9c, 0x3b, 0x0,

    /* U+005D "]" */
    0x2a, 0x28, 0x1f, 0xfc, 0xea, 0x0,

    /* U+005E "^" */
    0x5, 0x2, 0x71, 0x6, 0xd0, 0x70, 0x48,

    /* U+005F "_" */
    0x6b, 0x20,

    /* U+0060 "`" */
    0xd, 0x2, 0x0,

    /* U+0061 "a" */
    0x2a, 0x41, 0x56, 0xa, 0x91, 0x28, 0x4, 0x96,

    /* U+0062 "b" */
    0x30, 0xff, 0xe0, 0x54, 0x15, 0x3, 0xda, 0x1b,
    0x40, 0xc0,

    /* U+0063 "c" */
    0x1a, 0x20, 0xa9, 0x2, 0x1c, 0x87, 0x56, 0x0,

    /* U+0064 "d" */
    0xd, 0x87, 0xea, 0x83, 0x50, 0xe, 0x19, 0xc3,
    0xd4, 0x0,

    /* U+0065 "e" */
    0x1a, 0x20, 0xe9, 0x8a, 0xa0, 0x15, 0x50, 0x3a,
    0x80,

    /* U+0066 "f" */
    0xa, 0x88, 0x1e, 0x4a, 0xc9, 0x59, 0xf, 0xfe,
    0xd, 0x60,

    /* U+0067 "g" */
    0xd, 0x23, 0x41, 0x2a, 0x43, 0xf5, 0x58, 0x30,
    0x41, 0x53, 0x81, 0x4e,

    /* U+0068 "h" */
    0x30, 0xff, 0xe0, 0x51, 0xa, 0xa0, 0xe4, 0x3f,
    0xf8, 0x20,

    /* U+0069 "i" */
    0x6, 0x1b, 0x5, 0x40, 0xa0, 0x7f, 0xf0, 0x68,
    0xc8,

    /* U+006A "j" */
    0xa, 0x42, 0x92, 0x9a, 0x54, 0x1f, 0xfc, 0xaa,
    0x68,

    /* U+006B "k" */
    0xc3, 0xff, 0x81, 0x5, 0x40, 0x41, 0xae, 0x9,
    0x10,

    /* U+006C "l" */
    0x2d, 0xa, 0x81, 0xff, 0xd0, 0xac, 0x0,

    /* U+006D "m" */
    0x68, 0xc0, 0xf1, 0xa1, 0xff, 0xc6,

    /* U+006E "n" */
    0x36, 0x42, 0x68, 0x1c, 0x87, 0xff, 0x4,

    /* U+006F "o" */
    0x1a, 0x20, 0xe9, 0x88, 0x65, 0x6, 0x4e, 0x98,

    /* U+0070 "p" */
    0x36, 0x81, 0x30, 0x7b, 0x43, 0x68, 0x18, 0x33,
    0x40, 0xf8,

    /* U+0071 "q" */
    0x2a, 0xc2, 0xa0, 0x1c, 0x33, 0x87, 0xa8, 0x15,
    0x41, 0xf8,

    /* U+0072 "r" */
    0x2d, 0x92, 0x54, 0x87, 0xff, 0x6, 0x68, 0x0,

    /* U+0073 "s" */
    0x2b, 0x1, 0xa8, 0x15, 0x20, 0x69, 0x82, 0xa0,
    0x0,

    /* U+0074 "t" */
    0x6, 0x1f, 0xcc, 0x50, 0x62, 0x81, 0xff, 0xc3,
    0x68, 0x0,

    /* U+0075 "u" */
    0x20, 0x61, 0xff, 0xc2, 0x43, 0x9a, 0x0,

    /* U+0076 "v" */
    0x20, 0x40, 0x43, 0xa5, 0x60, 0x7c, 0x87, 0xc0,

    /* U+0077 "w" */
    0x8a, 0x16, 0x1b, 0x10, 0x23, 0xf9, 0x17, 0x0,

    /* U+0078 "x" */
    0x30, 0x60, 0xe9, 0x87, 0xe4, 0x8, 0x2b, 0x0,

    /* U+0079 "y" */
    0x60, 0x4b, 0x1, 0x44, 0xa8, 0xf, 0x90, 0xff,
    0x41, 0x41, 0x80,

    /* U+007A "z" */
    0x2a, 0xc1, 0x42, 0xa, 0xd0, 0x22, 0x14, 0x50,

    /* U+007B "{" */
    0x1a, 0x1d, 0xd, 0x3, 0x94, 0xca, 0x60, 0x70,
    0x68, 0x3a, 0x0,

    /* U+007C "|" */
    0x50, 0x7f, 0xf0, 0x94,

    /* U+007D "}" */
    0xa2, 0x53, 0x3, 0x83, 0x41, 0xd0, 0xe8, 0x68,
    0x1c, 0xa6, 0x0,

    /* U+007E "~" */
    0x28, 0x1a, 0xb0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 96, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 96, .box_w = 2, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 96, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 7, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 18, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 33, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 45, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 56, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 58, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 65, .adv_w = 96, .box_w = 3, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 72, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 81, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 88, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 90, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 92, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 94, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 106, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 118, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 126, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 137, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 147, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 169, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 200, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 212, .adv_w = 96, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 215, .adv_w = 96, .box_w = 2, .box_h = 6, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 219, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 228, .adv_w = 96, .box_w = 6, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 233, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 253, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 266, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 277, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 287, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 298, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 309, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 350, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 357, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 365, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 376, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 380, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 390, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 401, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 412, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 422, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 434, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 445, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 456, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 462, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 470, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 482, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 492, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 502, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 513, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 524, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 530, .adv_w = 96, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 542, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 548, .adv_w = 96, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 555, .adv_w = 96, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 557, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 560, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 568, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 578, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 586, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 596, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 605, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 615, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 627, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 637, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 646, .adv_w = 96, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 655, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 664, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 671, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 677, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 684, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 692, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 702, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 712, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 720, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 729, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 739, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 746, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 754, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 762, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 770, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 781, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 789, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 800, .adv_w = 96, .box_w = 2, .box_h = 9, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 804, .adv_w = 96, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 815, .adv_w = 96, .box_w = 6, .box_h = 2, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
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
    .bpp = 2,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t vokrr_font_mono_10 = {
#else
lv_font_t vokrr_font_mono_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if VOKRR_FONT_MONO_10*/

