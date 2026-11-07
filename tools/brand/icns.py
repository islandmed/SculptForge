#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The macOS .icns container: writing it (make_icons.py) and decoding every representation (ctest icns).

No Pillow, so the check runs on every CI machine. The 16 and 32 px 1x slots are the classic RGB + 8-bit mask pairs
(is32/s8mk, il32/l8mk): Finder's Get Info thumbnail draws PNG data in icp4/icp5 as noise (#156). Larger slots are PNG.

    tools/brand/icns.py [file.icns]   exit 1 unless every slot decodes at its size (default: the app's icon)
    tools/brand/icns.py --self-test   the RGB run-length codec round-trips
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP_ICNS = ROOT / "assets" / "branding" / "sculptforge.icns"

RGB_SLOTS = {"is32": ("s8mk", 16), "il32": ("l8mk", 32)}  # the classic pairs: run-length RGB + raw alpha
PNG_SLOTS = {"ic11": 32, "ic12": 64, "ic07": 128, "ic13": 256, "ic08": 256, "ic14": 512, "ic09": 512, "ic10": 1024}
LAYOUT = ("is32", "s8mk", "il32", "l8mk") + tuple(PNG_SLOTS)  # what write() emits, in order
GARBLED = ("icp4", "icp5", "icp6")  # PNG in these small slots shows as noise in Finder


def pack_channel(data):
    """Apple's run-length code for one 8-bit channel: n<128 copies n+1 bytes, n>=128 repeats one byte n-125 times."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        run = 1
        while i + run < n and run < 130 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes((run + 125, data[i]))
            i += run
            continue
        j = i  # literals until the next run of 3 (or 128 bytes)
        while j < n and j - i < 128 and not (j + 2 < n and data[j] == data[j + 1] == data[j + 2]):
            j += 1
        out.append(j - i - 1)
        out += data[i:j]
        i = j
    return bytes(out)


def unpack_channel(data, pos, count):
    """One channel of count bytes from data[pos:]; returns (bytes, next pos). Raises ValueError on bad data."""
    out = bytearray()
    while len(out) < count:
        if pos >= len(data):
            raise ValueError("run-length data ends early")
        c = data[pos]
        if c < 128:
            out += data[pos + 1:pos + 2 + c]
            pos += 2 + c
        else:
            if pos + 1 >= len(data):
                raise ValueError("run-length data ends early")
            out += bytes((data[pos + 1],)) * (c - 125)
            pos += 2
    if len(out) != count:
        raise ValueError(f"run-length channel is {len(out)} bytes, not {count}")
    return bytes(out), pos


def pack_rgb(rgba, n):
    """is32/il32 data from n*n RGBA bytes: the R, G and B planes, each run-length coded."""
    return b"".join(pack_channel(rgba[c::4]) for c in range(3))


def unpack_rgb(data, n):
    planes, pos = [], 0
    for _ in range(3):
        p, pos = unpack_channel(data, pos, n * n)
        planes.append(p)
    if pos != len(data):
        raise ValueError(f"{len(data) - pos} bytes left after the RGB planes")
    return planes


def chunk(kind, data):
    return kind.encode() + struct.pack(">I", 8 + len(data)) + data


def write(path, rgba_at, png_at):
    """The .icns: rgba_at(n) gives n*n RGBA bytes (16, 32 px), png_at(n) PNG bytes (the larger slots)."""
    body = b""
    for kind, (mask, n) in RGB_SLOTS.items():
        rgba = rgba_at(n)
        body += chunk(kind, pack_rgb(rgba, n)) + chunk(mask, bytes(rgba[3::4]))
    for kind, n in PNG_SLOTS.items():
        body += chunk(kind, png_at(n))
    Path(path).write_bytes(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def chunks(data):
    if data[:4] != b"icns" or struct.unpack(">I", data[4:8])[0] != len(data):
        raise ValueError("not an icns, or its length is wrong")
    i = 8
    while i < len(data):
        kind, size = data[i:i + 4].decode("latin-1"), struct.unpack(">I", data[i + 4:i + 8])[0]
        if size < 8 or i + size > len(data):
            raise ValueError(f"{kind}: bad length {size}")
        yield kind, data[i + 8:i + size]
        i += size


def decode_png(data):
    """(width, height) after inflating every row: a truncated or corrupt PNG raises."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not PNG data")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if zlib.crc32(t + body) != struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])[0]:
            raise ValueError(f"PNG {t!r} CRC mismatch")
        if t == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    w, h, depth, ctype, _, _, interlace = ihdr
    if (depth, ctype, interlace) != (8, 6, 0):
        raise ValueError(f"PNG is depth {depth} type {ctype} interlace {interlace}, not 8-bit RGBA")
    if len(zlib.decompress(idat)) != h * (1 + w * 4):
        raise ValueError("PNG pixel data is the wrong length")
    return w, h


def check(path):
    """Problems with the .icns at path (empty when every slot decodes at its size and none is missing)."""
    problems, seen = [], {}
    try:
        for kind, data in chunks(Path(path).read_bytes()):
            seen[kind] = data
    except ValueError as e:
        return [str(e)]
    for kind in GARBLED:
        if kind in seen:
            problems.append(f"{kind}: Finder shows PNG in this slot as noise; use the RGB + mask pair")
    for kind, (mask, n) in RGB_SLOTS.items():
        if kind not in seen or mask not in seen:
            problems.append(f"{kind}/{mask}: missing ({n} px)")
            continue
        try:
            unpack_rgb(seen[kind], n)
        except ValueError as e:
            problems.append(f"{kind}: {e}")
        if len(seen[mask]) != n * n:
            problems.append(f"{mask}: {len(seen[mask])} bytes, not {n * n}")
    for kind, n in PNG_SLOTS.items():
        if kind not in seen:
            problems.append(f"{kind}: missing ({n} px)")
            continue
        try:
            if decode_png(seen[kind]) != (n, n):
                problems.append(f"{kind}: {decode_png(seen[kind])}, not {n}x{n}")
        except ValueError as e:
            problems.append(f"{kind}: {e}")
    return problems


def self_test():
    import random
    rnd = random.Random(156)
    cases = [b"", b"\x01", b"\x07" * 300, bytes(range(256)) * 2, b"ab" * 200 + b"\x00" * 3 + b"c" * 131]
    cases += [bytes(rnd.choice((0, 0, 0, 9, rnd.randrange(256))) for _ in range(1024)) for _ in range(200)]
    for data in cases:
        packed = pack_channel(data)
        if unpack_channel(packed, 0, len(data)) != (data, len(packed)):
            print(f"round trip failed for {data[:16]!r}...", file=sys.stderr)
            return 1
    rgba = bytes(rnd.randrange(256) for _ in range(32 * 32 * 4))
    if unpack_rgb(pack_rgb(rgba, 32), 32) != [rgba[c::4] for c in range(3)]:
        print("RGB planes round trip failed", file=sys.stderr)
        return 1
    print("icns codec OK")
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    path = argv[1] if len(argv) > 1 else APP_ICNS
    problems = check(path)
    for p in problems:
        print(f"{path}: {p}", file=sys.stderr)
    if not problems:
        print(f"{path}: every slot decodes")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
