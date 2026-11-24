#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mirror check for an X-symmetry step: each moved vertex's displacement against its mirror vertex's, mirrored.

  python3 tools/qa/mirror_check.py pre.obj post.obj [shot.png]

Prints the mismatch (largest and mean, as % of the largest displacement) over vertices that moved. With a front-view
shot, also writes <shot>-mirror.png: the left half beside the right half flipped (the same pattern should show twice).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from closeup import read_obj  # noqa: E402


def main():
    pre, _ = read_obj(sys.argv[1])
    post, _ = read_obj(sys.argv[2])
    lo = [min(q[i] for q in pre) for i in range(3)]
    hi = [max(q[i] for q in pre) for i in range(3)]
    cell = math.dist(lo, hi) / 400.0
    grid = {}
    for i, q in enumerate(pre):
        grid.setdefault(tuple(int(math.floor(c / cell)) for c in q), []).append(i)
    disp = [(post[i][0] - pre[i][0], post[i][1] - pre[i][1], post[i][2] - pre[i][2]) for i in range(len(pre))]
    big = max(math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2) for d in disp)
    errs = []
    for i, q in enumerate(pre):
        d = disp[i]
        if q[0] <= 0 or d == (0.0, 0.0, 0.0):
            continue
        m = (-q[0], q[1], q[2])
        k = tuple(int(math.floor(c / cell)) for c in m)
        best, bj = None, None
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for j in grid.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                        e = math.dist(m, pre[j])
                        if best is None or e < best:
                            best, bj = e, j
        if bj is None:
            continue
        dm = disp[bj]
        errs.append(math.dist(d, (-dm[0], dm[1], dm[2])) / big)
    errs.sort()
    print(f"moved +x vertices {len(errs)}: mismatch max {100 * errs[-1]:.2f}%, p99 {100 * errs[int(len(errs) * 0.99)]:.2f}%, "
          f"mean {100 * sum(errs) / len(errs):.3f}% of the largest displacement")
    if len(sys.argv) > 3:
        from PIL import Image, ImageOps
        im = Image.open(sys.argv[3]).convert("RGB")
        w, h = im.size
        cx = int(sys.argv[4]) if len(sys.argv) > 4 else w // 2
        half = min(cx, w - cx)
        left = im.crop((cx - half, 0, cx, h))
        right = ImageOps.mirror(im.crop((cx, 0, cx + half, h)))
        s = Image.new("RGB", (half * 2 + 8, h), (20, 20, 20))
        s.paste(left, (0, 0))
        s.paste(right, (half + 8, 0))
        s.save(str(Path(sys.argv[3]).with_suffix("")) + "-mirror.png")


if __name__ == "__main__":
    main()
