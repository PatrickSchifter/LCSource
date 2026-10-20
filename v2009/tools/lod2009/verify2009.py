#!/usr/bin/env python3
"""Verify data files against the exact 2009 client read sequences.

Each loader below is a line-by-line port of the 2009 C++ (Engine/Entities/*.cpp,
World/WorldIO.cpp), including the local #defines that are active in that tree:
  LOAD_ITEMARMOR_EFFECT (ItemData.cpp), DEL_PET_AI off; DEL_AUCTION (MobData.cpp);
  NEW_SHOP + ITEM_ENABLE (CashShopData.cpp); USA block of LocalDefine.h:
  NEW_GUILD_SYSTEM (skill learnGP), HELP_SYSTEM_1, MONSTER_COMBO, WILD_PET_ADD,
  RARE_ITEM; PKPENALTY_QUEST / NEW_QUESTBOOK off.
fread() semantics are emulated: a short read returns 0 and leaves the file at
EOF. The loaders only stop on a failed *index* read (FILE* loaders) or on the
loop bound; CTFileStream loaders (quest, action) throw on EOF.

A file is EXACT when: every read inside a record is complete, every string fits
its char[] (with room for the terminating NUL the zeroed struct provides),
every array index is in bounds (CStaticArray::New(n) allocates n+1), and the
loader stops exactly at the last byte.

usage: verify2009.py [--dir DIR] [--no-spot] [files...]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class CFile:
    """FILE* opened "rb"."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.p = 0
        self.name = os.path.basename(path)
        self.errors = []
        self.warnings = []

    def fread(self, n):
        """fread(&x, n, 1, fp): returns bytes, or None (=0 items) on short read."""
        if n < 0:
            self.errors.append("negative fread size %d at %d" % (n, self.p))
            return None
        if self.p + n > len(self.d):
            self.p = len(self.d)
            return None
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def err(self, msg):
        if len(self.errors) < 20:
            self.errors.append(msg)

    def warn(self, msg):
        if len(self.warnings) < 20:
            self.warnings.append(msg)

    # LOADINT/LOADCHAR/LOADFLOAT inside a record: a short read is an error
    def INT(self, what="int"):
        b = self.fread(4)
        if b is None:
            self.err("short read (%s) at EOF" % what)
            return 0
        return struct.unpack("<i", b)[0]

    def CHAR(self, what="char"):
        b = self.fread(1)
        if b is None:
            self.err("short read (%s) at EOF" % what)
            return 0
        return struct.unpack("<b", b)[0]

    def FLOAT(self, what="float"):
        b = self.fread(4)
        if b is None:
            self.err("short read (%s) at EOF" % what)
            return 0.0
        return struct.unpack("<f", b)[0]

    def STR(self, bufsize, what="str"):
        """LOADSTR(d): { int iLen; LOADINT(iLen); fread(&d, iLen, 1, fp); }"""
        at = self.p
        n = self.INT(what + ".len")
        if n < 0 or n > bufsize:
            self.err("%s: length %d overflows char[%d] at %d" % (what, n, bufsize, at))
            n = max(0, n)
        elif n == bufsize:
            self.warn("%s: length %d fills char[%d] (no NUL) at %d" % (what, n, bufsize, at))
        b = self.fread(n)
        if b is None:
            self.err("short read (%s, %d bytes) at EOF" % (what, n))
            return b""
        return b

    def left(self):
        return len(self.d) - self.p


class StreamEOF(Exception):
    pass


class CTFileStream(CFile):
    """CTFileStream: Read_t throws at EOF."""

    def fread(self, n):
        b = CFile.fread(self, n)
        if b is None:
            raise StreamEOF("read %d bytes past EOF" % n)
        return b


def index_ok(f, idx, header, what="index"):
    # CStaticArray::New(n) allocates n+1 elements; operator[] asserts (i-1) <= count
    if idx < 0 or idx > header:
        f.err("%s %d outside array [0..%d] at %d" % (what, idx, header, f.p))
        return False
    return True


def finish(f, res):
    res["left"] = f.left()
    if f.left():
        f.err("%d byte(s) left after the loader stopped (%s...)" % (f.left(), f.d[f.p:f.p + 8].hex()))
    res["errors"] = f.errors
    res["warnings"] = f.warnings
    res["exact"] = not f.errors
    return res


def fileloop(f, header):
    """for(i=0;i<header;++i){ LOADINT(iIndex); if(iReadBytes<=0) break; ... }"""
    for _ in range(header):
        b = f.fread(4)
        if b is None:
            return
        yield struct.unpack("<i", b)[0]


# --------------------------------------------------------------------------
def load_itemall(path):
    f = CFile(path)
    hdr = f.INT("header")
    recs = {}
    for idx in fileloop(f, hdr):
        index_ok(f, idx, hdr)
        it = {"index": idx}
        it["name"] = f.STR(50, "name")
        n = f.INT("job.len")                       # { LOADINT(iLen); fread(&job, iLen) }
        if n != 4:
            f.err("item %d: job length %d (overflows int job)" % (idx, n))
        b = f.fread(max(n, 0))
        it["job"] = struct.unpack("<i", b[:4])[0] if b and len(b) >= 4 else 0
        for k in ("weight", "fame", "level", "flag", "wearing", "type", "subType"):
            it[k] = f.INT(k)
        it["need"] = [(f.INT(), f.INT()) for _ in range(10)]
        it["sskill"] = [f.INT() for _ in range(4)]
        it["num"] = [f.INT() for _ in range(4)]
        it["price"] = f.INT("price")
        it["smc"] = f.STR(255, "fileSMC")
        it["icon"] = [f.INT(), f.INT(), f.INT()]
        it["descr"] = f.STR(255, "descr")
        it["armorEffect"] = f.STR(255, "ArmorEffectName")   # LOAD_ITEMARMOR_EFFECT
        recs[idx] = it
    return finish(f, {"header": hdr, "records": len(recs), "data": recs})


def load_itemname(path):
    f = CFile(path)
    hdr = f.INT("header")
    recs = {}
    for idx in fileloop(f, hdr):
        index_ok(f, idx, hdr)
        recs[idx] = {"name": f.STR(50, "name"), "descr": f.STR(255, "descr")}
    return finish(f, {"header": hdr, "records": len(recs), "data": recs})


def load_skills(path):
    f = CFile(path)
    hdr = f.INT("header")
    recs = {}
    for idx in fileloop(f, hdr):
        index_ok(f, idx, hdr)
        s = {"index": idx, "job": f.INT(), "job2": f.INT(), "petindex": f.INT()}
        s["name"] = f.STR(51, "name")
        s["type"] = f.CHAR()
        s["flag"] = f.INT()
        s["sorcerer"] = f.INT()
        s["maxLevel"] = f.CHAR()
        s["range"] = [f.FLOAT(), f.FLOAT(), f.FLOAT()]
        s["targetType"] = f.CHAR()
        s["targetNum"] = f.CHAR()
        s["use"] = [f.INT(), f.INT(), f.INT(), f.INT(), f.CHAR(), f.INT(), f.CHAR(), f.INT(), f.CHAR()]
        s["appState"] = f.INT()
        s["times"] = [f.INT(), f.INT(), f.INT(), f.INT()]
        s["client"] = []
        for _w in range(2):                         # WEAPON_COUNT
            c = {"strs": [f.STR(256, "client.str") for _ in range(7)]}
            c["fireobjType"] = f.CHAR()
            c["obj"] = [f.FLOAT() for _ in range(4)]
            c["coord"] = f.CHAR()
            c["delayCount"] = f.CHAR()
            c["delay"] = [f.FLOAT() for _ in range(4)]
            if not 0 <= c["delayCount"] <= 4:
                f.err("skill %d: fireobjDelayCount %d > 4 (fireobjDelay[4] overrun)" % (idx, c["delayCount"]))
            c["destDelay"] = f.FLOAT()
            s["client"].append(c)
        s["desc"] = f.STR(256, "client_description")
        s["tooltip"] = f.STR(256, "client_tooltip")
        s["icon"] = [f.INT(), f.INT(), f.INT()]
        s["levels"] = []
        for _j in range(s["maxLevel"]):
            L = [f.INT() for _ in range(10)]
            for _k in range(3):
                L += [f.INT(), f.CHAR()]
            for _k in range(3):
                L += [f.INT(), f.INT()]
            L += [f.INT() for _ in range(4)]
            for _k in range(6):
                L += [f.INT(), f.CHAR()]
            L.append(f.INT("learnGP"))              # NEW_GUILD_SYSTEM
            s["levels"].append(L)
        recs[idx] = s
    return finish(f, {"header": hdr, "records": len(recs), "data": recs})


def load_option(path):
    f = CFile(path)
    hdr = f.INT("header")
    recs = {}
    for idx in fileloop(f, hdr):
        typ = f.INT("type")
        index_ok(f, typ, hdr, "option type")
        name = f.STR(50, "name")
        b = f.fread(4 * 7)                          # fread(&value, sizeof(int), OPTION_MAX_LEVEL)
        if b is None:
            f.err("option %d: value[7] short read (client aborts the load)" % idx)
            break
        recs[typ] = {"index": idx, "type": typ, "name": name, "values": list(struct.unpack("<7i", b))}
    return finish(f, {"header": hdr, "records": len(recs), "data": recs})


def load_catalog(path):
    f = CFile(path)
    cnt = f.INT("count")
    recs = {}
    for idx in fileloop(f, cnt):
        typ = f.INT("type")
        if not 0 <= typ <= 10:                      # apShopData.New(MAX_CASHSHOP_CLASS=10)
            f.err("catalog %d: type %d outside apShopData[0..10]" % (idx, typ))
        d = {"index": idx, "type": typ, "cash": f.INT(), "mileage": f.INT(),
             "tex": [f.INT(), f.INT(), f.INT()]}
        d["flag"] = f.INT("flag")                   # NEW_SHOP
        d["enable"] = f.CHAR("enable")              # ITEM_ENABLE
        d["name"] = f.STR(1024, "name")
        d["desc"] = f.STR(2048, "desc")
        n = f.INT("itemListCnt")
        if n < 0:
            f.err("catalog %d: negative item count" % idx)
            n = 0
        d["items"] = [[f.INT() for _ in range(5)] for _ in range(n)]
        recs[idx] = d
    return finish(f, {"header": cnt, "records": len(recs), "data": recs})


def load_npchelp(path):
    f = CFile(path)
    cnt = f.INT("count")
    recs = []
    for _i in range(cnt):                           # no EOF check in the 2009 loop
        recs.append({"zone": f.INT(), "npc": f.INT(), "name": f.STR(50, "npc_name"),
                     "explan": f.STR(1024, "npc_explan")})
    return finish(f, {"header": cnt, "records": len(recs), "data": recs})


def load_bigpet(path):
    f = CFile(path)
    cnt = f.INT("count")
    recs = []
    for _i in range(cnt):
        d = {"index": f.INT(), "name": f.STR(256, "strName"), "type": f.INT(),
             "file": f.STR(256, "strFileName"), "anis": [f.STR(50, "PetAni") for _ in range(9)]}
        recs.append(d)
    return finish(f, {"header": cnt, "records": len(recs), "data": recs})


def load_quest(path):
    f = CTFileStream(path)
    recs = {}
    try:
        last = f.INT("lastIndex")
        i = 0
        while i <= last:
            index = f.INT("index")
            if index < i:
                f.err("quest index %d not ascending (loop at %d)" % (index, i))
            i = index
            if not 0 <= i <= last:
                f.err("quest index %d outside m_aQuestStaticData[0..%d]" % (i, last))
            q = {"index": index}
            n = f.INT("name.len")
            if n > 50:
                f.err("quest %d: name length %d overflows char[50]" % (index, n))
            elif n == 50:
                f.warn("quest %d: name fills char[50]" % index)
            q["name"] = f.fread(n)
            q["head"] = [f.INT() for _ in range(12)]   # type1..needJob (no PKPENALTY_QUEST)
            q["need"] = [(f.INT(), f.INT()) for _ in range(5)]
            q["cond"] = [[f.INT() for _ in range(3 + 4)] for _ in range(3)]
            q["prize"] = [[f.INT() for _ in range(3)] for _ in range(5)]
            q["optPrize"] = f.INT()
            q["opt"] = [[f.INT() for _ in range(4)] for _ in range(5)]
            q["desc"] = []
            for k in range(3):
                n = f.INT("desc.len")
                if n > 2048:
                    f.err("quest %d: desc%d length %d overflows char[2048]" % (index, k + 1, n))
                q["desc"].append(f.fread(n))
            recs[index] = q                         # NEW_QUESTBOOK off: no party scale
            i += 1
    except StreamEOF as e:
        f.err("CTFileStream exception: %s (loader throws)" % e)
        last = recs and max(recs) or 0
    return finish(f, {"header": last, "records": len(recs), "data": recs})


def load_mobname(path):
    f = CFile(path)
    hdr = f.INT("header")
    recs = {}
    for idx in fileloop(f, hdr):
        index_ok(f, idx, hdr)
        recs[idx] = {"name": f.STR(50, "name")}
    return finish(f, {"header": hdr, "records": len(recs), "data": recs})


def load_moball(path):
    f = CFile(path)
    hdr = f.INT("header")
    recs = {}
    for idx in fileloop(f, hdr):
        index_ok(f, idx, hdr)
        m = {"index": idx, "name": f.STR(50, "name"), "level": f.INT()}
        m["walk"] = f.FLOAT()                       # LOADINT into float members
        m["run"] = f.FLOAT()
        m["effect"] = [f.INT() for _ in range(5)]
        m["hp"] = f.INT()
        m["mp"] = f.INT()
        m["attackSpeed"] = f.INT()
        m["attackDistance"] = f.FLOAT()
        m["skill"] = f.CHAR()
        m["sskillmaster"] = f.CHAR()
        m["type"] = f.INT()
        m["typeSE"] = f.INT()                       # DEL_AUCTION
        m["scale"] = f.FLOAT()
        m["size"] = f.FLOAT()
        m["skill0"] = (f.INT(), f.CHAR())
        m["skill1"] = (f.INT(), f.CHAR())
        m["motions"] = [f.STR(50, "motion") for _ in range(8)]
        m["smc"] = f.STR(255, "fileSMC")
        m["attackType"] = f.CHAR()
        m["fireDelayCount"] = f.CHAR()
        if not 0 <= m["fireDelayCount"] <= 4:
            f.err("mob %d: fireDelayCount %d > 4" % (idx, m["fireDelayCount"]))
        m["delay"] = [f.FLOAT() for _ in range(4)]
        m["fireEffect"] = [f.STR(256, "fireEffect") for _ in range(3)]
        m["fireObject"] = f.CHAR()
        m["fireSpeed"] = f.FLOAT()
        recs[idx] = m
    return finish(f, {"header": hdr, "records": len(recs), "data": recs})


def load_actions(path):
    f = CTFileStream(path)
    recs = {}
    try:
        cnt = f.INT("count")
        for _i in range(cnt):
            idx = f.INT("index")
            index_ok(f, idx, cnt)
            a = {"index": idx, "type": f.CHAR("type")}
            for k, size in (("name", 32), ("desc", 256)):
                n = f.INT(k + ".len")
                if n >= size:                       # Action_strX[nLength] = NULL
                    f.err("action %d: %s length %d >= char[%d]" % (idx, k, n, size))
                a[k] = f.fread(n) if n > 0 else b""
            a["anis"] = []
            for _j in range(6):                     # TOTAL_JOB
                n = f.INT("anim.len")
                if n >= 32:
                    f.err("action %d: anim length %d >= char[32]" % (idx, n))
                a["anis"].append(f.fread(n) if n > 0 else b"")
            a["job"] = f.INT()
            a["tex"] = [f.INT(), f.INT(), f.INT()]
            recs[idx] = a
    except StreamEOF as e:
        f.err("CTFileStream exception: %s (loader throws)" % e)
        cnt = 0
    return finish(f, {"header": cnt, "records": len(recs), "data": recs})


LOADERS = {
    "ItemAll_brz.lod": load_itemall,
    "ItemName_brz.lod": load_itemname,
    "Skills_brz.bin": load_skills,
    "Option_brz.lod": load_option,
    "catalog.lod": load_catalog,
    "npchelp_brz.lod": load_npchelp,
    "bigpet_brz.lod": load_bigpet,
    "questAll_brz.lod": load_quest,
    "mobAll_brz.lod": load_moball,
    "MobName_brz.lod": load_mobname,
    "Actions_brz.bin": load_actions,
}


def loader_for(path):
    base = os.path.basename(path).lower()
    for k, fn in LOADERS.items():
        if base == k.lower() or base.startswith(os.path.splitext(k)[0].lower()):
            return fn
    raise KeyError(path)


# --------------------------------------------------------------------------
# Spot checks against the sources (DB cache + Local/br strings)
def spot_checks(name, res):
    import db
    from common import load_str, to_bytes, strip_prefix
    out = []
    D = res["data"]

    def chk(label, got, want):
        ok = got == want
        out.append(("ok  " if ok else "FAIL") + " %s: %r%s" % (label, got, "" if ok else " (want %r)" % (want,)))

    def row(table, key, idx):
        for r in db._MEMO.get(table, []):
            if db.I(r[key]) == idx:
                return r
        return None

    if name == "ItemAll_brz.lod":
        st = load_str("strItem", 2)
        chk("item 1 present (absent from DB t_item: no record expected)", 1 in D, False)
        for idx in (2, 19, 43, 45, 723, 3770, 11303):
            it = D.get(idx)
            if not it:
                out.append("FAIL item %d missing" % idx)
                continue
            r = row("t_item", "a_index", idx)
            want = st.get(idx, 0) or to_bytes(r["a_name_brz"]) or to_bytes(r["a_name_usa"])
            chk("item %d name" % idx, it["name"], want[:49] if want else it["name"])
            chk("item %d type/sub/num0/price" % idx, (it["type"], it["subType"], it["num"][0], it["price"]),
                (db.I(r["a_type_idx"]), db.I(r["a_subtype_idx"]), db.I(r["a_num_0"]), db.I(r["a_price"])))
            chk("item %d smc/icon" % idx, (it["smc"], it["icon"]),
                (to_bytes(r["a_file_smc"]), [db.I(r["a_texture_id"]), db.I(r["a_texture_row"]), db.I(r["a_texture_col"])]))
        if db.DB_NAME == "lc2009_data":
            bad = sorted(i for i, it in D.items() if it["job"] & ~0xFF)
            chk("item job flags use only 2009 bits 0-5 classes, 6 pet, 7 attack pet", bad[:10], [])
            ws = sorted(i for i, it in D.items() if it["wearing"] >= 11)
            en = {db.I(r["a_index"]) for r in db._MEMO["t_item"] if db.I(r["a_enable"]) == 1}
            chk("no enabled item wears slot >= 11 (2009 MAX_WEARING 11)", [i for i in ws if i in en][:10], [])
    elif name == "Skills_brz.bin":
        st = load_str("strSkill", 3)
        for idx in (1, 2, 36, 1854):
            s = D.get(idx)
            if not s:
                out.append("FAIL skill %d missing" % idx)
                continue
            r = row("t_skill", "a_index", idx)
            chk("skill %d name" % idx, s["name"], (st.get(idx, 0) or to_bytes(r["a_name_brz"]))[:50])
            chk("skill %d maxLevel/levels" % idx, (s["maxLevel"], len(s["levels"])), (db.I(r["a_maxLevel"]),) * 2)
            chk("skill %d fireAni w0/w1" % idx, (s["client"][0]["strs"][3], s["client"][1]["strs"][3]),
                (to_bytes(r["a_cd_fa"]), to_bytes(r["a_cd_fa2"])))
            chk("skill %d desc" % idx, s["desc"], strip_prefix(st.get(idx, 1))[:255] or s["desc"])
        if db.DB_NAME == "lc2009_data":
            import collections
            jobs = collections.Counter(s["job"] for s in D.values())
            SKJOB = {db.I(r["a_index"]): db.I(r["a_job"]) for r in db._MEMO["t_skill"]}
            out.append("info skill jobs: %s" % dict(sorted(jobs.items())))
            chk("no skill with 2016-only job 8/10/11", sorted(j for j in jobs if j in (8, 10, 11)), [])
            chk("skill jobs == DB a_job", [i for i, s in D.items()
                                         if s["job"] != SKJOB.get(i)][:10], [])
        lv = [L for L in db._MEMO["t_skilllevel"] if db.I(L["a_index"]) == 1]
        chk("skill 1 lv1 needMP/learnLevel", (D[1]["levels"][0][1], D[1]["levels"][0][8]),
            (db.I(lv[0]["a_needMP"]), db.I(lv[0]["a_learnLevel"])))
    elif name == "Option_brz.lod":
        st = load_str("strOption", 1)
        for r in db._MEMO["t_option"][:3] + db._MEMO["t_option"][-1:]:
            t = db.I(r["a_type"])
            o = D.get(t)
            chk("option type %d name" % t, o and o["name"], st.get(db.I(r["a_index"])) or o and o["name"])
            chk("option type %d values" % t, o and o["values"], ([int(v) for v in r["a_level"].split()] + [0] * 7)[:7])
    elif name == "catalog.lod":
        cti = db._MEMO["t_ct_item"]
        for c in db._MEMO["t_catalog"][:2] + db._MEMO["t_catalog"][-1:]:
            idx = db.I(c["a_ctid"])
            d = D.get(idx)
            want = [[db.I(x["a_item_idx"]), db.I(x["a_item_flag"]), db.I(x["a_item_plus"]), db.I(x["a_item_option"]),
                     db.I(x["a_item_num"])] for x in cti if db.I(x["a_ctid"]) == idx]
            chk("catalog %d cash/items" % idx, d and (d["cash"], d["items"]), (db.I(c["a_cash"]), want))
            out.append("info catalog %d: tab %d name %r" % (idx, d["type"], d["name"][:40]))
        chk("catalog ids == DB t_catalog", sorted(D), [db.I(c["a_ctid"]) for c in db._MEMO["t_catalog"]])
    elif name == "npchelp_brz.lod":
        st = load_str("strNpcName", 2)
        for d in D[:2] + D[-1:]:
            chk("npchelp npc %d name" % d["npc"], d["name"], st.get(d["npc"], 0)[:49] or d["name"])
    elif name == "bigpet_brz.lod":
        import bigpet
        rows = bigpet.load_rows()
        sfx = bigpet.source()[1]
        chk("bigpet indices == DB rows", [d["index"] for d in D], [db.I(r["a_index"]) for r in rows])
        for d, r in zip(D[:2] + D[-1:], rows[:2] + rows[-1:]):
            chk("bigpet %d smc/idle/hungry" % d["index"], (d["file"], d["anis"][0], d["anis"][8]),
                (to_bytes(r["a_smcFileName" + sfx]), to_bytes(r["a_ani_idle1" + sfx]), to_bytes(r["a_ani_levelup" + sfx])))
    elif name == "questAll_brz.lod":
        from common import pick
        st = load_str("strQuest", 4)
        enabled = [db.I(r["a_index"]) for r in db._MEMO["t_quest"] if db.I(r["a_enable"]) == 1]
        chk("quest records == enabled DB quests", sorted(D), enabled)
        if db.DB_NAME == "lc2009_data":
            chk("no quest needs a 2016-only job (6-8)", [i for i, q in D.items() if 6 <= q["head"][11] <= 8], [])
        for idx in enabled[:2] + enabled[-1:]:
            q = D.get(idx)
            r = row("t_quest", "a_index", idx)
            want = pick(st.get(idx, 0), r["a_name_brz"], r["a_name_usa"])[0][:49]
            chk("quest %d name" % idx, q and q["name"], want)
            chk("quest %d startType/startData/prizeNpc" % idx, q and q["head"][2:5],
                [db.I(r["a_start_type"]), db.I(r["a_start_data"]), db.I(r["a_prize_npc"])])
    elif name in ("mobAll_brz.lod", "MobName_brz.lod"):
        st = load_str("strNpcName", 2)
        for idx in (1, 23, 1523, 1653):
            m = D.get(idx)
            if m is None:
                out.append("info npc %d not in file" % idx)
                continue
            chk("npc %d name" % idx, m["name"], st.get(idx, 0)[:49] or m["name"])
            if name == "mobAll_brz.lod":
                r = row("t_npc", "a_index", idx)
                chk("npc %d level/hp/smc" % idx, (m["level"], m["hp"], m["smc"]),
                    (db.I(r["a_level"]), db.I(r["a_hp"]), to_bytes(r["a_file_smc"])))
    elif name == "Actions_brz.bin":
        st = load_str("strAction", 2)
        for idx in (1, 53):
            a = D.get(idx)
            chk("action %d name" % idx, a and a["name"], st.get(idx, 0)[:31])
    elif name == "ItemName_brz.lod":
        st = load_str("strItem", 2)
        for idx in (2, 43, 3770):
            chk("itemname %d" % idx, D.get(idx, {}).get("name"), st.get(idx, 0)[:49])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=None)
    ap.add_argument("--no-spot", action="store_true")
    ap.add_argument("--offline", action="store_true")
    ap.add_argument("--db", default=None)
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    from common import OUT_DATA
    import db
    db.OFFLINE = a.offline
    if a.db:
        db.set_db(a.db)
    d = a.dir or OUT_DATA
    files = a.files or [os.path.join(d, k) for k in LOADERS]
    allok = True
    if not a.no_spot:
        # warm the DB cache used by the spot checks
        import items, skills, catalog, bigpet, mobs  # noqa: F401
        db.query("t_item", items.SQL)
        db.query("t_skill", skills.SKILL_SQL)
        db.query("t_skilllevel", skills.LEVEL_SQL)
        db.query("t_option", "select a_index, a_type, a_name_brz, a_name_usa, a_level from t_option order by a_index")
        db.query("t_catalog", "select a_ctid, a_category, a_type, a_subtype, a_cash, a_mileage, a_flag, a_enable, "
                              "a_texture_id, a_texture_row, a_texture_col from t_catalog order by a_ctid")
        db.query("t_ct_item", "select a_index, a_ctid, a_item_idx, a_item_flag, a_item_plus, a_item_option, a_item_num "
                              "from t_ct_item order by a_ctid, a_index")
        bigpet.load_rows()
        db.query("t_quest", "select * from t_quest order by a_index")
        db.query("t_npc", mobs.SQL)
    for p in files:
        if not os.path.exists(p):
            print("MISSING  %s" % p)
            allok = False
            continue
        fn = loader_for(p)
        res = fn(p)
        status = "EXACT" if res["exact"] else "NOT EXACT"
        allok &= res["exact"]
        print("%-9s %-22s header=%-6s records=%-6d size=%d left=%d"
              % (status, os.path.basename(p), res["header"], res["records"], os.path.getsize(p), res["left"]))
        for e in res["errors"]:
            print("    error:", e)
        for w in res["warnings"]:
            print("    warn: ", w)
        if not a.no_spot:
            key = [k for k in LOADERS if LOADERS[k] is fn][0]
            for line in spot_checks(key, res):
                print("    " + line)
                if line.startswith("FAIL"):
                    allok = False
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
