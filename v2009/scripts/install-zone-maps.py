#!/usr/bin/env python3
"""Install the zone maps (.sat/.sht) the 2009 GameServer reads, into v2009/run/GameServer/.

    v2009/scripts/install-zone-maps.py [zone ...]

The 2009 GameServer (GameServer/MapAttr.cpp) reads, relative to its working directory and by
the paths in lc2009_data.t_zonedata (a_attrmap/a_heightmap, e.g. "data/StartZone_1536_1536_0.sat"):
  .sat  one byte per cell: 0 field, 10 peace zone, 20/30 public/private production,
        40/50 stair up/down, 60 war zone, 70 free PK, anything else blocked (255)
  .sht  uint16 big-endian, height * 100 (same as the 2015 server)
cells row by row (z outer, x inner), width * height from t_zonedata.

Sources, first match wins:
  0. maps exported from the 2009 client's own worlds (LC_SERVER_MAPS, default
     ~/Projects/LC-2009/ServerMaps/2009, written by ZoneMapExport + sat-to-2009.py). Zone 0 comes
     from here: the client draws the old Juno (startzone.wld), but the BR StartZone_*.sht is the
     new Juno's height map, which spawns players under the old terrain.
  1. the client's Data/World (LC_CLIENT_DIR, default ~/Projects/LC-BR2014): those .sat files are
     already the one-byte format.
  2. the 2015 port's run dir (LC2015_MAPS, default ~/Projects/LCSource/run/GameServer/data):
     16-bit MATT_* flags (LE) are turned back into the one-byte values.

Prints the zones that have every layer installed; pass those to the GameServer config.
"""
import array
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "run" / "GameServer"
CLIENT = pathlib.Path(os.environ.get("LC_CLIENT_DIR", pathlib.Path.home() / "Projects/LC-BR2014"))
EXPORTED = pathlib.Path(os.environ.get("LC_SERVER_MAPS",
                                       pathlib.Path.home() / "Projects/LC-2009/ServerMaps/2009"))
MAPS2015 = pathlib.Path(os.environ.get("LC2015_MAPS",
                                       pathlib.Path.home() / "Projects/LCSource/run/GameServer/data"))

MATT_WALKABLE, MATT_UNWALKABLE = 0x0001, 0x8000
# 2015 flag -> 2009 byte, most restrictive first.
FLAG_TO_OLD = [(0x0080, 70), (0x0040, 60), (0x0020, 50), (0x0010, 40), (0x0008, 30),
               (0x0004, 20), (0x0002, 10)]


def flags_to_old(f):
    if f & MATT_UNWALKABLE or not f & MATT_WALKABLE:
        return 255
    for bit, old in FLAG_TO_OLD:
        if f & bit:
            return old
    return 0


def zone_rows():
    query = "SELECT a_zone_index, a_width, a_height, a_attrmap, a_heightmap FROM t_zonedata"
    out = subprocess.run(["mysql", "-N", "-B", "lc2009_data", "-e", query],
                         check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        zone, w, h, attrs, heights = line.split("\t")
        yield int(zone), int(w), int(h), list(zip(attrs.split(), heights.split()))


def index(directory):
    if not directory.is_dir():
        return {}
    return {p.name.lower(): p for p in directory.rglob("*") if p.suffix.lower() in (".sat", ".sht")}


def main():
    wanted = {int(z) for z in sys.argv[1:]}
    exported = index(EXPORTED)
    client = index(CLIENT / "Data" / "World")
    old = index(MAPS2015)
    installed = []
    for zone, w, h, layers in sorted(zone_rows()):
        if wanted and zone not in wanted:
            continue
        cells = w * h
        ok = True
        for attr, height in layers:
            name_a, name_h = pathlib.Path(attr).name.lower(), pathlib.Path(height).name.lower()
            sat = sht = None
            src = ""
            ea, eh = exported.get(name_a), exported.get(name_h)
            ca, ch = client.get(name_a), client.get(name_h)
            if ea and eh and ea.stat().st_size == cells and eh.stat().st_size == 2 * cells:
                sat, sht, src = ea.read_bytes(), eh.read_bytes(), "exported"
            elif ca and ch and ca.stat().st_size == cells and ch.stat().st_size == 2 * cells:
                sat, sht, src = ca.read_bytes(), ch.read_bytes(), "client"
            else:
                oa, oh = old.get(name_a), old.get(name_h)
                if oa and oh and oa.stat().st_size == 2 * cells and oh.stat().st_size == 2 * cells:
                    flags = array.array("H", oa.read_bytes())
                    if sys.byteorder != "little":
                        flags.byteswap()
                    table = {f: flags_to_old(f) for f in set(flags)}
                    sat, sht, src = bytes(table[f] for f in flags), oh.read_bytes(), "2015 run"
            if sat is None:
                print(f"zone {zone}: no map for {attr} / {height}")
                ok = False
                break
            for rel, data in ((attr, sat), (height, sht)):
                dest = OUT / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
            print(f"zone {zone}: {attr} ({src})")
        if ok:
            installed.append(zone)
    print("installed zones:", " ".join(map(str, installed)))


if __name__ == "__main__":
    main()
