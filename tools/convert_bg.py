#!/usr/bin/env python3
"""Convert an image to LVGL RGB565 C array for ESP32 firmware background."""

import sys
from PIL import Image, ImageFilter, ImageEnhance

INPUT = "/Users/dushyant/.cursor/projects/Users-dushyant-Projects-Personal/assets/alessio-soggetti-IOhH65el-no-unsplash-f9919479-5e92-425e-b46b-cc951b98fe29.png"
OUTPUT = "/Users/dushyant/Projects/Personal/quantum-watch/firmware/esp-brookesia/main/bg_image.c"
TARGET_W = 205
TARGET_H = 251
BLUR_RADIUS = 12
DARKEN_FACTOR = 0.45

img = Image.open(INPUT).convert("RGB")

# Crop to match 410:502 aspect ratio
src_w, src_h = img.size
target_ratio = 410 / 502
src_ratio = src_w / src_h

if src_ratio > target_ratio:
    new_w = int(src_h * target_ratio)
    offset = (src_w - new_w) // 2
    img = img.crop((offset, 0, offset + new_w, src_h))
else:
    new_h = int(src_w / target_ratio)
    offset = (src_h - new_h) // 2
    img = img.crop((0, offset, src_w, offset + new_h))

img = img.resize((TARGET_W, TARGET_H), Image.LANCZOS)
img = img.filter(ImageFilter.GaussianBlur(radius=BLUR_RADIUS))
img = ImageEnhance.Brightness(img).enhance(DARKEN_FACTOR)

# Convert to RGB565
pixels = list(img.getdata())
rgb565_data = []
for r, g, b in pixels:
    val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    rgb565_data.append(val & 0xFF)
    rgb565_data.append((val >> 8) & 0xFF)

data_size = len(rgb565_data)
stride = TARGET_W * 2

with open(OUTPUT, "w") as f:
    f.write('#include "lvgl.h"\n\n')
    f.write(f"/* Blurred mountain sunset background {TARGET_W}x{TARGET_H} RGB565 */\n")
    f.write(f"static const uint8_t bg_image_map[] = {{\n")
    for i in range(0, len(rgb565_data), 16):
        chunk = rgb565_data[i:i+16]
        f.write("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",\n")
    f.write("};\n\n")
    f.write(f"const lv_image_dsc_t bg_image_dsc = {{\n")
    f.write(f"    .header = {{\n")
    f.write(f"        .cf = LV_COLOR_FORMAT_RGB565,\n")
    f.write(f"        .flags = 0,\n")
    f.write(f"        .w = {TARGET_W},\n")
    f.write(f"        .h = {TARGET_H},\n")
    f.write(f"        .stride = {stride},\n")
    f.write(f"    }},\n")
    f.write(f"    .data_size = {data_size},\n")
    f.write(f"    .data = bg_image_map,\n")
    f.write(f"}};\n")

print(f"Generated {OUTPUT}: {TARGET_W}x{TARGET_H}, {data_size} bytes")
