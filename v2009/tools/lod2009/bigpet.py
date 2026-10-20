"""bigpet_brz.lod (2009 CWildPetData::LoadWildPetDataFromFile, WILD_PET_ADD).

2009 record: index, name (char[256]), type, smc file (char[256]),
9 animation names (char[50]): idle1, idle2, attack1, attack2, damage, die,
walk, run, hungry. Source: server DB t_attack_pet (form 1 columns); name =
BR name of the pet's item (strItem_br) since the DB/BR pet names are Korean.
"""
import db
import items
from common import Writer, Report, out, pick

NAME_LEN, FILE_LEN, ANI_LEN = 256, 256, 50
ANIS = ("idle1", "idle2", "attack1", "attack2", "damage", "die", "walk", "run", "levelup")


def source():
    """(table, column suffix): the 2009 server reads t_bigpet (APetProto.cpp); the 2016
    dump's t_attack_pet has the same rows plus the 2015-only pets, with *_1/*_2 forms."""
    if db.DB_NAME == "lc2009_data":
        return "t_bigpet", ""
    return "t_attack_pet", "_1"


def load_rows():
    table, _ = source()
    return db.query(table, "select * from %s order by a_index" % table)


def build():
    table, sfx = source()
    rep = Report("bigpet_brz.lod", "DB %s + BR item names" % table)
    rows_ = load_rows()
    disabled = [db.I(r["a_index"]) for r in rows_ if db.I(r["a_enable"]) == 0]
    if disabled:
        rep.note("disabled rows kept (server never creates them): %s" % disabled)
    rows = rows_
    tx = items.texts()
    w = Writer()
    w.i32(len(rows))
    rep.header = len(rows)
    for r in rows:
        idx = db.I(r["a_index"])
        it = tx.get(db.I(r["a_item_idx"]))
        name, src = pick(it[0] if it and it[2] >= 0 else b"", "Pet %d" % idx)
        rep.count("name from " + ["BR item name", "placeholder 'Pet <n>'"][src])
        w.i32(idx)
        w.lstr(name, NAME_LEN)
        w.i32(db.I(r["a_type"]))
        w.lstr(db.S(r["a_smcFileName" + sfx]), FILE_LEN)
        for a in ANIS:
            v = db.S(r["a_ani_%s%s" % (a, sfx)])
            if len(v) > ANI_LEN - 1:
                rep.count("animation name truncated to 49 bytes")
            w.lstr(v, ANI_LEN)
        if sfx and db.S(r["a_smcFileName_2"]) and db.S(r["a_smcFileName_2"]) != db.S(r["a_smcFileName_1"]):
            rep.count("second (mounted/transformed) form dropped")
        rep.records += 1
    rep.note("2009 slot 'hungry' is filled from a_ani_levelup (the column holds *_hungry anims).")
    return w.save(out("bigpet_brz.lod")), rep
