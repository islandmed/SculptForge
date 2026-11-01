#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes the two lit-sphere images custom_matcaps.sfui loads (Gold.png, Rose.png) next to this script: a sphere
# shaded with a key light, a rim and a highlight in one tint, drawn here so the test data has a known origin.
# Standard library only. Run it again to regenerate them: python3 tests/ui/data/make_test_matcaps.py
import math
import os
import struct
import zlib


def png(path, size, pixel):
    rows = bytearray()
    for y in range(size):
        rows.append(0)  # filter: none
        for x in range(size):
            rows.extend(pixel(x, y))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def lit_sphere(size, tint, shine):
    light = (-0.45, 0.55, 0.70)
    norm = math.sqrt(sum(c * c for c in light))
    light = tuple(c / norm for c in light)

    def pixel(x, y):
        u = (x + 0.5) / size * 2.0 - 1.0
        v = 1.0 - (y + 0.5) / size * 2.0
        r2 = u * u + v * v
        if r2 > 1.0:  # outside the sphere: the darkest rim colour, as lit-sphere images usually have
            u, v, r2 = u / math.sqrt(r2), v / math.sqrt(r2), 1.0
        z = math.sqrt(max(0.0, 1.0 - r2))
        diffuse = max(0.0, u * light[0] + v * light[1] + z * light[2])
        half = (light[0], light[1], light[2] + 1.0)
        hn = math.sqrt(sum(c * c for c in half))
        spec = max(0.0, (u * half[0] + v * half[1] + z * half[2]) / hn) ** 40
        rim = (1.0 - z) ** 3 * 0.35
        out = []
        for c in tint:
            value = c * (0.18 + 0.82 * diffuse) + 255.0 * (shine * spec + rim * 0.5)
            out.append(max(0, min(255, int(round(value)))))
        return bytes(out)

    return pixel


here = os.path.dirname(os.path.abspath(__file__))
png(os.path.join(here, "Gold.png"), 256, lit_sphere(256, (230, 180, 80), 0.9))
png(os.path.join(here, "Rose.png"), 64, lit_sphere(64, (220, 120, 145), 0.5))
