#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw BotW VR's own art (docs/bettervr-ps5): its home screen tile (sce_sys/icon0.png, 512x512) and
background (sce_sys/pic0.dds, 3840x2160 BC7, tools/render-presentation.py's encoder).

    render-botwvr-art.py SCE_SYS_DIR [PREVIEW_PNG]

An original picture, nothing of Nintendo's: a headset's visor with two lenses, in white over a dusk
gradient, with "BotW VR" in the launcher's Lexend SemiBold. The app shows these until it finds the
player's own Breath of the Wild, then makes its tile and background from that game's box art and
boot screen (port/bettervr/TileArt.cpp). The output is committed, so the build needs none of this.
"""

import importlib.util
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("presentation", os.path.join(HERE, "render-presentation.py"))
presentation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(presentation)

FONT = os.path.join(HERE, "..", "port", "ui", "fonts", "Lexend-SemiBold.ttf")
W, H = presentation.W, presentation.H
TOP, BOTTOM = np.array([18, 52, 70]), np.array([8, 14, 22])  # dusk teal into night
GLOW = (120, 210, 190)


def gradient(width, height):
    t = np.linspace(0.0, 1.0, height)[:, None, None]
    rows = TOP[None, None, :] * (1 - t) + BOTTOM[None, None, :] * t
    image = np.repeat(rows, width, axis=1)
    # a soft light from above the visor
    yy, xx = np.mgrid[0:height, 0:width]
    d = np.sqrt(((xx - width / 2) / (width * 0.45)) ** 2 + ((yy - height * 0.38) / (height * 0.45)) ** 2)
    image += (np.clip(1 - d, 0, 1) ** 2)[:, :, None] * np.array([30, 60, 55])[None, None, :]
    return Image.fromarray(np.clip(image, 0, 255).astype(np.uint8), "RGB")


def visor(size):
    """The headset: a rounded visor with two round lenses, white on transparent, size wide."""
    w, h = size, int(size * 0.5)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    line = max(4, size // 28)
    draw.rounded_rectangle((line, line, w - line, h - line), radius=h // 2 - line, outline=(255, 255, 255, 255), width=line)
    r = int(h * 0.26)
    for cx in (w * 0.31, w * 0.69):
        draw.ellipse((cx - r, h / 2 - r, cx + r, h / 2 + r), outline=(255, 255, 255, 255), width=line)
    return layer


def glowing(layer, blur):
    # room around it for the glow to fade out
    padded = Image.new("RGBA", (layer.width + blur * 8, layer.height + blur * 8), (0, 0, 0, 0))
    padded.alpha_composite(layer, (blur * 4, blur * 4))
    layer = padded
    glow = Image.new("RGBA", layer.size, GLOW + (0,))
    glow.putalpha(layer.getchannel("A").filter(ImageFilter.GaussianBlur(blur)))
    out = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    out.alpha_composite(glow)
    out.alpha_composite(glow)
    out.alpha_composite(layer)
    return out


def lockup(canvas, width, centre_y, text_size, gap):
    v = glowing(visor(width), width // 30)
    canvas.alpha_composite(v, ((canvas.width - v.width) // 2, int(centre_y - v.height / 2)))
    font = ImageFont.truetype(FONT, text_size)
    draw = ImageDraw.Draw(canvas)
    box = draw.textbbox((0, 0), "BotW VR", font=font)
    x = (canvas.width - (box[2] - box[0])) // 2 - box[0]
    y = int(centre_y + v.height / 2 + gap) - box[1]
    draw.text((x, y), "BotW VR", font=font, fill=(240, 248, 245, 255))


def picture():
    canvas = gradient(W, H).convert("RGBA")
    # above the middle: the home screen draws its title and Play button over the lower left
    lockup(canvas, 1300, H * 0.30, 190, 70)
    return canvas.convert("RGB")


def tile():
    canvas = gradient(512, 512).convert("RGBA")
    lockup(canvas, 360, 200, 78, 34)
    return canvas.convert("RGB")


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    out = sys.argv[1]
    rgb = np.asarray(picture())
    blocks = presentation.encode_bc7(rgb)
    decoded = presentation.decode_bc7(blocks, W, H)
    error = np.sqrt(np.mean((decoded.astype(np.float64) - rgb) ** 2))
    print(f"pic0.dds: BC7, {len(blocks)} bytes, RMS error {error:.2f}")
    presentation.write_dds(os.path.join(out, "pic0.dds"), blocks)
    if len(sys.argv) == 3:
        Image.fromarray(decoded, "RGB").save(sys.argv[2])
    tile().save(os.path.join(out, "icon0.png"))
    print("icon0.png: 512x512")


if __name__ == "__main__":
    main()
