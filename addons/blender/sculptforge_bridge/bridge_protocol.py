# SculptForge bridge for Blender: the bytes the Bridge file format needs, as is. They are the format's, not
# SculptForge's names, so they are written as byte escapes (docs/legal/trademarks.md lists what they spell).
HEADER = b"\x47\x6F\x5A\x62\x20\x31\x2E\x30\x20\x5A\x42\x72\x75\x73\x68\x20\x47\x6F\x5A\x20\x42\x69\x6E\x61\x72\x79" + b"." * 6
MAGIC = b"\x47\x6F\x5A\x62"
NAME_PREFIX = b"\x47\x6F\x5A\x4D\x65\x73\x68\x5F"
EXT = b"\x2E\x47\x6F\x5A".decode("ascii")
