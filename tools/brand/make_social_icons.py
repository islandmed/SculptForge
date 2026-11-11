#!/usr/bin/env python3
"""assets/branding/social/social-icons.png: the community icons as one white-on-transparent strip, 64 px each, left to
right in brand::Social order (Discord, GitHub, X, Bluesky, Instagram, Facebook). The glyphs are the symbols of website/img/social.svg;
the app tints them (Welcome dialog, App::drawSocialRow). Needs rsvg-convert and Pillow. Run from the repo root."""
import io, re, subprocess
from PIL import Image

NAMES = ["discord", "github", "x", "bluesky", "instagram", "facebook"]
SIZE = 64
svg = open("website/img/social.svg").read()
strip = Image.new("RGBA", (SIZE * len(NAMES), SIZE), (0, 0, 0, 0))
for i, n in enumerate(NAMES):
    d = re.search(r'<symbol id="%s"[^>]*><path d="([^"]+)"' % n, svg).group(1)
    one = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="{SIZE*4}" height="{SIZE*4}"><path fill="#fff" d="{d}"/></svg>'
    png = subprocess.run(["rsvg-convert", "-w", str(SIZE * 4), "-h", str(SIZE * 4)], input=one.encode(), capture_output=True, check=True).stdout
    strip.alpha_composite(Image.open(io.BytesIO(png)).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS), (i * SIZE, 0))
strip.save("assets/branding/social/social-icons.png", optimize=True)
