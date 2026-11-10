#!/usr/bin/env python3
"""把照片转换成 ST7789 屏幕可直接显示的 135x240 RGB565 文件。

默认布局：
    中间：完整照片按宽度缩放到 135x90
    上下：同一张照片放大、模糊、压暗后填满 135x240
    中间照片上下各画一条 1 像素浅色边框

用法（在工程根目录 sample_project 下）：
    python tools/make_photo.py
    python tools/make_photo.py pic/另一张照片.jpg
    python tools/make_photo.py pic/另一张照片.jpg --background black
    python tools/make_photo.py --big-endian

输出：
    main/photo.rgb565   给固件用的二进制
    pic/photo_preview.png   给你在电脑上看的预览图
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter

SCREEN_W = 135
SCREEN_H = 240

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INPUT = PROJECT_ROOT / "pic" / "e9f07e5ffb9a2149e38261009811f975.jpg"
DEFAULT_BIN = PROJECT_ROOT / "main" / "photo.rgb565"
DEFAULT_PREVIEW = PROJECT_ROOT / "pic" / "photo_preview.png"


def resolve(path_text: str) -> Path:
    """相对路径按工程根目录解析，绝对路径原样使用。"""
    path = Path(path_text).expanduser()
    return path if path.is_absolute() else PROJECT_ROOT / path


def make_background(img: Image.Image, brightness: float, blur: float) -> Image.Image:
    """把原图放大到能盖住 135x240，居中裁剪，再模糊和压暗。"""
    scale = max(SCREEN_W / img.width, SCREEN_H / img.height)
    resized = img.resize(
        (round(img.width * scale), round(img.height * scale)),
        Image.Resampling.LANCZOS,
    )

    left = (resized.width - SCREEN_W) // 2
    top = (resized.height - SCREEN_H) // 2
    cover = resized.crop((left, top, left + SCREEN_W, top + SCREEN_H))

    cover = cover.filter(ImageFilter.GaussianBlur(blur))
    return ImageEnhance.Brightness(cover).enhance(brightness)


def make_fill_canvas(img: Image.Image) -> Image.Image:
    """铺满模式：按比例放大后居中裁剪，填满 135x240。"""
    scale = max(SCREEN_W / img.width, SCREEN_H / img.height)
    resized = img.resize(
        (round(img.width * scale), round(img.height * scale)),
        Image.Resampling.LANCZOS,
    )

    left = (resized.width - SCREEN_W) // 2
    top = (resized.height - SCREEN_H) // 2
    return resized.crop((left, top, left + SCREEN_W, top + SCREEN_H))


def make_canvas(
    img: Image.Image,
    background: str,
    brightness: float,
    blur: float,
) -> Image.Image:
    """生成最终的 135x240 RGB 画布。"""
    if background == "black":
        canvas = Image.new("RGB", (SCREEN_W, SCREEN_H), (0, 0, 0))
    else:
        canvas = make_background(img, brightness, blur)

    fg_h = round(img.height * SCREEN_W / img.width)
    foreground = img.resize((SCREEN_W, fg_h), Image.Resampling.LANCZOS)
    top = (SCREEN_H - fg_h) // 2

    canvas.paste(foreground, (0, top))
    canvas.paste((225, 225, 225), (0, top - 1, SCREEN_W, top))
    canvas.paste((225, 225, 225), (0, top + fg_h, SCREEN_W, top + fg_h + 1))
    return canvas


def to_rgb565(canvas: Image.Image, big_endian: bool) -> bytearray:
    """逐像素把 RGB888 压成 RGB565，并按屏幕需要的字节序输出。"""
    data = bytearray()
    raw = canvas.tobytes()

    for i in range(0, len(raw), 3):
        r = raw[i]
        g = raw[i + 1]
        b = raw[i + 2]

        # RRRRRGGG GGGBBBBB
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

        if big_endian:
            data += bytes((value >> 8, value & 0xFF))
        else:
            data += bytes((value & 0xFF, value >> 8))

    return data


def main() -> None:
    parser = argparse.ArgumentParser(description="生成 ST7789 135x240 RGB565 图片")
    parser.add_argument("input", nargs="?", default=str(DEFAULT_INPUT), help="输入照片路径")
    parser.add_argument("--bin", default=str(DEFAULT_BIN), help="输出的 .rgb565 文件")
    parser.add_argument("--preview", default=str(DEFAULT_PREVIEW), help="输出的预览 PNG")
    parser.add_argument(
        "--mode",
        choices=("wallpaper", "fill"),
        default="wallpaper",
        help="wallpaper=上下模糊背景，fill=旋转后铺满屏幕",
    )
    parser.add_argument(
        "--rotate",
        type=int,
        choices=(0, 90, 180, 270),
        default=0,
        help="顺时针旋转角度（90=照片转 90 度）",
    )
    parser.add_argument(
        "--background",
        choices=("blur", "black"),
        default="blur",
        help="上下背景：模糊原图或纯黑（仅 wallpaper 模式）",
    )
    parser.add_argument("--bg-brightness", type=float, default=0.38, help="背景亮度 0~1")
    parser.add_argument("--blur-radius", type=float, default=6.0, help="背景模糊半径")
    parser.add_argument("--big-endian", action="store_true", help="输出大端字节序")
    args = parser.parse_args()

    input_path = resolve(args.input)
    bin_path = resolve(args.bin)
    preview_path = resolve(args.preview)

    img = Image.open(input_path).convert("RGB")
    if args.rotate:
        # Pillow 的 rotate 是逆时针；这里转成顺时针，方便理解
        img = img.rotate(-args.rotate, expand=True)

    if args.mode == "fill":
        canvas = make_fill_canvas(img)
    else:
        canvas = make_canvas(img, args.background, args.bg_brightness, args.blur_radius)

    preview_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(preview_path)

    data = to_rgb565(canvas, args.big_endian)
    expected = SCREEN_W * SCREEN_H * 2
    if len(data) != expected:
        raise RuntimeError(f"output size mismatch: {len(data)} != {expected}")

    bin_path.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(data)

    print(f"input:   {input_path}")
    print(f"preview: {preview_path} ({canvas.width}x{canvas.height})")
    print(f"rgb565:  {bin_path} ({len(data)} bytes, "
          f"{'big-endian' if args.big_endian else 'little-endian'})")


if __name__ == "__main__":
    main()
