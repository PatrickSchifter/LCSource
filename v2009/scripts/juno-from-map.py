#!/usr/bin/env python3
"""Lay out the zone 0 (old Juno) monster spawns after the classic Juno map ("Juno mapped by Ambard").

    v2009/scripts/juno-from-map.py [--out FILE] [--seed N] [--plot PNG --ref-image Juno.png]

Files in the repo (regenerate with):
    juno-from-map.py --out v2009/server-config/zone0-mobs-map.sql

The reference map (814x845 px) draws each monster species where it lives in the classic Juno,
plus a few field NPCs and the gathering spots (herbs, energy, stones). Its background is the
in-game zone 0 map texture (Data/Interface/Map_World0.tex, 512x512 DXT1, which Map.dta draws at
ratio 1/3: world = 3 * texture px), so the reference was registered to that texture by maximising
the correlation of the road network (similarity fit, then a 6-parameter affine refinement); the
result, REF_TO_WORLD below, maps reference pixels to world (x, z), z growing downwards on the map
(no flip). Checked against the server .sat: Randol's walls and both rivers line up within a few m.

What it writes (UPDATEs keyed by a_index, absolute values, so applying the file again changes
nothing; applied by setup-db.sh after zone0-oldjuno.sql):
  * every zone 0 regen row of a species on the map (SPECIES) gets a new position inside the
    species' map regions (REGIONS: boxes around the drawn monsters, in reference pixels). Rows are
    shared out between the regions in proportion to their usable area, spread at random (fixed
    seed) with a minimum spacing, on plain field cells (attr 0, not blocked / peace zone, with a
    MARGIN-cell ring of field around and no cliff: height range within the ring <= MAX_SLOPE m).
    a_regen_sec is kept; a_total_num goes back to -1 (the dump value; zone0-oldjuno.sql had set 0
    on rows whose old position had no valid cell nearby).
  * gathering objects (herbs, energy, ore) go around their map spots (RESOURCES).
  * the field NPCs drawn on the map (Tamon, Teleporter Rian, Kal, Dranore) go to the nearest
    walkable cell of their icon (NPC_SPOTS).
  * every other non-peaceful NPC with zone 0 rows (newer content: Steel-horn Deer, Aged Treant,
    Shadow Jaguar, Red Godeys, the Elite mobs, event mobs, ...) gets a_total_num=0: not spawned,
    recoverable from the original values in the trailing comments.
Town NPCs (peaceful, not on the map) are left alone.

Each statement records the row's previous values in a trailing comment ("was ..."); when FILE
already exists those are reused, so regenerating after the file was applied gives the same file.
Reads the zone 0 maps installed in v2009/run/GameServer/data and lc2009_data.
"""
import argparse
import math
import pathlib
import random
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "run" / "GameServer" / "data"
SAT, SHT = DATA / "StartZone_1536_1536_0.sat", DATA / "StartZone_1536_1536_0.sht"
W = H = 1536
BLOCK, PEACE, FIELD = 255, 10, 0
NPC_PEACEFUL = 1 << 4

# world x = a*px + b*py + c ; world z = d*px + e*py + f   (reference map pixels -> world metres)
REF_TO_WORLD = (2.00426, 0.00033, -50.344, -0.00265, 1.99257, -75.374)

MARGIN = 2        # cells of field required around a spawn cell
MAX_SLOPE = 2.5   # m of height difference allowed inside that ring
PAD = 8           # px added around each drawn monster (about 16 m)
SEED = 2009

# Map label -> t_npc indices (a_name_usa). "Warewolf" is the Werewolf, "Bezerk Wolf" the Berserk
# Wolf; Little Fox (146, added with the Deer) is a fox and shares the Fox areas.
SPECIES = {
    "Drake": [53], "Elder Drake": [2], "Great Horn Beast": [120], "Horn Beast": [31],
    "Blood Wolf": [191], "Elder Werewolf": [119], "Werewolf": [1], "Treant": [4],
    "Little Treant": [52], "Sasquatch": [29], "Dire Wolf": [30], "Berserk Wolf": [117],
    "Wolf": [115], "Elder Wolf": [61], "Jaguar": [63], "Fox": [60, 146], "Deer": [145],
}

# Boxes (x0, y0, x1, y1) in reference pixels around each drawn monster of the species.
REGIONS = {
    "Elder Drake": [(50, 50, 180, 115), (30, 310, 105, 385), (195, 310, 270, 370)],
    "Drake": [(175, 55, 270, 130), (35, 135, 125, 215), (355, 55, 570, 200), (590, 55, 685, 130),
              (55, 490, 140, 570)],
    "Great Horn Beast": [(265, 75, 335, 135), (150, 130, 215, 190)],
    "Horn Beast": [(665, 115, 740, 180), (150, 365, 210, 415), (65, 595, 200, 660)],
    "Blood Wolf": [(245, 135, 300, 185), (155, 215, 200, 260), (290, 215, 335, 280), (445, 205, 495, 270),
                   (635, 190, 690, 255), (145, 420, 190, 470), (350, 440, 400, 495), (125, 515, 180, 580)],
    "Elder Werewolf": [(295, 145, 355, 200), (245, 240, 300, 300), (295, 625, 350, 690)],
    "Werewolf": [(580, 170, 640, 225), (195, 440, 250, 495), (300, 460, 355, 515), (185, 530, 240, 585)],
    "Treant": [(95, 190, 155, 265), (80, 395, 140, 470)],
    "Little Treant": [(335, 225, 395, 310), (410, 285, 470, 370), (365, 605, 425, 670)],
    "Sasquatch": [(680, 220, 745, 280), (295, 345, 365, 410), (255, 530, 415, 600), (340, 700, 415, 750)],
    "Dire Wolf": [(365, 385, 445, 435), (415, 605, 500, 650), (610, 690, 695, 740)],
    "Berserk Wolf": [(505, 230, 565, 300), (475, 640, 535, 690)],
    "Wolf": [(505, 290, 560, 350), (640, 320, 695, 375), (410, 540, 475, 600), (730, 530, 790, 585),
             (690, 665, 755, 715)],
    "Elder Wolf": [(400, 450, 465, 495), (565, 635, 630, 690)],
    "Jaguar": [(585, 265, 650, 320), (725, 455, 790, 520)],
    "Fox": [(495, 365, 575, 420), (670, 385, 745, 430), (700, 600, 785, 650)],
    "Deer": [(585, 360, 635, 425), (450, 495, 500, 555)],
}

# Gathering objects around the map's "Herbs", "Energy" and "Stones" marks: npc -> spots (ref px).
HERBS, ENERGY, STONES = [(390, 380), (558, 418)], [(628, 375), (492, 592)], [(722, 357), (752, 410)]
RESOURCES = {105: HERBS, 106: HERBS, 107: ENERGY, 108: ENERGY, 103: STONES, 104: STONES}
RESOURCE_RADIUS = 30  # m

# Field NPCs drawn on the map: npc -> icon (ref px).
NPC_SPOTS = {137: (229, 262), 54: (117, 358), 138: (240, 410), 58: (543, 692)}


def sql(query):
    out = subprocess.run(["mysql", "--default-character-set=utf8", "-N", "-B", "lc2009_data", "-e", query],
                         check=True, capture_output=True, text=True).stdout
    return [line.split("\t") for line in out.splitlines()]


def to_world(px, py):
    a, b, c, d, e, f = REF_TO_WORLD
    return a * px + b * py + c, d * px + e * py + f


class Zone:
    def __init__(self):
        self.sat, self.sht = SAT.read_bytes(), SHT.read_bytes()
        if len(self.sat) != W * H or len(self.sht) != 2 * W * H:
            sys.exit("zone 0 maps have the wrong size")

    def attr(self, ix, iz):
        return self.sat[iz * W + ix] if 0 <= ix < W and 0 <= iz < H else BLOCK

    def height(self, ix, iz):
        i = 2 * (iz * W + ix)
        return int.from_bytes(self.sht[i:i + 2], "big") / 100.0

    def spawnable(self, ix, iz):
        """Plain field with a ring of field around it and no cliff inside the ring."""
        hs = []
        for j in range(-MARGIN, MARGIN + 1):
            for i in range(-MARGIN, MARGIN + 1):
                if self.attr(ix + i, iz + j) != FIELD:
                    return False
                hs.append(self.height(ix + i, iz + j))
        return max(hs) - min(hs) <= MAX_SLOPE

    def cells_in(self, box, pad=PAD):
        x0, y0, x1, y1 = box
        corners = [to_world(px, py) for px in (x0 - pad, x1 + pad) for py in (y0 - pad, y1 + pad)]
        wx0, wx1 = int(min(c[0] for c in corners)), int(max(c[0] for c in corners))
        wz0, wz1 = int(min(c[1] for c in corners)), int(max(c[1] for c in corners))
        return [(ix, iz) for iz in range(max(wz0, 0), min(wz1, H - 1) + 1)
                for ix in range(max(wx0, 0), min(wx1, W - 1) + 1) if self.spawnable(ix, iz)]

    def cells_around(self, wx, wz, radius):
        r = int(radius) + 1
        return [(ix, iz) for iz in range(int(wz) - r, int(wz) + r + 1) for ix in range(int(wx) - r, int(wx) + r + 1)
                if math.hypot(ix + 0.5 - wx, iz + 0.5 - wz) <= radius and self.spawnable(ix, iz)]

    def nearest_walkable(self, wx, wz):
        """Nearest non-blocked cell, preferring one with a ring of non-blocked cells."""
        for want_margin in (True, False):
            for r in range(0, 300):
                ring = [(int(wx) + i, int(wz) + j) for i in range(-r, r + 1) for j in range(-r, r + 1)
                        if max(abs(i), abs(j)) == r]
                ring.sort(key=lambda c: math.hypot(c[0] + 0.5 - wx, c[1] + 0.5 - wz))
                for ix, iz in ring:
                    if self.attr(ix, iz) == BLOCK:
                        continue
                    if want_margin and any(self.attr(ix + i, iz + j) == BLOCK for i in (-1, 0, 1) for j in (-1, 0, 1)):
                        continue
                    return ix, iz
        return None


def apportion(n, weights):
    """Split n into integer shares proportional to weights (largest remainder), at least one each when n allows."""
    tot = sum(weights)
    if n == 0 or tot == 0:
        return [0] * len(weights)
    live = [i for i, w in enumerate(weights) if w > 0]
    base = [0] * len(weights)
    if n >= len(live):
        for i in live:
            base[i] = 1
    rest = n - sum(base)
    raw = [rest * w / tot for w in weights]
    shares = [b + int(r) for b, r in zip(base, raw)]
    order = sorted(range(len(weights)), key=lambda i: raw[i] - int(raw[i]), reverse=True)
    for i in order[:n - sum(shares)]:
        shares[i] += 1
    return shares


def scatter(rng, cells, n, taken):
    """Pick n cells spread out (minimum spacing shrinks until they fit); avoids cells in taken."""
    if n == 0:
        return []
    if not cells:
        raise SystemExit("no usable cell for a region")
    spacing = min(14.0, max(4.0, 0.75 * math.sqrt(len(cells) / n)))
    cells = list(cells)
    rng.shuffle(cells)
    while True:
        got = []
        for c in cells:
            if c in taken:
                continue
            if all(math.hypot(c[0] - g[0], c[1] - g[1]) >= spacing for g in got):
                got.append(c)
                if len(got) == n:
                    return got
        if spacing < 1.0:
            return (got + [c for c in cells if c not in got] * n)[:n]
        spacing *= 0.8


def load_originals(path):
    orig = {}
    if path.exists():
        pat = re.compile(r"UPDATE .* WHERE a_index=(\d+) .*-- was (x=\S+ z=\S+ h=\S+ total=-?\d+)")
        for line in path.read_text().splitlines():
            m = pat.match(line)
            if m:
                orig[int(m[1])] = m[2]
    return orig


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "server-config" / "zone0-mobs-map.sql")
    ap.add_argument("--seed", type=int, default=SEED)
    ap.add_argument("--plot", type=pathlib.Path, help="also draw the result over --ref-image (needs Pillow)")
    ap.add_argument("--ref-image", type=pathlib.Path, help="the reference map (Juno.png, 814x845)")
    args = ap.parse_args()

    zone, rng = Zone(), random.Random(args.seed)
    orig = load_originals(args.out)
    rows = {}
    names, flags = {}, {}
    for idx, npc, x, z, h, total, regen, flag, usa, kor in sql(
            "SELECT r.a_index, r.a_npc_idx, r.a_pos_x, r.a_pos_z, r.a_pos_h, r.a_total_num, r.a_regen_sec, n.a_flag,"
            " n.a_name_usa, n.a_name FROM t_npc_regen r JOIN t_npc n ON n.a_index=r.a_npc_idx"
            " WHERE r.a_zone_num=0 ORDER BY r.a_index"):
        npc = int(npc)
        rows.setdefault(npc, []).append((int(idx), f"x={float(x):g} z={float(z):g} h={float(h):g} total={total}"))
        names[npc], flags[npc] = usa or kor, int(flag)

    def was(idx, cur):
        return orig.get(idx, cur)

    lines = ["-- Generated by v2009/scripts/juno-from-map.py; do not edit.",
             "-- Zone 0 (old Juno) monster spawns laid out after the classic Juno map (see the script).",
             "-- Idempotent; applied by v2009/scripts/setup-db.sh after zone0-oldjuno.sql.", "",
             "USE lc2009_data;", ""]
    placed = []  # (species label, world x, world z) for the plot
    summary = []
    taken = set()

    def emit(idx, ix, iz, total, cur, note):
        lines.append(f"UPDATE t_npc_regen SET a_pos_x={ix + 0.5:g}, a_pos_z={iz + 0.5:g}, "
                     f"a_pos_h={zone.height(ix, iz):.2f}, a_y_layer=0{total} WHERE a_index={idx} AND a_zone_num=0;"
                     f" -- was {was(idx, cur)}; {note}")

    for label, npcs in SPECIES.items():
        lines.append(f"-- {label}: npc {', '.join(f'{n} {names.get(n, "?")}' for n in npcs)}")
        species_rows = [(n, r) for n in npcs for r in rows.get(n, [])]
        regions = [zone.cells_in(b) for b in REGIONS[label]]
        shares = apportion(len(species_rows), [len(c) for c in regions])
        k = 0
        for ri, (cells, n) in enumerate(zip(regions, shares)):
            for ix, iz in scatter(rng, cells, n, taken):
                taken.add((ix, iz))
                npc, (idx, cur) = species_rows[k]
                k += 1
                emit(idx, ix, iz, ", a_total_num=-1", cur, f"npc {npc} {names[npc]}; region {ri + 1}")
                placed.append((label, ix + 0.5, iz + 0.5))
        summary.append((label, npcs, len(species_rows), shares, [len(c) for c in regions]))
        lines.append("")

    lines.append("-- gathering objects around the map's Herbs / Energy / Stones marks")
    for npc, spots in RESOURCES.items():
        rs = rows.get(npc, [])
        cells = [zone.cells_around(*to_world(*s), RESOURCE_RADIUS) for s in spots]
        shares = apportion(len(rs), [1] * len(spots))
        k = 0
        for ri, (c, n) in enumerate(zip(cells, shares)):
            for ix, iz in scatter(rng, c, n, taken):
                taken.add((ix, iz))
                idx, cur = rs[k]
                k += 1
                emit(idx, ix, iz, ", a_total_num=-1", cur, f"npc {npc} {names[npc]}; spot {ri + 1}")
                placed.append(("res", ix + 0.5, iz + 0.5))
    lines.append("")

    lines.append("-- field NPCs at their map icons")
    for npc, spot in NPC_SPOTS.items():
        wx, wz = to_world(*spot)
        ix, iz = zone.nearest_walkable(wx, wz)
        for idx, cur in rows.get(npc, []):
            emit(idx, ix, iz, "", cur, f"npc {npc} {names[npc]}; icon at {wx:.0f},{wz:.0f}")
            placed.append(("npc:" + names[npc], ix + 0.5, iz + 0.5))
    lines.append("")

    on_map = {n for ns in SPECIES.values() for n in ns} | set(RESOURCES) | set(NPC_SPOTS)
    disabled = []
    lines.append("-- non-peaceful NPCs that are not on the map: not spawned (a_total_num=0); originals in the comments")
    for npc in sorted(rows):
        if npc in on_map or flags[npc] & NPC_PEACEFUL:
            continue
        disabled.append((npc, names[npc], len(rows[npc])))
        for idx, cur in rows[npc]:
            lines.append(f"UPDATE t_npc_regen SET a_total_num=0 WHERE a_index={idx} AND a_zone_num=0;"
                         f" -- was {was(idx, cur)}; npc {npc} {names[npc]}")
    args.out.write_text("\n".join(lines) + "\n")

    for label, npcs, n, shares, areas in summary:
        print(f"{label:17} npc {','.join(map(str, npcs)):8} rows {n:3}  per region {shares}  cells {areas}")
    print("disabled:", ", ".join(f"{n} {name} ({c})" for n, name, c in disabled))
    print("wrote", args.out)
    if args.plot:
        plot(args.plot, args.ref_image, placed)


def plot(path, ref_image, placed):
    from PIL import Image, ImageDraw, ImageFont  # optional dependency
    a, b, c, d, e, f = REF_TO_WORLD
    det = a * e - b * d

    def to_ref(wx, wz):
        x, z = wx - c, wz - f
        return (e * x - b * z) / det, (-d * x + a * z) / det

    im = Image.open(ref_image).convert("RGB")
    im = Image.blend(im, Image.new("RGB", im.size, (0, 0, 0)), 0.35)
    scale = 2
    im = im.resize((im.width * scale, im.height * scale), Image.LANCZOS)
    dr = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
        small = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 11)
    except OSError:
        font = small = ImageFont.load_default()
    palette = [(255, 60, 60), (60, 220, 60), (70, 140, 255), (255, 220, 0), (255, 70, 255), (0, 230, 230),
               (255, 150, 40), (255, 255, 255), (170, 110, 255), (150, 255, 150), (255, 160, 160), (120, 200, 255),
               (200, 200, 90), (255, 120, 190), (90, 255, 210), (230, 170, 120), (180, 180, 180)]
    letters = "ABCDEFGHIJKLMNOPQ"
    keys = list(SPECIES)
    for label, wx, wz in placed:
        x, y = to_ref(wx, wz)
        x, y = x * scale, y * scale
        if label in SPECIES:
            i = keys.index(label)
            dr.ellipse((x - 7, y - 7, x + 7, y + 7), fill=(0, 0, 0), outline=palette[i], width=2)
            dr.text((x, y), letters[i], fill=palette[i], font=small, anchor="mm")
        elif label == "res":
            dr.rectangle((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255), outline=(0, 0, 0))
        else:
            dr.polygon([(x, y - 10), (x + 9, y + 7), (x - 9, y + 7)], fill=(255, 255, 0), outline=(0, 0, 0))
            dr.text((x + 11, y), label[4:], fill=(255, 255, 0), font=small, anchor="lm", stroke_width=2,
                    stroke_fill=(0, 0, 0))
    legend = Image.new("RGB", (260, im.height), (20, 20, 20))
    ld = ImageDraw.Draw(legend)
    ld.text((12, 12), "zone 0 spawns", fill=(255, 255, 255), font=font)
    for i, label in enumerate(keys):
        y = 46 + i * 26
        ld.ellipse((12, y, 30, y + 18), fill=(0, 0, 0), outline=palette[i], width=2)
        ld.text((21, y + 9), letters[i], fill=palette[i], font=small, anchor="mm")
        n = sum(1 for p in placed if p[0] == label)
        ld.text((40, y + 1), f"{label} ({n})", fill=palette[i], font=font)
    y = 46 + len(keys) * 26 + 10
    ld.rectangle((16, y + 5, 24, y + 13), fill=(255, 255, 255))
    ld.text((40, y + 1), "herb / energy / ore", fill=(255, 255, 255), font=font)
    ld.polygon([(21, y + 30), (29, y + 44), (13, y + 44)], fill=(255, 255, 0))
    ld.text((40, y + 30), "field NPC", fill=(255, 255, 0), font=font)
    out = Image.new("RGB", (im.width + legend.width, im.height))
    out.paste(im, (0, 0))
    out.paste(legend, (im.width, 0))
    out.save(path)
    print("plot", path)


if __name__ == "__main__":
    main()
