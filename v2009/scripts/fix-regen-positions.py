#!/usr/bin/env python3
"""Move NPC regen points and shop keepers that the installed zone maps reject to the nearest valid cell.

    v2009/scripts/fix-regen-positions.py [--out FILE] [--ref ZONE:LAYER=SAT ...] [--report]
                                         [--monsters-only] [--far-monsters disable|move] ZONE...

Files in the repo (regenerate with):
    fix-regen-positions.py 0 --out v2009/server-config/zone0-oldjuno.sql --report
    fix-regen-positions.py --monsters-only 4 9 17 23 32 --out v2009/server-config/zone-maps-regen.sql

lc2009_data comes from the Feb/2016 dump, laid out for the maps of that time (zone 0: the new
Juno, juno.wld). The 2009 GameServer reads other maps (zone 0: the old Juno, startzone.wld; some
zones: maps converted from the 2015 port), so some t_npc_regen rows land on cells the server
refuses. GameServer/Area.cpp (CArea::LoadNPC regen loop) and MobActivity.cpp skip a regen row and
log "REGEN ERROR" when the NPC proto lacks NPC_PEACEFUL and GetAttr(y, x, z) is MAPATT_BLOCK (255,
also off the map or a missing layer) or MAPATT_PEACEZONE (10). GetAttr reads layer y / 2 of the
.sat at cell ((int)x, (int)z). Peaceful NPCs and shop keepers (Area.cpp, shop loop: skipped only
on MAPATT_WARZONE) are never refused, but one on a blocked cell stands inside a wall, so this tool
treats blocked as invalid for them too.

Each invalid row moves to the nearest cell (same y layer) that passes the test, with a one-cell
margin of valid cells around it when there is one within reach. When a reference map is given
(--ref, default for zone 0: the new Juno StartZone .sat of the BR2014 client), a cell of the same
class as the row's cell in the reference map is preferred unless it is more than PREFER_SLACK m
farther: peace-zone town NPCs stay in the peace zone, monsters stay in the field. A moved monster
only goes to plain field (0), a moved NPC to any walkable cell but war / free-PK zones (or the
reference class). A monster row whose nearest such cell is more than FAR m away is in an area the
map does not have at all (e.g. inside the old Juno town walls): it gets a_total_num=0 (the server
would never spawn it anyway) unless --far-monsters move.

Writes UPDATEs keyed by a_index (t_npc_regen) / a_keeper_idx (t_shop) to --out (default
v2009/server-config/zone<N>-regen.sql), run by setup-db.sh after db-fixes.sql. The statements set
absolute values, so applying them again changes nothing. Each one records the original position
in a trailing comment; when FILE already exists those originals are used as the starting point,
so regenerating after the fix was applied gives the same file.

Reads the maps installed in v2009/run/GameServer (install-zone-maps.py) and lc2009_data.
"""
import argparse
import math
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RUN = ROOT / "run" / "GameServer"
CLIENT_BR = pathlib.Path(os.environ.get("LC_CLIENT_DIR", pathlib.Path.home() / "Projects/LC-BR2014"))
DEFAULT_REFS = {(0, 0): CLIENT_BR / "Data/World/StartZone/StartZone_1536_1536_0.sat"}

BLOCK, PEACE = 255, 10
NPC_SHOPPER, NPC_PEACEFUL = 1 << 0, 1 << 4
FAR = 30.0          # moves longer than this (m) are reported
PREFER_SLACK = 15.0  # how much farther a cell of the preferred class may be
MAX_RADIUS = 600     # cells


def sql(query):
    out = subprocess.run(["mysql", "--default-character-set=utf8", "-N", "-B", "lc2009_data", "-e", query],
                         check=True, capture_output=True, text=True).stdout
    return [line.split("\t") for line in out.splitlines()]


class Layer:
    def __init__(self, w, h, sat, sht):
        self.w, self.h, self.sat, self.sht = w, h, sat, sht

    def attr(self, ix, iz):
        if 0 <= ix < self.w and 0 <= iz < self.h:
            return self.sat[iz * self.w + ix]
        return BLOCK

    def height(self, ix, iz):
        i = iz * self.w + ix
        return int.from_bytes(self.sht[2 * i:2 * i + 2], "big") / 100.0


def load_zone(zone):
    row = sql(f"SELECT a_width, a_height, a_attrmap, a_heightmap FROM t_zonedata WHERE a_zone_index={zone}")
    if not row:
        sys.exit(f"zone {zone}: not in t_zonedata")
    w, h, attrs, heights = row[0]
    w, h = int(w), int(h)
    layers = []
    for a, b in zip(attrs.split(), heights.split()):
        sat, sht = (RUN / a).read_bytes(), (RUN / b).read_bytes()
        if len(sat) != w * h or len(sht) != 2 * w * h:
            sys.exit(f"zone {zone}: {a} / {b} have the wrong size")
        layers.append(Layer(w, h, sat, sht))
    return w, h, layers


def server_attr(layers, y, x, z):
    """CArea::GetAttr: layer y / 2 (C division), cell ((int)x, (int)z)."""
    li = int(y / 2)
    if li < 0 or li >= len(layers):
        return BLOCK
    return layers[li].attr(int(x), int(z))


def ok_for(kind):
    if kind == "monster":  # Area.cpp regen check for protos without NPC_PEACEFUL
        return lambda a: a not in (BLOCK, PEACE)
    return lambda a: a != BLOCK


def nearest(layer, x, z, ok, prefer=None):
    """Nearest cell to (x, z) whose attr passes ok. Cells whose 8 neighbours pass too (margin) and
    cells of the preferred class win when at most PREFER_SLACK m farther. (ix, iz, d) or None."""
    cx, cz = int(x), int(z)
    best = {}  # (margin, preferred) -> (d, ix, iz)
    first = None  # distance of the nearest valid cell
    for r in range(MAX_RADIUS + 1):
        if first is not None and r - 1.5 > first + PREFER_SLACK + 10:
            break
        if r == 0:
            ring = [(cx, cz)]
        else:
            ring = [(cx + d, cz + s) for d in range(-r, r + 1) for s in (-r, r)] + \
                   [(cx + s, cz + d) for d in range(-r + 1, r) for s in (-r, r)]
        for ix, iz in ring:
            a = layer.attr(ix, iz)
            if not ok(a):
                continue
            d = math.hypot(ix + 0.5 - x, iz + 0.5 - z)
            first = d if first is None else min(first, d)
            margin = all(ok(layer.attr(ix + i, iz + j)) for i in (-1, 0, 1) for j in (-1, 0, 1))
            pref = prefer is not None and prefer(a)
            for key in {(margin, pref), (False, pref), (margin, False), (False, False)}:
                if key not in best or d < best[key][0]:
                    best[key] = (d, ix, iz)
    if not best:
        return None
    base = best.get((True, False))
    if base is None or base[0] > best[(False, False)][0] + PREFER_SLACK:
        base = best[(False, False)]
    for key in ((True, True), (False, True)):
        if key in best and best[key][0] <= base[0] + PREFER_SLACK:
            base = best[key]
            break
    d, ix, iz = base
    return ix, iz, d


def load_originals(path):
    """Original positions recorded in a previously written file: {(table, key): (x, z, h)}."""
    orig = {}
    if path.exists():
        pat = re.compile(r"UPDATE (\w+) .* WHERE (?:a_index|a_keeper_idx)=(\d+) .*-- was x=(\S+) z=(\S+) h=(\S+)")
        for line in path.read_text().splitlines():
            m = pat.match(line)
            if m:
                orig[(m[1], int(m[2]))] = (float(m[3]), float(m[4]), float(m[5]))
    return orig


def fmt(v):
    return f"{v:g}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("zones", type=int, nargs="+")
    ap.add_argument("--out", type=pathlib.Path, help="SQL file (default server-config/zone<N>-regen.sql)")
    ap.add_argument("--ref", action="append", default=[], metavar="ZONE:LAYER=SAT",
                    help="reference .sat the rows were laid out for (one byte per cell, same size)")
    ap.add_argument("--report", action="store_true", help="also list every non-monster NPC and shop")
    ap.add_argument("--monsters-only", action="store_true",
                    help="only fix what the server refuses (monster rows); leave peaceful NPCs and shops")
    ap.add_argument("--far-monsters", choices=("disable", "move"), default="disable",
                    help=f"monster rows with no valid cell within {FAR:g} m: set a_total_num=0 (default)"
                         " or move them anyway")
    args = ap.parse_args()

    refs = dict(DEFAULT_REFS)
    for spec in args.ref:
        key, path = spec.split("=", 1)
        z, l = key.split(":")
        refs[(int(z), int(l))] = pathlib.Path(path)

    out = args.out or ROOT / "server-config" / f"zone{'-'.join(map(str, args.zones))}-regen.sql"
    orig = load_originals(out)
    lines = [f"-- Generated by v2009/scripts/fix-regen-positions.py {' '.join(map(str, args.zones))}; do not edit.",
             "-- NPC regen points / shop keepers moved off cells the installed zone maps reject",
             "-- (see the script). Idempotent; applied by v2009/scripts/setup-db.sh.", "",
             "USE lc2009_data;", ""]
    total_bad = 0
    for zone in args.zones:
        w, h, layers = load_zone(zone)
        ref = {}
        for (rz, rl), path in refs.items():
            if rz == zone and path.exists() and path.stat().st_size == w * h:
                ref[rl] = Layer(w, h, path.read_bytes(), bytes(2 * w * h))
        rows = []
        for idx, npc, x, z, hh, y, flag, total, usa, kor in sql(
                "SELECT r.a_index, r.a_npc_idx, r.a_pos_x, r.a_pos_z, r.a_pos_h, r.a_y_layer, n.a_flag, r.a_total_num,"
                " n.a_name_usa, n.a_name FROM t_npc_regen r"
                f" JOIN t_npc n ON n.a_index=r.a_npc_idx WHERE r.a_zone_num={zone} ORDER BY r.a_index"):
            name = usa or kor
            if total == "0" and ("t_npc_regen", int(idx)) not in orig:
                continue  # never spawned (CArea::LoadNPC skips a_total_num 0)
            kind = "npc" if int(flag) & NPC_PEACEFUL else "monster"
            rows.append(("t_npc_regen", "a_index", int(idx), int(npc), float(x), float(z), float(hh), int(y),
                         int(flag), name, kind))
        for idx, x, z, hh, y, flag, usa, kor in sql(
                "SELECT s.a_keeper_idx, s.a_pos_x, s.a_pos_z, s.a_pos_h, s.a_y_layer, IFNULL(n.a_flag, 0),"
                " IFNULL(n.a_name_usa, s.a_name_usa), s.a_name FROM t_shop s"
                f" LEFT JOIN t_npc n ON n.a_index=s.a_keeper_idx WHERE s.a_zone_num={zone} ORDER BY 1"):
            name = usa if usa not in ("", "NULL") else kor
            rows.append(("t_shop", "a_keeper_idx", int(idx), int(idx), float(x), float(z), float(hh), int(y),
                         int(flag), name, "shop"))

        lines.append(f"-- zone {zone}")
        bad = far = disabled = 0
        table_rows = []
        for table, keycol, key, npc, x, z, hh, y, flag, name, kind in rows:
            if (table, key) in orig:
                x, z, hh = orig[(table, key)]
            ok = ok_for(kind)
            # where a moved row may go: a monster to plain field, an NPC anywhere walkable but war /
            # free-PK zones, or a cell of the class it had in the reference map
            plain = (0,) if kind == "monster" else (0, PEACE, 20, 30, 40, 50)
            target = lambda c, ok=ok, plain=plain: ok(c) and (c in plain or c == ra)
            a = server_attr(layers, y, x, z)
            li = int(y / 2)
            ra = ref[li].attr(int(x), int(z)) if li in ref else None
            new = None
            if not ok(a) and not (args.monsters_only and kind != "monster"):
                bad += 1
                prefer = None
                if ra is not None and ok(ra):
                    prefer = (lambda c: c == PEACE) if ra == PEACE else (lambda c, ra=ra: c == ra)
                elif kind != "monster":
                    prefer = lambda c: c == PEACE
                hit = nearest(layers[li], x, z, target, prefer) if 0 <= li < len(layers) else None
                if hit is None:
                    print(f"zone {zone} {table} {key} npc {npc} {name}: no valid cell within {MAX_RADIUS}",
                          file=sys.stderr)
                else:
                    ix, iz, d = hit
                    nx, nz = ix + 0.5, iz + 0.5
                    nh = layers[li].height(ix, iz)
                    new = (nx, nz, nh, d, layers[li].attr(ix, iz))
                    if kind == "monster" and d > FAR and args.far_monsters == "disable":
                        new = None
                        disabled += 1
                        lines.append(f"UPDATE {table} SET a_total_num=0 WHERE {keycol}={key} AND a_zone_num={zone};"
                                     f" -- was x={fmt(x)} z={fmt(z)} h={fmt(hh)} attr={a} ref={ra}; npc {npc} {name};"
                                     f" nearest valid cell {d:.1f} m away, disabled")
                        print(f"DISABLED zone {zone} {table} {key} npc {npc} {name}: ({x:.1f},{z:.1f}) attr {a}"
                              f" ref {ra}, nearest valid cell ({nx},{nz}) {d:.1f} m")
                        continue
                    lines.append(f"UPDATE {table} SET a_pos_x={fmt(nx)}, a_pos_z={fmt(nz)}, a_pos_h={nh:.2f}"
                                 f" WHERE {keycol}={key} AND a_zone_num={zone};"
                                 f" -- was x={fmt(x)} z={fmt(z)} h={fmt(hh)} attr={a} ref={ra}; npc {npc} {name};"
                                 f" moved {d:.1f} m")
                    if d > FAR:
                        far += 1
                        print(f"FAR zone {zone} {table} {key} npc {npc} {name} ({kind}): ({x:.1f},{z:.1f}) attr {a}"
                              f" ref {ra} -> ({nx},{nz}) attr {new[4]}, {d:.1f} m")
            if kind != "monster":
                table_rows.append((table, key, npc, name, kind, flag, x, z, y, a, ra, new))
        total_bad += bad
        print(f"zone {zone}: {len(rows)} rows, {bad} invalid, {far} moved more than {FAR:g} m,"
              f" {disabled} far monster rows disabled")
        if args.report:
            print(f"\nzone {zone} non-monster NPCs (flag: S shop, Q quest, Z zone mover, G guard, K storage)")
            print("table | key | npc | name | flags | x | z | y | attr | ref attr | fixed to")
            for table, key, npc, name, kind, flag, x, z, y, a, ra, new in table_rows:
                f = "".join(c for c, bit in (("S", 0), ("Q", 8), ("Z", 5), ("G", 17), ("K", 18)) if flag >> bit & 1)
                fixed = f"({new[0]},{new[1]}) attr {new[4]} {new[3]:.0f} m" if new else ""
                print(f"{table} | {key} | {npc} | {name} | {f} | {x:.1f} | {z:.1f} | {y} | {a} | {ra} | {fixed}")
        lines.append("")
    out.write_text("\n".join(lines))
    print(f"wrote {out} ({total_bad} rows moved)")


if __name__ == "__main__":
    main()
