#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares two PNGs of the same size (8-bit RGB / RGBA, as the UI tests' shots): exits 1 when more than FRACTION of
the pixels differ by more than TOLERANCE levels in any channel, or any pixel by more than MAX levels.

    png_diff.py A.png B.png [--tolerance 2] [--fraction 0.001] [--max 24] [--crop X0 Y0 X1 Y1]

--crop compares only that rectangle (pixels, X1 / Y1 exclusive), e.g. the 3D view of a UI test's shot.
"""
import argparse
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, width, height, channels = 8, b"", 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            if depth != 8 or color not in (2, 6) or body[12] != 0:
                sys.exit(f"{path}: only 8-bit RGB / RGBA, not interlaced")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw, stride = zlib.decompress(idat), width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b, c = prev[x], prev[x - channels] if x >= channels else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, channels, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tolerance", type=int, default=2)
    ap.add_argument("--fraction", type=float, default=0.001)
    ap.add_argument("--max", type=int, default=24)
    ap.add_argument("--crop", type=int, nargs=4, metavar=("X0", "Y0", "X1", "Y1"))
    o = ap.parse_args()
    wa, ha, ca, ra = read_png(o.a)
    wb, hb, cb, rb = read_png(o.b)
    if (wa, ha) != (wb, hb):
        sys.exit(f"sizes differ: {wa}x{ha} vs {wb}x{hb}")
    x0, y0, x1, y1 = o.crop if o.crop else (0, 0, wa, ha)
    x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, wa), min(y1, ha)
    over, differ, worst, total = 0, 0, 0, max((x1 - x0) * (y1 - y0), 1)
    for y in range(y0, y1):
        la, lb = ra[y], rb[y]
        for x in range(x0, x1):
            d = max(abs(la[x * ca + k] - lb[x * cb + k]) for k in range(3))
            worst = max(worst, d)
            differ += d > 0
            over += d > o.tolerance
    share = over / total
    print(f"png_diff: {over} of {total} pixels differ by more than {o.tolerance} levels ({100 * share:.4f}%), "
          f"{differ} at all ({100 * differ / total:.4f}%), worst {worst}")
    sys.exit(0 if share <= o.fraction and worst <= o.max else 1)


if __name__ == "__main__":
    main()
