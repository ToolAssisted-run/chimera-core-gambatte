#!/usr/bin/env python3
"""Says where the Game Boy's screen is in a Super Game Boy picture (a
--vid-out file: "W H" then BGRA rows), and exits 0 when it is where the
Super Game Boy puts it: 48 pixels in, 40 down, 160 by 144, with every pixel
outside it the border's colour and something drawn inside it.

usage: sgb-screen.py PICTURE.vid"""
import sys

d = open(sys.argv[1], "rb").read()
head, px = d.split(b"\n", 1)
w, h = map(int, head.split())
p = [int.from_bytes(px[i * 4:i * 4 + 4], "little") & 0xFFFFFF for i in range(w * h)]
bg = p[0]


def screen(x, y):
    return 40 <= y < 184 and 48 <= x < 208


inside = sum(1 for y in range(h) for x in range(w) if screen(x, y) and p[y * w + x] != bg)
outside = sum(1 for y in range(h) for x in range(w) if not screen(x, y) and p[y * w + x] != bg)
print("%dx%d, %d drawn pixels inside the screen's place and %d outside it" % (w, h, inside, outside))
sys.exit(0 if (w, h) == (256, 224) and inside > 100 and outside == 0 else 1)
