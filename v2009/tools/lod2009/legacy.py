"""Readers for the old BR `_brz` files (post-2009 variable-length formats).

Only used as text fallbacks and for cross-checks; the record sets come from
the server DB. Formats were reverse-engineered against the files themselves
(each reader consumes its file up to the -9999 footer / end exactly).
"""
from common import Reader, br, FOOTER

ITEM_FLAG_ORIGIN = 1 << 19


def read_items_brz(path=None):
    """itemALL_brz.lod: 2009 layout with job as a plain int, plus a tail:
    attackEffect str, damageEffect str, 5 ints (set/restriction),
    rare (index, prob), [ORIGIN flag: 10 x (optType, optLevel)],
    N + N x 4 ints. Ends with -9999 and a 4-byte checksum."""
    r = Reader.open(path or br("itemALL_brz.lod"))
    last = r.i32()
    recs = {}
    while r.left() >= 4:
        idx = r.i32()
        if idx == FOOTER:
            break
        d = {"index": idx, "name": r.lstr()}
        v = r.ints(37)
        d.update(job=v[0], weight=v[1], fame=v[2], level=v[3], flag=v[4], wearing=v[5],
                 type=v[6], subType=v[7], need=v[8:28], ssk=v[28:32], num=v[32:36], price=v[36])
        d["smc"] = r.lstr()
        d["tex"] = r.ints(3)
        d["descr"] = r.lstr()
        d["armorEffect"] = r.lstr()
        d["attackEffect"] = r.lstr()
        d["damageEffect"] = r.lstr()
        d["tail"] = r.ints(7 + (20 if v[4] & ITEM_FLAG_ORIGIN else 0))
        n = r.i32()
        d["list"] = r.ints(4 * n)
        recs[idx] = d
    return last, recs


def read_skills_brz(path=None):
    """skills_brz.bin: 2009 layout + useSoulCount after useMagicLevel3,
    an after-effect string before the description, needGP after needMP and
    4 attribute chars at the end of each level."""
    r = Reader.open(path or br("skills_brz.bin"))
    last = r.i32()
    recs = {}
    while r.left() >= 4:
        idx = r.i32()
        if idx == FOOTER:
            break
        s = {"index": idx, "job": r.i32(), "job2": r.i32(), "petindex": r.i32(), "name": r.lstr()}
        s["type"] = r.i8()
        s["flag"] = r.i32()
        s["sorcerer"] = r.i32()
        s["maxLevel"] = r.i8()
        s["ranges"] = [r.f32(), r.f32(), r.f32()]
        s["targetType"] = r.i8()
        s["targetNum"] = r.i8()
        s["use"] = [r.i32(), r.i32(), r.i32(), r.i32(), r.i8(), r.i32(), r.i8(), r.i32(), r.i8()]
        s["soul"] = r.i32()
        s["appState"] = r.i32()
        s["times"] = r.ints(4)
        s["client"] = []
        for _w in range(2):
            strs = [r.lstr() for _ in range(7)]
            nums = [r.i8(), r.f32(), r.f32(), r.f32(), r.f32(), r.i8(), r.i8(),
                    r.f32(), r.f32(), r.f32(), r.f32(), r.f32()]
            s["client"].append((strs, nums))
        s["after"] = r.lstr()
        s["desc"] = r.lstr()
        s["tooltip"] = r.lstr()
        s["icon"] = r.ints(3)
        lv = []
        for _j in range(s["maxLevel"]):
            L = r.ints(11)
            for _k in range(3):
                L += [r.i32(), r.i8()]
            L += r.ints(6) + r.ints(4)
            for _k in range(6):
                L += [r.i32(), r.i8()]
            L.append(r.i32())
            L.append(r.raw(4))
            lv.append(L)
        s["levels"] = lv
        recs[idx] = s
    return last, recs


def read_npchelp_brz(path=None):
    """npchelp_brz.lod: npc list (zone, npc), mob list (zone, mob),
    then (npc, explanation) strings."""
    r = Reader.open(path or br("npchelp_brz.lod"))
    n = r.i32()
    npcs = [(r.i32(), r.i32()) for _ in range(n)]
    m = r.i32()
    mobs = [(r.i32(), r.i32()) for _ in range(m)]
    k = r.i32()
    expl = {}
    for _ in range(k):
        i = r.i32()
        expl[i] = r.lstr()
    assert r.eof()
    return npcs, mobs, expl


def read_option_brz(path=None):
    """option_brz.lod: 8-byte timestamp, last index, then until EOF
    (index, type, name, nLevels, nLevels x int)."""
    r = Reader.open(path or br("option_brz.lod"))
    r.raw(8)
    n = r.i32()
    recs = {}
    while not r.eof():
        idx = r.i32()
        typ = r.i32()
        name = r.lstr()
        nl = r.i32()
        recs[idx] = {"index": idx, "type": typ, "name": name, "values": r.ints(nl)}
    return n, recs, r.left()
