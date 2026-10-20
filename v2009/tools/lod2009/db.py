"""Read-only access to the server's data DB (newproject_data, the 2016 dump).

The 2009 client data files were originally produced by the LOD maker straight
from these tables, and the server running against this DB is the authority for
every item/skill/quest/catalog index the client will receive. Queries go
through the `mysql` CLI (no Python driver needed) and are cached as JSON under
LC_2009_CACHE so later builds work without the DB (`--offline`).
"""
import json
import os
import subprocess

_MEMO = {}

from common import OUT_DATA

# lc2009_data: the 2009 server's DB (newproject_data + v2009/server-config/db-fixes.sql:
# options cut to 7 levels, jobs renumbered to 2009, 2009-unsupported rows disabled).
# newproject_data: the untouched Feb/2016 dump used by the 2015 port (--db newproject_data).
DB_NAME = os.environ.get("LC_DB_NAME", "lc2009_data")
DB_USER = os.environ.get("LC_DB_USER", "lcserver")
DB_PASS = os.environ.get("LC_DB_PASS", "lcserver")
DB_HOST = os.environ.get("LC_DB_HOST", "127.0.0.1")
CACHE_ROOT = os.environ.get("LC_2009_CACHE", os.path.join(os.path.dirname(OUT_DATA), "cache"))


def set_db(name):
    global DB_NAME
    DB_NAME = name
    _MEMO.clear()


def cache_dir():
    return os.path.join(CACHE_ROOT, DB_NAME)

OFFLINE = False


def _unescape(s):
    if s == "NULL":
        return None
    if "\\" not in s:
        return s
    out = []
    i = 0
    m = {"n": "\n", "t": "\t", "\\": "\\", "0": "\0", "r": "\r"}
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            out.append(m.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def query(name, sql):
    """Return list of dicts. `name` is the cache key."""
    if name in _MEMO:
        return _MEMO[name]
    os.makedirs(cache_dir(), exist_ok=True)
    path = os.path.join(cache_dir(), name + ".json")
    if OFFLINE:
        with open(path, encoding="utf-8") as f:
            _MEMO[name] = json.load(f)
            return _MEMO[name]
    cmd = ["mysql", "-h", DB_HOST, "-u" + DB_USER, "-p" + DB_PASS, DB_NAME,
           "--default-character-set=utf8mb4", "-B", "-e", sql]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout.decode("utf-8", "replace")
    lines = raw.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    cols = lines[0].split("\t")
    rows = []
    for ln in lines[1:]:
        vals = [_unescape(v) for v in ln.split("\t")]
        rows.append(dict(zip(cols, vals)))
    with open(path, "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False)
    _MEMO[name] = rows
    return rows


def I(v, default=0):
    """DB value -> int (NULL/'' -> default)."""
    if v is None or v == "":
        return default
    try:
        return int(v)
    except ValueError:
        return int(float(v))


def F(v, default=0.0):
    if v is None or v == "":
        return default
    return float(v)


def S(v):
    return "" if v is None else v
