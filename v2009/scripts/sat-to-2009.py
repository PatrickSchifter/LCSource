#!/usr/bin/env python3
"""Convert a zone attribute map (.sat) from the ported engine's format (uint16 LE flags per cell,
MATT_* in Engine/Brushes/Brush.h, what ZoneMapExport writes) to the 2009 server's (one byte per
cell, MAPATT_* in GameServer/Config.h: 0 field, 10 peace, 20/30 production, 40/50 stairs,
60 war, 70 free PK, 255 blocked).

    v2009/scripts/sat-to-2009.py in.sat out.sat
"""
import struct
import sys

MATT = [  # most specific first, as one cell gets one code
    (0x8000, 255),  # MATT_UNWALKABLE
    (0x0080, 70),   # MATT_FREEPKZONE
    (0x0040, 60),   # MATT_WAR
    (0x0020, 50),   # MATT_STAIR_DOWN
    (0x0010, 40),   # MATT_STAIR_UP
    (0x0008, 30),   # MATT_PRODUCT_PRIVATE
    (0x0004, 20),   # MATT_PRODUCT_PUBLIC
    (0x0002, 10),   # MATT_PEACE
]

def code(flags):
    for bit, value in MATT:
        if flags & bit:
            return value
    return 0

data = open(sys.argv[1], "rb").read()
cells = struct.unpack("<%dH" % (len(data) // 2), data)
open(sys.argv[2], "wb").write(bytes(code(c) for c in cells))
