#!/usr/bin/env python3
"""Prints what the test program (tests/make-testprog.py) left in the work
memory, from a dump of the WRAM domain: one name=value a line.

usage: results.py WRAM.bin [NAME]   (with a NAME, its value alone)"""
import sys

w = open(sys.argv[1], "rb").read()
if len(w) < 8:
    sys.exit("too short to be the work memory")
v = {
    "pad": w[0],            # the buttons on the last frame
    "padSeen": w[1],        # every button pressed so far
    "frames": w[2] | w[3] << 8,
    "clockSeconds": w[4],   # the cartridge clock's seconds
    "starts": w[5],         # how many times the cartridge was started
    "linkIn": w[6],         # the last byte that came through the link port
    "linkCount": w[7],      # how many transfers finished (counting on from 255 to 0)
}
if len(sys.argv) > 2:
    print(v[sys.argv[2]])
else:
    for k, x in v.items():
        print(f"{k}={x}")
