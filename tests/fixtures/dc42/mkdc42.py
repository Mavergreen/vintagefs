# platform: host-agnostic -- wraps a raw floppy image in a DiskCopy 4.2 header (layout in src/dc42.c)
# usage: python3 -I mkdc42.py RAW OUT NAME FORMAT TAGBYTES [badsum]
import struct, sys
raw, out, name, fmt, tag = sys.argv[1], sys.argv[2], sys.argv[3].encode("mac_roman"), int(sys.argv[4]), int(sys.argv[5])
data = open(raw, "rb").read()
tags = bytes((i * 3 + 1) & 0xff for i in range(len(data) // 512 * tag))
def dcsum(b):
    s = 0
    for i in range(0, len(b) - 1, 2):
        s = (s + (b[i] << 8 | b[i + 1])) & 0xffffffff
        s = (s >> 1 | s << 31) & 0xffffffff
    return s
dsum = dcsum(data) ^ (1 if sys.argv[6:] == ["badsum"] else 0)
h = bytes([len(name)]) + name.ljust(63, b"\0")
h += struct.pack(">IIIIBBH", len(data), len(tags), dsum, dcsum(tags[12:]) if tags else 0, fmt, 0x22 if fmt else 0x02, 0x0100)
assert len(h) == 84
open(out, "wb").write(h + data + tags)
