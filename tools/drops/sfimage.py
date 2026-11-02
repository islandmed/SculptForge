# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small image helpers for the daily drops, standard library only.

A grid is a list of rows of floats (0..1, row 0 at the top). PNGs are 8-bit gray, RGB or RGBA,
non-interlaced: what the generator writes and `sculptforge --render` produces.
"""

import math
import struct
import zlib


# --- PNG ---

def _chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(path, rows, channels=1):
    """rows: lists of 0..255 ints (channels interleaved). Deterministic bytes for the same pixels."""
    h = len(rows)
    w = len(rows[0]) // channels
    color = {1: 0, 3: 2, 4: 6}[channels]
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    data = (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color, 0, 0, 0)) +
            _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(data)


def write_gray(path, grid):
    write_png(path, [[max(0, min(255, int(round(v * 255)))) for v in row] for row in grid], 1)


def read_png(path):
    """(width, height, channels, rows of ints). Raises ValueError on anything but 8-bit, non-interlaced."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, [], 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace or color not in (0, 2, 4, 6):
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, colour {color}, interlace {interlace})")
            ch = {0: 1, 2: 3, 4: 2, 6: 4}[color]
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if not w:
        raise ValueError(f"{path}: no IHDR")
    raw = zlib.decompress(b"".join(idat))
    stride = w * ch
    rows, prev = [], bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if ft == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                left = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, ch, rows


def luminance(path):
    """A PNG as a gray grid 0..1."""
    w, h, ch, rows = read_png(path)
    if ch < 3:
        return [[row[x * ch] / 255.0 for x in range(w)] for row in rows]
    return [[(0.299 * row[x * ch] + 0.587 * row[x * ch + 1] + 0.114 * row[x * ch + 2]) / 255.0 for x in range(w)]
            for row in rows]


def contact_sheet(path, images, cols=4, cell=(256, 192), pad=8, bg=(24, 26, 32)):
    """Tiles PNG files (nearest-neighbour scaled to `cell`) into one RGB PNG."""
    cw, chh = cell
    n = max(1, len(images))
    rows_n = (n + cols - 1) // cols
    W, H = cols * cw + (cols + 1) * pad, rows_n * chh + (rows_n + 1) * pad
    out = [bytearray(bg * W) for _ in range(H)]
    for i, p in enumerate(images):
        w, h, ch, rows = read_png(p)
        ox, oy = pad + (i % cols) * (cw + pad), pad + (i // cols) * (chh + pad)
        for y in range(chh):
            src = rows[y * h // chh]
            dst = out[oy + y]
            for x in range(cw):
                s = (x * w // cw) * ch
                rgb = (src[s],) * 3 if ch < 3 else (src[s], src[s + 1], src[s + 2])
                d = (ox + x) * 3
                dst[d:d + 3] = bytes(rgb)
    write_png(path, out, 3)


# --- Measurements ---

def resample(grid, n):
    """Box-filtered n x n copy."""
    h, w = len(grid), len(grid[0])
    out = []
    for j in range(n):
        y0, y1 = j * h // n, max(j * h // n + 1, (j + 1) * h // n)
        row = []
        for i in range(n):
            x0, x1 = i * w // n, max(i * w // n + 1, (i + 1) * w // n)
            s = sum(sum(grid[y][x0:x1]) for y in range(y0, y1))
            row.append(s / ((y1 - y0) * (x1 - x0)))
        out.append(row)
    return out


def phash(grid):
    """64-bit perceptual hash: the sign of the low 8 x 8 DCT terms of a 32 x 32 copy against their median."""
    n = 32
    g = resample(grid, n)
    cos = [[math.cos(math.pi * (2 * x + 1) * u / (2 * n)) for x in range(n)] for u in range(8)]
    rows = [[sum(g[y][x] * cos[u][x] for x in range(n)) for u in range(8)] for y in range(n)]
    coef = [sum(rows[y][u] * cos[v][y] for y in range(n)) for v in range(8) for u in range(8)]
    ac = coef[1:]
    med = sorted(ac)[len(ac) // 2]
    bits = 0
    for c in ac:
        bits = (bits << 1) | (1 if c > med else 0)
    return bits


def hamming(a, b):
    return bin(a ^ b).count("1")


def stats(grid):
    """Range, standard deviation, mean |gradient| (detail), the clipped fractions, non-finite count."""
    vals = [v for row in grid for v in row]
    bad = sum(1 for v in vals if not math.isfinite(v))
    if bad:
        return {"bad": bad, "range": 0.0, "std": 0.0, "detail": 0.0, "low": 0.0, "high": 0.0}
    n = len(vals)
    mean = sum(vals) / n
    std = math.sqrt(sum((v - mean) ** 2 for v in vals) / n)
    h, w = len(grid), len(grid[0])
    grad = sum(abs(grid[y][x + 1] - grid[y][x]) for y in range(h) for x in range(w - 1)) / (h * (w - 1))
    return {"bad": 0, "range": max(vals) - min(vals), "std": std, "detail": grad, "mean": mean,
            "low": sum(1 for v in vals if v <= 0.002) / n, "high": sum(1 for v in vals if v >= 0.998) / n}


def seam_ratio(grid):
    """The jump across the wrap-around edge against the largest jump between neighbouring columns (or rows) inside:
    about 1 or less for a seamless tile (sharp structure inside is fine), large where the tile doesn't wrap."""
    h, w = len(grid), len(grid[0])
    cols = [sum(abs(grid[y][x + 1] - grid[y][x]) for y in range(h)) / h for x in range(w - 1)]
    rows = [sum(abs(grid[y + 1][x] - grid[y][x]) for x in range(w)) / w for y in range(h - 1)]
    edge_x = sum(abs(grid[y][0] - grid[y][w - 1]) for y in range(h)) / h
    edge_y = sum(abs(grid[0][x] - grid[h - 1][x]) for x in range(w)) / w
    return max(edge_x / max(max(cols), 1e-4), edge_y / max(max(rows), 1e-4))


def border_mean(grid, band=3):
    h, w = len(grid), len(grid[0])
    vals = [grid[y][x] for y in range(h) for x in range(w) if x < band or y < band or x >= w - band or y >= h - band]
    return sum(vals) / len(vals)


def mean_abs_diff(a, b):
    h, w = min(len(a), len(b)), min(len(a[0]), len(b[0]))
    return sum(abs(a[y][x] - b[y][x]) for y in range(h) for x in range(w)) / (h * w)
