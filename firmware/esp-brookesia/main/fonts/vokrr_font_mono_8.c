/*******************************************************************************
 * Size: 8 px
 * Bpp: 2
 * Opts: --font /tmp/vokrr-fonts/IBMPlexMono-Regular.ttf --size 8 --bpp 2 --format lvgl --range 0x20-0x7E --lv-include lvgl.h -o firmware/esp-brookesia/main/fonts/vokrr_font_mono_8.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef VOKRR_FONT_MONO_8
#define VOKRR_FONT_MONO_8 1
#endif

#if VOKRR_FONT_MONO_8

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x20, 0xb1, 0x30,

    /* U+0022 "\"" */
    0x84, 0x28, 0x40,

    /* U+0023 "#" */
    0x15, 0xe, 0x8a, 0x6, 0xa1, 0xc4, 0xe0,

    /* U+0024 "$" */
    0x4, 0x14, 0x43, 0xe8, 0x4a, 0x1, 0xb4, 0xa6,
    0x93, 0x0,

    /* U+0025 "%" */
    0x5c, 0x12, 0x85, 0x8, 0x19, 0xa3, 0xb8,

    /* U+0026 "&" */
    0x28, 0x17, 0x5, 0x89, 0x56, 0x76, 0x0,

    /* U+0027 "'" */
    0x88,

    /* U+0028 "(" */
    0x2, 0x49, 0x8a, 0x14, 0xc, 0x12,

    /* U+0029 ")" */
    0x10, 0x98, 0x3e, 0xc2, 0xc3, 0xd4, 0x0,

    /* U+002A "*" */
    0x4, 0x6, 0x61, 0xc8, 0x2d, 0x0,

    /* U+002B "+" */
    0x2, 0x1b, 0x3, 0x30, 0xcc, 0x0,

    /* U+002C "," */
    0xd, 0x8c, 0x0,

    /* U+002D "-" */
    0x64,

    /* U+002E "." */
    0xb,

    /* U+002F "/" */
    0xa, 0xd, 0x84, 0xa0, 0xb0, 0x9c, 0x2d, 0xb,
    0x8,

    /* U+0030 "0" */
    0x2a, 0x1a, 0x66, 0x43, 0x90, 0xaa, 0x60,

    /* U+0031 "1" */
    0x2c, 0x29, 0xf, 0xfe, 0xc, 0xc0,

    /* U+0032 "2" */
    0x2a, 0x5, 0x3, 0xf2, 0xa1, 0xb8,

    /* U+0033 "3" */
    0x2a, 0x5, 0x3, 0x58, 0x56, 0x34, 0x0,

    /* U+0034 "4" */
    0x5, 0x0, 0xc1, 0xf5, 0xca, 0xa9, 0x40,

    /* U+0035 "5" */
    0x3a, 0x1, 0x30, 0x53, 0xf, 0xa8, 0x0,

    /* U+0036 "6" */
    0x2, 0x15, 0xa3, 0xd8, 0x6c, 0x7b, 0x0,

    /* U+0037 "7" */
    0x69, 0x82, 0x88, 0x8e, 0x16, 0x81, 0xc0,

    /* U+0038 "8" */
    0x2a, 0x1e, 0x3, 0xc0, 0x69, 0xaa, 0x9a,

    /* U+0039 "9" */
    0x2a, 0x1e, 0xc7, 0xb0, 0x50, 0x35, 0x0,

    /* U+003A ":" */
    0x33, 0xb,

    /* U+003B ";" */
    0x30, 0x61, 0xd8, 0xc2, 0x80,

    /* U+003C "<" */
    0xf, 0xa4, 0x14, 0x41, 0xc1, 0x34, 0x0,

    /* U+003D "=" */
    0x6a, 0x1a, 0x86, 0xa0,

    /* U+003E ">" */
    0xf, 0x48, 0x53, 0x5, 0x60, 0x64,

    /* U+003F "?" */
    0x2a, 0x5, 0x3, 0x50, 0x28, 0x34, 0x0,

    /* U+0040 "@" */
    0x25, 0xf, 0xc1, 0x78, 0x3c, 0x87, 0xf0,

    /* U+0041 "A" */
    0x6, 0x15, 0x10, 0x9c, 0xc, 0x97, 0x44,

    /* U+0042 "B" */
    0x6a, 0x7, 0x5, 0xc1, 0x76, 0x9d, 0xa0,

    /* U+0043 "C" */
    0x2a, 0x1e, 0x81, 0xff, 0x3d, 0x0,

    /* U+0044 "D" */
    0x3a, 0x2, 0x98, 0x72, 0x19, 0x3b, 0x0,

    /* U+0045 "E" */
    0x6a, 0x7, 0x40, 0x74, 0x7, 0x40, 0x74, 0x0,

    /* U+0046 "F" */
    0x6a, 0x7, 0x40, 0x74, 0x7, 0x40, 0xf0,

    /* U+0047 "G" */
    0x2a, 0x1e, 0x86, 0xa8, 0xd4, 0x7, 0x80,

    /* U+0048 "H" */
    0x51, 0x7, 0xdc, 0x83, 0x90, 0xf0,

    /* U+0049 "I" */
    0x2e, 0x4, 0xc1, 0xff, 0xc0, 0x98,

    /* U+004A "J" */
    0x1b, 0x19, 0xf, 0xea, 0x60,

    /* U+004B "K" */
    0x51, 0x5, 0x40, 0x41, 0xaa, 0xd, 0xa0,

    /* U+004C "L" */
    0x20, 0xff, 0xe3, 0xb4, 0x0,

    /* U+004D "M" */
    0x63, 0x40, 0xa0, 0x53, 0xb, 0xf, 0x80,

    /* U+004E "N" */
    0x71, 0x2, 0x82, 0xf0, 0x6f, 0x4, 0xa0,

    /* U+004F "O" */
    0x2a, 0x1e, 0xcd, 0x3, 0xa0, 0x57, 0x60,

    /* U+0050 "P" */
    0x6a, 0x7, 0x69, 0xda, 0x74, 0xf, 0x0,

    /* U+0051 "Q" */
    0x2a, 0x1e, 0x6, 0x9b, 0x86, 0x51, 0xa0, 0xe0,
    0x32, 0x0,

    /* U+0052 "R" */
    0x6a, 0x7, 0x69, 0xda, 0x7c, 0x13, 0x80,

    /* U+0053 "S" */
    0x2a, 0x1e, 0x82, 0x28, 0xa, 0x12, 0x9a,

    /* U+0054 "T" */
    0x6e, 0x54, 0xc8, 0x7f, 0xf0, 0xc0,

    /* U+0055 "U" */
    0x51, 0x7, 0xff, 0x11, 0xe0, 0x0,

    /* U+0056 "V" */
    0x80, 0xa9, 0x34, 0xc3, 0x9c, 0x14, 0x40,

    /* U+0057 "W" */
    0x82, 0x41, 0x84, 0xb1, 0xeb, 0x44, 0x40,

    /* U+0058 "X" */
    0x51, 0xe, 0xe0, 0xa2, 0xa, 0x87, 0x74,

    /* U+0059 "Y" */
    0x80, 0xaa, 0x1a, 0x7a, 0x2, 0x83, 0xe0,

    /* U+005A "Z" */
    0x69, 0x8d, 0x10, 0xa8, 0xa, 0x2, 0x28, 0x0,

    /* U+005B "[" */
    0x24, 0x1a, 0x1f, 0xfc, 0x6d, 0x0,

    /* U+005C "\\" */
    0x10, 0xd8, 0x6d, 0x9, 0xc3, 0x61, 0x94, 0x1b,
    0x0,

    /* U+005D "]" */
    0x1c, 0x50, 0x7f, 0xf0, 0x50,

    /* U+005E "^" */
    0x6, 0x15, 0xc3, 0x18, 0x81, 0x0,

    /* U+005F "_" */
    0x6a, 0x0,

    /* U+0060 "`" */
    0x10, 0x0,

    /* U+0061 "a" */
    0x28, 0x83, 0xe1, 0x10, 0x3a, 0x0,

    /* U+0062 "b" */
    0x10, 0xfe, 0xe8, 0xe, 0xc3, 0xee, 0xc0,

    /* U+0063 "c" */
    0x2a, 0x7, 0x40, 0xfb, 0xa0,

    /* U+0064 "d" */
    0xa, 0xf, 0xa4, 0xc, 0x87, 0xcc, 0x80,

    /* U+0065 "e" */
    0x26, 0x10, 0xed, 0x87, 0xa0,

    /* U+0066 "f" */
    0x5, 0x3, 0x40, 0x98, 0x13, 0x7, 0xd3, 0x0,

    /* U+0067 "g" */
    0x26, 0x9, 0xc, 0xe1, 0x60, 0x78, 0x56, 0xe8,

    /* U+0068 "h" */
    0x10, 0xfe, 0xe8, 0xe, 0xf, 0xfe, 0x0,

    /* U+0069 "i" */
    0x4, 0x1a, 0xa, 0xc2, 0x90, 0xfd, 0x30,

    /* U+006A "j" */
    0x9, 0x9, 0x28, 0x94, 0xf, 0xfe, 0x15, 0x0,

    /* U+006B "k" */
    0x10, 0xff, 0xa0, 0x7a, 0xa, 0x41, 0xb8,

    /* U+006C "l" */
    0x28, 0x14, 0x1f, 0xfc, 0x79, 0x80,

    /* U+006D "m" */
    0x9a, 0x1, 0x24, 0x3f, 0xc0,

    /* U+006E "n" */
    0x2a, 0x7, 0x7, 0xff, 0x0,

    /* U+006F "o" */
    0x2a, 0x1e, 0xc3, 0xcf, 0x60,

    /* U+0070 "p" */
    0x2a, 0x7, 0x61, 0xf7, 0x60, 0xe8, 0x1e,

    /* U+0071 "q" */
    0x26, 0x19, 0xf, 0x99, 0xa, 0x43, 0xe0,

    /* U+0072 "r" */
    0x2a, 0x48, 0x90, 0x61, 0x5c, 0x0,

    /* U+0073 "s" */
    0x26, 0xd, 0x2, 0x60, 0x50, 0x0,

    /* U+0074 "t" */
    0x14, 0x7, 0xd0, 0xfa, 0xf, 0x9e, 0x0,

    /* U+0075 "u" */
    0x51, 0x7, 0x90, 0xee, 0x40,

    /* U+0076 "v" */
    0x51, 0xe, 0x1c, 0xe0, 0xa2, 0x0,

    /* U+0077 "w" */
    0x8c, 0x45, 0x8f, 0x5a, 0x80, 0x80,

    /* U+0078 "x" */
    0x22, 0x7, 0x40, 0xfb, 0xa0,

    /* U+0079 "y" */
    0x50, 0x8e, 0x61, 0x38, 0x28, 0x84, 0x85, 0x40,
    0x0,

    /* U+007A "z" */
    0x29, 0x82, 0x30, 0x38, 0x57, 0x0,

    /* U+007B "{" */
    0x4, 0x84, 0x87, 0x21, 0x21, 0xb0, 0xb4,

    /* U+007C "|" */
    0x83, 0xc0,

    /* U+007D "}" */
    0x18, 0x8, 0x6c, 0x2d, 0x6, 0x83, 0x3, 0x80,

    /* U+007E "~" */
    0x24, 0xc, 0x40
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 77, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 77, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 77, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 6, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 13, .adv_w = 77, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 23, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 37, .adv_w = 77, .box_w = 1, .box_h = 3, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 38, .adv_w = 77, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 44, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 51, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 57, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 63, .adv_w = 77, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 66, .adv_w = 77, .box_w = 3, .box_h = 1, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 67, .adv_w = 77, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 77, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 77, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 90, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 103, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 117, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 124, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 131, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 145, .adv_w = 77, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 147, .adv_w = 77, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 152, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 159, .adv_w = 77, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 163, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 169, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 176, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 183, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 197, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 203, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 210, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 225, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 232, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 238, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 77, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 256, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 268, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 275, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 282, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 289, .adv_w = 77, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 299, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 313, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 325, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 332, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 339, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 346, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 353, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 361, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 367, .adv_w = 77, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 376, .adv_w = 77, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 381, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 387, .adv_w = 77, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 389, .adv_w = 77, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 391, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 404, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 409, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 416, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 429, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 437, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 444, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 451, .adv_w = 77, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 459, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 466, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 472, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 477, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 482, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 487, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 494, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 501, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 507, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 513, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 520, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 525, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 531, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 537, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 542, .adv_w = 77, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 551, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 557, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 564, .adv_w = 77, .box_w = 1, .box_h = 7, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 566, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 574, .adv_w = 77, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 1}
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
const lv_font_t vokrr_font_mono_8 = {
#else
lv_font_t vokrr_font_mono_8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 8,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if VOKRR_FONT_MONO_8*/

