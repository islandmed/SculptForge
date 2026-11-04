# SculptForge bridge for Blender: the bytes the Bridge file format needs, as is. They are the format's, not
# SculptForge's names, and live only here (docs/legal/trademarks.md, tools/marks_allow.txt).
HEADER = b"GoZb 1.0 ZBrush GoZ Binary" + b"." * 6
MAGIC = b"GoZb"
NAME_PREFIX = b"GoZMesh_"
EXT = ".GoZ"
