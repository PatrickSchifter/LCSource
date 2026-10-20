"""Shared helpers for the 2009-format Last Chaos data converters.

All files are little-endian. 2009 strings are "int length + raw bytes" (no NUL),
read with fread() straight into fixed-size char[] buffers, so a string written
here must be at most (buffer size - 1) bytes to stay NUL-terminated.
BR text is Windows-1252 (latin1 superset) in every source file.
"""
import os
import struct

BR_ROOT = os.environ.get("LC_BR_ROOT", "/Users/patrickschifter/Projects/LC-BR2014")
BR_DATA = os.path.join(BR_ROOT, "Data")
BR_STR = os.path.join(BR_ROOT, "Local", "br", "String")
OUT_DATA = os.environ.get("LC_2009_OUT", "/Users/patrickschifter/Projects/LC-2009-gen/Data")

ENC = "cp1252"
FOOTER = -9999


class Reader:
    """Sequential little-endian reader that mimics fread() on a FILE*."""

    def __init__(self, data, name="?"):
        self.d = data
        self.p = 0
        self.name = name

    @classmethod
    def open(cls, path):
        with open(path, "rb") as f:
            return cls(f.read(), os.path.basename(path))

    def left(self):
        return len(self.d) - self.p

    def eof(self):
        return self.p >= len(self.d)

    def raw(self, n):
        if n < 0 or self.p + n > len(self.d):
            raise EOFError("%s: read %d bytes at %d past end (%d)" % (self.name, n, self.p, len(self.d)))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def _u(self, fmt):
        n = struct.calcsize(fmt)
        return struct.unpack(fmt, self.raw(n))[0]

    def i32(self):
        return self._u("<i")

    def u32(self):
        return self._u("<I")

    def i64(self):
        return self._u("<q")

    def i16(self):
        return self._u("<h")

    def u8(self):
        return self._u("<B")

    def i8(self):
        return self._u("<b")

    def f32(self):
        return self._u("<f")

    def ints(self, n):
        return list(struct.unpack("<%di" % n, self.raw(4 * n)))

    def lstr(self, maxlen=None):
        """int length + bytes. maxlen = destination buffer size (checked)."""
        n = self.i32()
        if n < 0 or n > 100000:
            raise ValueError("%s: bad string length %d at %d" % (self.name, n, self.p - 4))
        if maxlen is not None and n > maxlen:
            raise ValueError("%s: string length %d overflows char[%d] at %d" % (self.name, n, maxlen, self.p - 4))
        return self.raw(n)

    def cstr(self, n):
        """Fixed char[n] field (2015 packed structs)."""
        b = self.raw(n)
        z = b.find(b"\0")
        return b if z < 0 else b[:z]


class Writer:
    def __init__(self):
        self.parts = []

    def i32(self, v):
        self.parts.append(struct.pack("<i", int(v)))

    def u32(self, v):
        self.parts.append(struct.pack("<I", int(v) & 0xFFFFFFFF))

    def i64(self, v):
        self.parts.append(struct.pack("<q", int(v)))

    def i16(self, v):
        self.parts.append(struct.pack("<h", int(v)))

    def u8(self, v):
        self.parts.append(struct.pack("<B", int(v) & 0xFF))

    def i8(self, v):
        self.parts.append(struct.pack("<b", int(v)))

    def f32(self, v):
        self.parts.append(struct.pack("<f", float(v)))

    def ints(self, vs):
        for v in vs:
            self.i32(v)

    def raw(self, b):
        self.parts.append(bytes(b))

    def lstr(self, s, bufsize):
        """Length-prefixed string fitting a char[bufsize] (keeps room for NUL)."""
        b = to_bytes(s)
        b = clip(b, bufsize - 1)
        self.i32(len(b))
        self.parts.append(b)
        return b

    def cstr(self, s, n):
        b = clip(to_bytes(s), n - 1)
        self.parts.append(b + b"\0" * (n - len(b)))

    def data(self):
        return b"".join(self.parts)

    def save(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(self.data())
        return path


def to_bytes(s):
    if s is None:
        return b""
    if isinstance(s, bytes):
        return s
    return s.encode(ENC, "replace")


def clip(b, n):
    """Truncate to n bytes; never split a trailing NUL or leave a half token."""
    if n < 0:
        n = 0
    b = b.split(b"\0", 1)[0]
    return b[:n]


def dec(b):
    return b.decode(ENC, "replace") if isinstance(b, bytes) else b


class StringTable:
    """Local/br/String/str*_br.lod (2015 TableLoader::load_file format).

    nRow, nMax, then nRow x (index, strCnt x (len, bytes)). Unlike the 2015
    loader (which drops empty strings and shifts later ones down), this keeps
    positions, so get(i, 1) is always the 2nd column.
    """

    def __init__(self, path, strcnt):
        self.path = path
        self.rows = {}
        if not os.path.exists(path):
            return
        r = Reader.open(path)
        nrow = r.i32()
        r.i32()  # nMax
        for _ in range(nrow):
            idx = r.i32()
            cols = []
            for _c in range(strcnt):
                n = r.i32()
                cols.append(r.raw(n) if n > 0 else b"")
            self.rows[idx] = cols
        self.trailing = r.left()

    def get(self, idx, col=0, default=b""):
        row = self.rows.get(idx)
        if not row or col >= len(row):
            return default
        return row[col]

    def __contains__(self, idx):
        return idx in self.rows


def load_str(name, strcnt):
    return StringTable(os.path.join(BR_STR, name + "_br.lod"), strcnt)


def br(name):
    return os.path.join(BR_DATA, name)


def out(name):
    return os.path.join(OUT_DATA, name)


# Every column-1+ string in Local/br/String carries this branding prefix
# (even the empty ones); it is not part of the real text.
BR_PREFIX = "Last Chaos Brasil © ".encode(ENC)


def strip_prefix(b):
    if isinstance(b, str):
        b = to_bytes(b)
    while b.startswith(BR_PREFIX):
        b = b[len(BR_PREFIX):]
    if b.strip() == BR_PREFIX.strip():
        b = b""
    return b


def is_garbage(b):
    """Text lost to a '?' conversion (e.g. Korean saved as cp1252): mostly '?'."""
    t = b.replace(b" ", b"")
    return bool(t) and t.count(b"?") * 2 >= len(t)


def pick(*cands):
    """First non-empty, non-garbage candidate, as cp1252 bytes.
    Returns (bytes, position) or (b"", -1)."""
    for i, c in enumerate(cands):
        b = to_bytes(c) if c is not None else b""
        b = b.split(b"\0", 1)[0]
        if b.strip() and not is_garbage(b):
            return b, i
    return b"", -1


def s32(v):
    """Low 32 bits of an integer, as a signed C int."""
    v = int(v) & 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def s8(v):
    v = int(v) & 0xFF
    return v - 0x100 if v & 0x80 else v


class Report:
    """Collects per-file notes on losses/defaults for the final summary."""

    def __init__(self, name, source):
        self.name = name
        self.source = source
        self.records = 0
        self.header = None
        self.notes = []
        self.counts = {}

    def count(self, key, n=1):
        self.counts[key] = self.counts.get(key, 0) + n

    def note(self, s):
        self.notes.append(s)

    def text(self):
        lines = ["%s  <-  %s" % (self.name, self.source),
                 "  header=%s records=%d" % (self.header, self.records)]
        for k in sorted(self.counts):
            lines.append("  %-48s %d" % (k, self.counts[k]))
        for n in self.notes:
            lines.append("  - " + n)
        return "\n".join(lines)
