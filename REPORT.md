# fix/first-sculpt-image: the first-sculpt lesson makes a cute owl head (replaces the mushroom house)
- Lesson (docs/guide/first-sculpt.md, 12 steps, ~10 min, built-in tools only): Move ear tufts, Alt+Clay face discs, Inflate/Move/Smooth beak, Divide x3 (5.2M tris), Inflate eyes, Clay brows, brown Fill, soft cream face, hard-edged amber eyes + pupils + glints, orange beak, darker tufts, White + PBR, 3/4 Save Render (2210x1549, downsampled for the guide).
- Every step sets the brush size with S + typed number (brushes keep their own size since #78; the old script flooded white/yellow because of it).
- Images: tests/ui/first_sculpt.sfui -> `make_images.py --first-sculpt` -> docs/guide/images/first-sculpt/ (13 JPEGs) and images/wn-first-sculpt.jpg (600x340 card, from the 2x render). Every image reviewed at full size.
- Text: index blurb, What's New 0.10.9 card line/highlight, help tooltip and welcome tooltip say owl; CHANGELOG [Unreleased] Changed. Guide rebuilt.
- Tests: see the result line below.
