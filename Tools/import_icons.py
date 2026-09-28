"""
Brings the Godot game's icons into the Unreal port as pictures the HUD can draw.

Godot draws a vector icon for every class and every ability (its
tools/ability_icons.mjs made them, into assets/icons). Unreal's HUD draws
pictures, so this turns each SVG into a 128-pixel PNG with a see-through
background, under Content/Data/Icons: classes/<id>.png and abilities/<id>.png.
The HUD loads them at run time; nothing is imported as an asset.

It reads the Godot project and never writes to it. It needs Microsoft Edge,
which renders the SVGs exactly as a browser would, and Python with Pillow:

    python Tools/import_icons.py
"""

import glob
import os
import subprocess
import sys
import tempfile

from PIL import Image

GODOT_ICONS = r"D:\ProgramsByMe\TacticalMasters\assets\icons"
OUT = os.path.join(os.path.dirname(__file__), "..", "Content", "Data", "Icons")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
CELL = 128
ACROSS = 20


def render(svgs, sheet):
    """All the SVGs on one page, one to a cell, photographed once."""
    rows = (len(svgs) + ACROSS - 1) // ACROSS
    cells = "".join(
        f'<img src="file:///{p.replace(os.sep, "/")}" style="position:absolute;left:{(i % ACROSS) * CELL}px;'
        f'top:{(i // ACROSS) * CELL}px;width:{CELL}px;height:{CELL}px">'
        for i, p in enumerate(svgs))
    page = os.path.join(tempfile.gettempdir(), "tm_icons.html")
    with open(page, "w", encoding="utf-8") as f:
        f.write(f"<html><body style='margin:0;background:transparent'>{cells}</body></html>")
    subprocess.run([EDGE, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", "--force-device-scale-factor=1",
                    f"--window-size={ACROSS * CELL},{rows * CELL}", f"--screenshot={sheet}",
                    "file:///" + page.replace(os.sep, "/")], check=True, capture_output=True, timeout=120)
    return rows


def main():
    groups = {
        "classes": sorted(glob.glob(os.path.join(GODOT_ICONS, "*.svg"))),
        "abilities": sorted(glob.glob(os.path.join(GODOT_ICONS, "abilities", "*.svg"))),
    }
    for folder, svgs in groups.items():
        if not svgs:
            print("no icons found under", GODOT_ICONS)
            return 1
        sheet = os.path.join(tempfile.gettempdir(), f"tm_icons_{folder}.png")
        render(svgs, sheet)
        image = Image.open(sheet).convert("RGBA")
        target = os.path.join(OUT, folder)
        os.makedirs(target, exist_ok=True)
        for i, svg in enumerate(svgs):
            x, y = (i % ACROSS) * CELL, (i // ACROSS) * CELL
            name = os.path.splitext(os.path.basename(svg))[0]
            image.crop((x, y, x + CELL, y + CELL)).save(os.path.join(target, name + ".png"))
        print(f"{len(svgs)} {folder} icons -> {os.path.normpath(target)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
