"""mobAll_brz.lod + MobName_brz.lod (2009 CMobData::LoadNPCDataFromFile with
DEL_AUCTION -> typeSE, and CMobName::LoadMobNameFromFile).

The shipped BR mobAll_brz.lod/mobname_brz.lod already have the 2009 layout but
stop at index 1523 (DB t_npc goes to 1653: 300 NPCs missing -> out-of-range
array access in the 2009 client) and end with the -9999 footer. They are
regenerated from DB t_npc, whose columns match the BR file field-for-field
(verified: smc/motions/effects 100%, speeds/flags/skills ~100%).
Names: strNpcName_br col 0 > DB a_name_brz > old mobname_brz > a_name_usa.
"""
import db
from common import Reader, Writer, Report, br, out, load_str, pick, s32, s8

NAME_LEN, SMC_LEN, MOTION_LEN, EFFECT_LEN = 50, 255, 50, 256
MOTIONS = ("a_motion_idle", "a_motion_walk", "a_motion_dam", "a_motion_attack", "a_motion_die",
           "a_motion_run", "a_motion_idle2", "a_motion_attack2")

SQL = ("select a_index, a_enable, a_name_brz, a_name_usa, a_level, a_walk_speed, a_run_speed, a_hp, a_mp, "
       "a_attackSpeed, a_attack_area, a_skillmaster, a_sskill_master, a_flag, a_flag1, a_scale, a_size, "
       "a_skill0, a_skill1, %s, a_file_smc, a_attackType, a_fireDelayCount, a_fireDelay0, a_fireDelay1, "
       "a_fireDelay2, a_fireDelay3, a_fireEffect0, a_fireEffect1, a_fireEffect2, a_fireObject, a_fireSpeed "
       "from t_npc order by a_index" % ", ".join(MOTIONS))


def read_old_names():
    r = Reader.open(br("mobname_brz.lod"))
    r.i32()
    names = {}
    while r.left() >= 4:
        i = r.i32()
        if i == -9999:
            break
        names[i] = r.lstr()
    return names


def skill_pair(v):
    t = (v or "").split()
    try:
        return int(t[0]), int(t[1])
    except (IndexError, ValueError):
        return -1, 0


def build():
    rows = db.query("t_npc", SQL)
    st = load_str("strNpcName", 2)
    old = read_old_names()
    I, F = db.I, db.F
    last = max(I(r["a_index"]) for r in rows)
    rep = Report("mobAll_brz.lod", "DB t_npc + strNpcName_br.lod")
    rn = Report("MobName_brz.lod", "DB t_npc + strNpcName_br.lod")
    w = Writer()
    wn = Writer()
    w.i32(last)
    wn.i32(last)
    rep.header = rn.header = last
    for r in rows:
        idx = I(r["a_index"])
        name, src = pick(st.get(idx, 0), r["a_name_brz"], old.get(idx), r["a_name_usa"])
        if src < 0:
            name = b"NPC %d" % idx
        rep.count("name from " + ["strNpcName_br", "DB a_name_brz", "old mobname_brz", "DB a_name_usa", "placeholder"][src])
        if len(name) > NAME_LEN - 1:
            rep.count("name truncated to 49 bytes")
        wn.i32(idx)
        wn.lstr(name, NAME_LEN)
        rn.records += 1

        w.i32(idx)
        w.lstr(name, NAME_LEN)
        w.i32(I(r["a_level"]))
        w.f32(F(r["a_walk_speed"]))       # LOADINT into float: raw float bytes
        w.f32(F(r["a_run_speed"]))
        w.ints([0] * 5)                    # effect[5]: always 0 in the BR file, no DB column
        w.i32(I(r["a_hp"]))
        w.i32(I(r["a_mp"]))
        w.i32(I(r["a_attackSpeed"]))
        w.f32(F(r["a_attack_area"]))
        w.i8(s8(I(r["a_skillmaster"], -1)))
        w.i8(s8(I(r["a_sskill_master"], -1)))
        w.i32(s32(I(r["a_flag"])))
        w.i32(s32(I(r["a_flag1"])))        # DEL_AUCTION typeSE
        w.f32(F(r["a_scale"], 1.0))
        w.f32(F(r["a_size"]))
        for c in ("a_skill0", "a_skill1"):
            si, sl = skill_pair(r[c])
            w.i32(si)
            w.i8(s8(sl))
        for c in MOTIONS:
            v = db.S(r[c])
            if len(v) > MOTION_LEN - 1:
                rep.count("motion name truncated to 49 bytes")
            w.lstr(v, MOTION_LEN)
        w.lstr(db.S(r["a_file_smc"]), SMC_LEN)
        w.i8(s8(I(r["a_attackType"])))
        fdc = I(r["a_fireDelayCount"])
        if not 0 <= fdc <= 4:
            rep.count("fireDelayCount clamped to 0..4")
            fdc = max(0, min(4, fdc))
        w.i8(fdc)
        for k in range(4):
            w.f32(F(r["a_fireDelay%d" % k]))
        for k in range(3):
            w.lstr(db.S(r["a_fireEffect%d" % k]), EFFECT_LEN)
        w.i8(s8(I(r["a_fireObject"])))
        w.f32(F(r["a_fireSpeed"]))
        rep.records += 1
    rep.note("effect[5] written as 0 (as in the BR file); 2016 columns with no 2009 slot "
             "(skill2/3, AI, attributes, rvr, extra_flag ...) dropped.")
    return [(w.save(out("mobAll_brz.lod")), rep), (wn.save(out("MobName_brz.lod")), rn)]
