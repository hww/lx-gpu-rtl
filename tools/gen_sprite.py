#!/usr/bin/env python3
from PIL import Image

HEART = [
    "................",
    "................",
    "................",
    "..###.....###...",
    ".#####...#####..",
    "#######.#######.",
    "###############.",
    "###############.",
    ".#############..",
    "..###########...",
    "...#########....",
    "....#######.....",
    ".....#####......",
    "......###.......",
    ".......#........",
    "................",
]

def websafe_palette():
    pal = []
    for r in range(6):
        for g in range(6):
            for b in range(6):
                pal.append((r*51, g*51, b*51))
    for i in range(40):
        v = (i*255+39)//40
        pal.append((v, v, v))
    return pal

PAL = websafe_palette()
COLOR = {'.': 0, '#': 196}   # 196 = web-safe (255,0,0)

img = Image.new("P", (16, 16))
img.putpalette([c for rgb in PAL for c in rgb])
px = img.load()
for y, row in enumerate(HEART):
    for x, ch in enumerate(row):
        px[x, y] = COLOR.get(ch, 0)
img.save("sim/sprite_16x16.png")
print("wrote sim/sprite_16x16.png")