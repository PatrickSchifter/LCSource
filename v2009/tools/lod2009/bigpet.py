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


def build():
    rep = Report("bigpet_brz.lod", "DB t_attack_pet + BR item names")
    rows = db.query("t_attack_pet", "select * from t_attack_pet order by a_index")
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
        w.lstr(db.S(r["a_smcFileName_1"]), FILE_LEN)
        for a in ANIS:
            v = db.S(r["a_ani_%s_1" % a])
            if len(v) > ANI_LEN - 1:
                rep.count("animation name truncated to 49 bytes")
            w.lstr(v, ANI_LEN)
        if db.S(r["a_smcFileName_2"]) and db.S(r["a_smcFileName_2"]) != db.S(r["a_smcFileName_1"]):
            rep.count("second (mounted/transformed) form dropped")
        rep.records += 1
    rep.note("2009 slot 'hungry' is filled from a_ani_levelup_1 (the column holds *_hungry anims).")
    return w.save(out("bigpet_brz.lod")), rep
