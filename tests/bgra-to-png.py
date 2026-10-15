#!/usr/bin/env python3
"""Writes a picture the test runners leave (--vid-out: "W H\n" then BGRA
rows) as a PNG, to look at. usage: bgra-to-png.py IN OUT"""
import struct
import sys
import zlib

data = open(sys.argv[1], "rb").read()
head, _, pixels = data.partition(b"\n")
w, h = (int(v) for v in head.split())
rows = b"".join(b"\0" + bytes(px for i in range(w) for px in (pixels[(y * w + i) * 4 + 2], pixels[(y * w + i) * 4 + 1], pixels[(y * w + i) * 4]))
                for y in range(h))
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
