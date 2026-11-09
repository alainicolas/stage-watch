#!/usr/bin/env python3
"""Génère firmware/src/logo.h (RGB565) à partir d'un PNG sur fond noir.
Usage : python3 firmware/tools/make_logo.py firmware/tools/improlex.png [largeur_max hauteur_max]
Nécessite Pillow (pip install pillow)."""
import os, sys
from PIL import Image

src = sys.argv[1]
maxw, maxh = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (200, 42)
im = Image.open(src).convert("RGB")
im = im.crop(im.point(lambda v: 255 if v > 30 else 0).convert("L").getbbox())  # retire le noir autour
im.thumbnail((maxw, maxh), Image.LANCZOS)
w, h = im.size
px = im.load()
vals = [((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3) for y in range(h) for x in range(w) for r, g, b in [px[x, y]]]
out = os.path.join(os.path.dirname(__file__), "..", "src", "logo.h")
with open(out, "w") as f:
    f.write(f"#pragma once\n// Logo (écran info), RGB565, généré par tools/make_logo.py depuis {os.path.basename(src)}\n#include <pgmspace.h>\n")
    f.write(f"#define LOGO_W {w}\n#define LOGO_H {h}\nconst uint16_t LOGO[] PROGMEM = {{\n")
    for i in range(0, len(vals), 16):
        f.write(",".join("0x%04X" % v for v in vals[i:i + 16]) + ",\n")
    f.write("};\n")
print(f"logo.h : {w}x{h}")
