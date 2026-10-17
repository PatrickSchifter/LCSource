#!/usr/bin/env python3
"""Install real zone maps (.sat/.sht) taken from a game client for the GameServer.

Some clients ship the server's attribute and height maps under Data/World. Heights
(.sht) use the format the GameServer reads (16-bit big-endian, height * 100). The
attribute maps (.sat) there are the old one-byte format (0 walkable, 10 peace zone,
... 255 blocked); the GameServer wants 16-bit MATT_* flags, so they are converted.

    scripts/convert-client-maps.py <client Data/World dir>

Files are matched by name against t_zonedata and written under run/GameServer/,
replacing placeholders from make-placeholder-maps.py.
"""
import array
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "run" / "GameServer"

# Old one-byte attribute -> MATT_* flags (Server Source/ShareLib/Config.h).
MATT_WALKABLE, MATT_UNWALKABLE = 0x0001, 0x8000
OLD_TO_FLAGS = {
    0: MATT_WALKABLE,
    10: MATT_WALKABLE | 0x0002,   # peace zone
    20: MATT_WALKABLE | 0x0004,   # public production
    30: MATT_WALKABLE | 0x0008,   # private production
    40: MATT_WALKABLE | 0x0010,   # stair up
    50: MATT_WALKABLE | 0x0020,   # stair down
    60: MATT_WALKABLE | 0x0040,   # war zone
    70: MATT_WALKABLE | 0x0080,   # free PK zone
    255: MATT_UNWALKABLE,
}


def zone_maps():
    query = "SELECT a_zone_index, a_width, a_height, a_attrmap, a_heightmap FROM t_zonedata"
    out = subprocess.run(["mariadb", "-N", "-B", "newproject_data", "-e", query],
                         check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        zone, w, h, attrs, heights = line.split("\t")
        for attr, height in zip(attrs.split(), heights.split()):
            yield int(zone), int(w), int(h), attr, height


def convert_attributes(data, cells):
    if len(data) == 2 * cells:
        return data
    if len(data) != cells:
        raise ValueError(f"unexpected size {len(data)} for {cells} cells")
    unknown = sorted(set(data) - OLD_TO_FLAGS.keys())
    if unknown:
        print(f"    unknown attributes {unknown} treated as walkable")
    flags = array.array("H", (OLD_TO_FLAGS.get(b, MATT_WALKABLE) for b in data))
    if sys.byteorder != "little":
        flags.byteswap()
    return flags.tobytes()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    world = pathlib.Path(sys.argv[1])
    found = {p.name.lower(): p for p in world.rglob("*") if p.suffix.lower() in (".sat", ".sht")}
    for zone, w, h, attr, height in zone_maps():
        src_attr = found.get(pathlib.Path(attr).name.lower())
        src_height = found.get(pathlib.Path(height).name.lower())
        if not (src_attr and src_height):
            continue
        cells = w * h
        heights = src_height.read_bytes()
        if len(heights) != 2 * cells:
            print(f"zone {zone}: {src_height.name} has size {len(heights)}, expected {2 * cells}; skipped")
            continue
        try:
            attrs = convert_attributes(src_attr.read_bytes(), cells)
        except ValueError as e:
            print(f"zone {zone}: {src_attr.name}: {e}; skipped")
            continue
        (OUT / attr).parent.mkdir(parents=True, exist_ok=True)
        (OUT / attr).write_bytes(attrs)
        (OUT / height).write_bytes(heights)
        print(f"zone {zone}: installed {pathlib.Path(attr).name}, {pathlib.Path(height).name}")


if __name__ == "__main__":
    main()
