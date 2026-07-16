#!/usr/bin/env python3
"""Generate the Quantum Watch iOS app icon and watch startup logo assets."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path("/Users/dushyant/Projects/Personal/quantum-watch")
IOS_APP_ICON = ROOT / "ios/QuantumWatchCompanion/Assets.xcassets/AppIcon.appiconset/AppIcon.png"
WATCH_SPLASH_PNG = ROOT / "firmware/esp-brookesia/main/startup_logo.png"
WATCH_SPLASH_C = ROOT / "firmware/esp-brookesia/main/startup_logo.c"

BACKGROUND = (15, 23, 42, 255)
TEXT_PRIMARY = (248, 250, 252, 255)
TEXT_SECONDARY = (148, 163, 184, 255)
GRADIENT_STOPS = [
    (0.0, (245, 158, 11, 255)),
    (0.5, (239, 68, 68, 255)),
    (1.0, (139, 92, 246, 255)),
]


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def gradient_color(t: float) -> tuple[int, int, int, int]:
    t = max(0.0, min(1.0, t))
    for idx in range(len(GRADIENT_STOPS) - 1):
        start_t, start_color = GRADIENT_STOPS[idx]
        end_t, end_color = GRADIENT_STOPS[idx + 1]
        if t <= end_t:
            local_t = 0.0 if end_t == start_t else (t - start_t) / (end_t - start_t)
            return tuple(int(round(lerp(start_color[i], end_color[i], local_t))) for i in range(4))
    return GRADIENT_STOPS[-1][1]


def find_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    ]
    index = 1 if bold else 0
    for path in candidates:
        try:
            return ImageFont.truetype(path, size=size, index=index)
        except Exception:
            try:
                return ImageFont.truetype(path, size=size)
            except Exception:
                continue
    return ImageFont.load_default()


def line_points(start: tuple[float, float], end: tuple[float, float], steps: int) -> list[tuple[float, float]]:
    return [
        (lerp(start[0], end[0], i / max(steps - 1, 1)), lerp(start[1], end[1], i / max(steps - 1, 1)))
        for i in range(steps)
    ]


def arc_points(
    center: tuple[float, float], radius: float, start_deg: float, end_deg: float, steps: int
) -> list[tuple[float, float]]:
    pts: list[tuple[float, float]] = []
    for i in range(steps):
        t = i / max(steps - 1, 1)
        angle = math.radians(lerp(start_deg, end_deg, t))
        pts.append((center[0] + math.cos(angle) * radius, center[1] + math.sin(angle) * radius))
    return pts


def wave_points(width: int, height: int) -> list[tuple[float, float]]:
    left = width * 0.35
    right = width * 0.65
    center_y = height * 0.425
    amplitude = height * 0.065
    pts: list[tuple[float, float]] = []
    steps = 84
    for i in range(steps):
        t = i / max(steps - 1, 1)
        x = lerp(left, right, t)
        y = center_y - math.sin(t * math.tau * 1.5) * amplitude * (0.8 + 0.2 * math.cos(t * math.pi))
        pts.append((x, y))
    return pts


def draw_gradient_path(draw: ImageDraw.ImageDraw, pts: list[tuple[float, float]], width: int, t0: float, t1: float) -> None:
    if len(pts) < 2:
        return
    radius = max(1, width // 2)
    segments = len(pts) - 1
    for idx in range(segments):
        start = pts[idx]
        end = pts[idx + 1]
        t = idx / max(segments - 1, 1)
        color = gradient_color(lerp(t0, t1, t))
        draw.line((start[0], start[1], end[0], end[1]), fill=color, width=width)
        draw.ellipse(
            (start[0] - radius, start[1] - radius, start[0] + radius, start[1] + radius),
            fill=color,
        )
    end = pts[-1]
    color = gradient_color(t1)
    draw.ellipse((end[0] - radius, end[1] - radius, end[0] + radius, end[1] + radius), fill=color)


def draw_spaced_text(
    draw: ImageDraw.ImageDraw,
    center_x: float,
    y: float,
    text: str,
    font: ImageFont.FreeTypeFont,
    fill: tuple[int, int, int, int],
    tracking: int,
) -> None:
    widths = [draw.textbbox((0, 0), ch, font=font)[2] for ch in text]
    total_width = sum(widths) + tracking * max(len(text) - 1, 0)
    x = center_x - total_width / 2
    for ch, width in zip(text, widths):
        draw.text((x, y), ch, font=font, fill=fill)
        x += width + tracking


def draw_brand_icon(size: int, include_watch_word: bool = True) -> Image.Image:
    scale = 4
    canvas = Image.new("RGBA", (size * scale, size * scale), BACKGROUND)
    draw = ImageDraw.Draw(canvas)

    width = canvas.width
    height = canvas.height

    center = (width * 0.5, height * 0.43)
    radius = width * 0.275
    outer_stroke = max(10, int(width * 0.045))
    wave_stroke = max(6, int(width * 0.02))
    tick_stroke = max(4, int(width * 0.014))
    dot_radius = max(6, int(width * 0.026))

    outer_loop = arc_points(center, radius, -90, 45, 140)
    tail = line_points((width * 0.575, height * 0.50), (width * 0.775, height * 0.70), 36)

    draw_gradient_path(draw, outer_loop, outer_stroke, 0.0, 0.72)
    draw_gradient_path(draw, tail, outer_stroke, 0.62, 1.0)
    draw_gradient_path(draw, wave_points(width, height), wave_stroke, 0.08, 0.9)

    tick_color = gradient_color(0.12)
    top_tick_y0 = center[1] - radius
    top_tick_y1 = top_tick_y0 + height * 0.05
    draw.line((center[0], top_tick_y0, center[0], top_tick_y1), fill=tick_color, width=tick_stroke)
    left_tick_x0 = center[0] - radius
    left_tick_x1 = left_tick_x0 + width * 0.05
    draw.line((left_tick_x0, center[1], left_tick_x1, center[1]), fill=tick_color, width=tick_stroke)
    bottom_tick_y0 = center[1] + radius
    bottom_tick_y1 = bottom_tick_y0 - height * 0.05
    draw.line((center[0], bottom_tick_y0, center[0], bottom_tick_y1), fill=tick_color, width=tick_stroke)

    draw.ellipse(
        (center[0] - dot_radius, center[1] - dot_radius, center[0] + dot_radius, center[1] + dot_radius),
        fill=TEXT_PRIMARY,
    )

    title_font = find_font(max(16, int(height * 0.075)), bold=True)
    subtitle_font = find_font(max(10, int(height * 0.035)), bold=True)
    draw_spaced_text(
        draw,
        width / 2,
        height * 0.82,
        "QUANTUM",
        title_font,
        TEXT_PRIMARY,
        max(2, int(width * 0.007)),
    )

    if include_watch_word:
        draw_spaced_text(
            draw,
            width / 2,
            height * 0.89,
            "WATCH",
            subtitle_font,
            TEXT_SECONDARY,
            max(2, int(width * 0.012)),
        )

    return canvas.resize((size, size), Image.LANCZOS)


def write_rgb565_c(image: Image.Image, output: Path, symbol: str) -> None:
    rgb = image.convert("RGB")
    pixels = list(rgb.getdata())
    rgb565_bytes: list[int] = []
    for r, g, b in pixels:
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb565_bytes.append(value & 0xFF)
        rgb565_bytes.append((value >> 8) & 0xFF)

    map_name = f"{symbol}_map"
    stride = image.width * 2
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as f:
        f.write('#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n')
        f.write('#include "lvgl.h"\n')
        f.write('#elif defined(LV_BUILD_TEST)\n')
        f.write('#include "../lvgl.h"\n')
        f.write('#else\n')
        f.write('#include "lvgl/lvgl.h"\n')
        f.write('#endif\n\n')
        f.write('#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n')
        f.write(f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {map_name}[] = {{\n")
        for idx in range(0, len(rgb565_bytes), 16):
            chunk = rgb565_bytes[idx:idx + 16]
            f.write("    " + ",".join(f"0x{byte:02x}" for byte in chunk) + ",\n")
        f.write("};\n\n")
        f.write(f"const lv_image_dsc_t {symbol} = {{\n")
        f.write("  .header.magic = LV_IMAGE_HEADER_MAGIC,\n")
        f.write("  .header.cf = LV_COLOR_FORMAT_RGB565,\n")
        f.write("  .header.flags = 0,\n")
        f.write(f"  .header.w = {image.width},\n")
        f.write(f"  .header.h = {image.height},\n")
        f.write(f"  .header.stride = {stride},\n")
        f.write(f"  .data_size = sizeof({map_name}),\n")
        f.write(f"  .data = {map_name},\n")
        f.write("};\n")


def main() -> None:
    IOS_APP_ICON.parent.mkdir(parents=True, exist_ok=True)
    WATCH_SPLASH_PNG.parent.mkdir(parents=True, exist_ok=True)

    app_icon = draw_brand_icon(1024, include_watch_word=True)
    app_icon.save(IOS_APP_ICON)

    watch_logo = draw_brand_icon(220, include_watch_word=True)
    watch_logo.save(WATCH_SPLASH_PNG)
    write_rgb565_c(watch_logo, WATCH_SPLASH_C, "quantum_watch_startup_logo_220_220")

    print(f"Generated {IOS_APP_ICON}")
    print(f"Generated {WATCH_SPLASH_PNG}")
    print(f"Generated {WATCH_SPLASH_C}")


if __name__ == "__main__":
    main()
