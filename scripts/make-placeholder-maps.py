#!/usr/bin/env python3
"""Generate placeholder zone maps (.sat/.sht) for the GameServer.

The real attribute/height maps come from the client's .wld worlds (scripts/export-zone-maps.py).
For zones without one, this writes flat maps where every cell is walkable (MATT_WALKABLE) at
height 0, so the GameServer can boot and load zones.

    scripts/make-placeholder-maps.py [zone_index ...]   (default: zones in server-config/GameServer.ini)

Files are written under run/GameServer/ using the paths stored in t_zonedata.
Existing files are never overwritten.
"""
import configparser
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "run" / "GameServer"
MATT_WALKABLE = 0x0001


def configured_zones():
    ini = configparser.ConfigParser(strict=False, interpolation=None)
    ini.optionxform = str
    ini.read(ROOT / "server-config" / "GameServer.ini")
    count = int(ini["Zones"]["Count"])
    return [int(ini[f"Zone_{i}"]["No"]) for i in range(count)
            if ini[f"Zone_{i}"].get("Remote", "FALSE") == "FALSE"]


def zone_rows(zones):
    ids = ",".join(str(z) for z in zones)
    query = (f"SELECT a_zone_index, a_width, a_height, a_attrmap, a_heightmap "
             f"FROM t_zonedata WHERE a_zone_index IN ({ids})")
    out = subprocess.run(["mariadb", "-N", "-B", "newproject_data", "-e", query],
                         check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        zone, w, h, attrs, heights = line.split("\t")
        yield int(zone), int(w), int(h), attrs.split(), heights.split()


def write_if_missing(path, data):
    if path.exists():
        print(f"  keep   {path.relative_to(ROOT)}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"  wrote  {path.relative_to(ROOT)}")


def main():
    zones = [int(z) for z in sys.argv[1:]] or configured_zones()
    for zone, w, h, attrs, heights in zone_rows(zones):
        print(f"zone {zone}: {w}x{h}, {len(attrs)} layer(s)")
        cells = w * h
        attr = MATT_WALKABLE.to_bytes(2, "little") * cells  # native (little-endian) u16
        height = (0).to_bytes(2, "big") * cells               # read back with ntohs()
        for a, hgt in zip(attrs, heights):
            write_if_missing(OUT / a, attr)
            write_if_missing(OUT / hgt, height)


if __name__ == "__main__":
    main()
