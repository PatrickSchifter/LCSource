#!/usr/bin/env python3
"""Read or write the client's server list file (sl.dta).

Format (CCommunicationInterface::ReadInfo in Engine/Network/CommunicationInterface.cpp):
  6 bytes padding (the last one seeds the byte key), int32 iKey, 1 byte key,
  int32 server count + iKey, then per server: int32 length + previous int32,
  followed by `length` bytes where each plain byte = byte - previous byte.
Each line is "name address port fullUsers busyUsers".

    scripts/sl-dta.py read  sl.dta
    scripts/sl-dta.py write sl.dta "LC 192.168.0.10 4001 1500 1000" [more lines...]
"""
import struct
import sys


def read(path):
    data = open(path, "rb").read()
    pos = 6
    (ikey,) = struct.unpack_from("<i", data, pos); pos += 4
    key = data[pos]; pos += 1
    (count,) = struct.unpack_from("<i", data, pos); pos += 4
    count, ikey = count - ikey, count
    lines = []
    for _ in range(count):
        (n,) = struct.unpack_from("<i", data, pos); pos += 4
        n, ikey = n - ikey, n
        out = bytearray()
        for b in data[pos:pos + n]:
            out.append((b - key) & 0xFF)
            key = b
        pos += n
        lines.append(out.decode("latin-1"))
    return lines


def write(path, lines, ikey=0x1234, key=0x5A):
    out = bytearray(b"\0" * 6)
    out += struct.pack("<i", ikey) + bytes([key])
    count = len(lines) + ikey
    out += struct.pack("<i", count)
    ikey = count
    for line in lines:
        raw = line.encode("latin-1")
        n = len(raw) + ikey
        out += struct.pack("<i", n)
        ikey = n
        for c in raw:
            b = (c + key) & 0xFF
            out.append(b)
            key = b
    open(path, "wb").write(out)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "read":
        for line in read(sys.argv[2]):
            print(line)
    elif len(sys.argv) >= 4 and sys.argv[1] == "write":
        write(sys.argv[2], sys.argv[3:])
        print("\n".join(read(sys.argv[2])))
    else:
        sys.exit(__doc__)
