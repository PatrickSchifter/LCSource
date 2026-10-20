"""Skills_brz.bin (2009 CSkill::LoadSkillDataFromFile, NEW_GUILD_SYSTEM on -> learnGP).

Record set, numbers and client animation/effect data: server DB t_skill +
t_skilllevel (the BR skills files were generated from the same columns).
Text: strSkill_br.lod (name, description, tooltip) > DB a_*_brz >
old skills_brz.bin > DB a_*_usa.
"""
import db
from common import Writer, Report, load_str, out, pick, strip_prefix, s8
import legacy

NAME_LEN = 51
CLIENT_STR_LEN = 256
DESC_LEN = 256
WEAPON_COUNT = 2

LEVEL_COLS = ("a_needHP a_needMP a_durtime a_dummypower a_needItemIndex1 a_needItemCount1 "
              "a_needItemIndex2 a_needItemCount2 a_learnLevel a_learnSP").split()

# (string columns, numeric columns) per weapon slot
CLIENT = [
    (("a_cd_ra", "a_cd_re", "a_cd_sa", "a_cd_fa", "a_cd_fe0", "a_cd_fe1", "a_cd_fe2"),
     ("a_cd_fot", "a_cd_fos", "a_cd_ox", "a_cd_oz", "a_cd_oh", "a_cd_oc", "a_cd_fdc",
      "a_cd_fd0", "a_cd_fd1", "a_cd_fd2", "a_cd_fd3", "a_cd_dd")),
    (("a_cd_ra2", "a_cd_re2", "a_cd_sa2", "a_cd_fa2", "a_cd_fe3", "a_cd_fe4", "a_cd_fe5"),
     ("a_cd_fot2", "a_cd_fos2", "a_cd_ox2", "a_cd_oz2", "a_cd_oh2", "a_cd_oc2", "a_cd_fdc2",
      "a_cd_fd4", "a_cd_fd5", "a_cd_fd6", "a_cd_fd7", "a_cd_dd2")),
]
# numeric slot kinds: c = char, f = float
CLIENT_KINDS = "cffffccfffff"

SKILL_SQL = """select a_index, a_job, a_job2, a_apet_index, a_name_brz, a_name_usa, a_type, a_flag,
 a_sorcerer_flag, a_maxLevel, a_appRange, a_fireRange, a_fireRange2, a_targetType, a_targetNum,
 a_useState, a_useWeaponType0, a_useWeaponType1, a_useMagicIndex1, a_useMagicLevel1,
 a_useMagicIndex2, a_useMagicLevel2, a_useMagicIndex3, a_useMagicLevel3, a_appState,
 a_readyTime, a_stillTime, a_fireTime, a_reuseTime, %s,
 a_client_description_brz, a_client_tooltip_brz, a_client_description_usa, a_client_tooltip_usa,
 a_client_icon_texid, a_client_icon_row, a_client_icon_col, a_soul_consum, a_cd_fe_after
 from t_skill order by a_index""" % ", ".join(c for w in CLIENT for grp in w for c in grp)

LEVEL_SQL = "select * from t_skilllevel order by a_index, a_level"


def default_level():
    return {"a_needHP": 0, "a_needMP": 0, "a_durtime": 0, "a_dummypower": 0,
            "a_needItemIndex1": -1, "a_needItemCount1": 0, "a_needItemIndex2": -1, "a_needItemCount2": 0,
            "a_learnLevel": 0, "a_learnSP": 0}


def build():
    rep = Report("Skills_brz.bin", "DB t_skill + t_skilllevel + strSkill_br.lod (+old skills_brz.bin text fallback)")
    rows = db.query("t_skill", SKILL_SQL)
    lv_rows = db.query("t_skilllevel", LEVEL_SQL)
    levels = {}
    for L in lv_rows:
        levels.setdefault(db.I(L["a_index"]), {})[db.I(L["a_level"])] = L
    st = load_str("strSkill", 3)
    _, old = legacy.read_skills_brz()
    I, F = db.I, db.F

    w = Writer()
    last = max(I(r["a_index"]) for r in rows)
    w.i32(last)
    rep.header = last
    for r in rows:
        idx = I(r["a_index"])
        o = old.get(idx, {})
        w.i32(idx)
        w.i32(I(r["a_job"]))
        w.i32(I(r["a_job2"]))
        w.i32(I(r["a_apet_index"]))
        name, src = pick(st.get(idx, 0), r["a_name_brz"], o.get("name"), r["a_name_usa"])
        if src < 0:
            name = ("Skill %d" % idx).encode()
        rep.count("name from " + ["strSkill_br", "DB a_name_brz", "old skills_brz", "DB a_name_usa", "placeholder"][src])
        if len(name) > NAME_LEN - 1:
            rep.count("name truncated to 50 bytes")
        w.lstr(name, NAME_LEN)
        w.i8(s8(I(r["a_type"])))
        w.i32(I(r["a_flag"]))
        w.i32(I(r["a_sorcerer_flag"]))
        maxl = I(r["a_maxLevel"])
        if maxl > 127:
            rep.count("maxLevel clamped to 127")
            maxl = 127
        w.i8(maxl)
        w.f32(F(r["a_appRange"]))
        w.f32(F(r["a_fireRange"]))
        w.f32(F(r["a_fireRange2"]))
        w.i8(s8(I(r["a_targetType"])))
        w.i8(s8(I(r["a_targetNum"])))
        w.i32(I(r["a_useState"]))
        w.i32(I(r["a_useWeaponType0"]))
        w.i32(I(r["a_useWeaponType1"]))
        for k in (1, 2, 3):
            w.i32(I(r["a_useMagicIndex%d" % k], -1))
            w.i8(s8(I(r["a_useMagicLevel%d" % k])))
        w.i32(I(r["a_appState"]))
        w.i32(I(r["a_readyTime"]))
        w.i32(I(r["a_stillTime"]))
        w.i32(I(r["a_fireTime"]))
        w.i32(I(r["a_reuseTime"]))
        if I(r["a_soul_consum"]):
            rep.count("useSoulCount dropped (nonzero)")
        if db.S(r["a_cd_fe_after"]):
            rep.count("after-attach effect dropped (nonempty)")
        for strs, nums in CLIENT:
            for c in strs:
                v = db.S(r[c])
                if len(v) > CLIENT_STR_LEN - 1:
                    rep.count("client anim/effect string truncated")
                w.lstr(v, CLIENT_STR_LEN)
            for c, kind in zip(nums, CLIENT_KINDS):
                if kind == "c":
                    w.i8(s8(I(r[c])))
                else:
                    w.f32(F(r[c]))
        desc, _ = pick(strip_prefix(st.get(idx, 1)), r["a_client_description_brz"], o.get("desc"),
                       r["a_client_description_usa"])
        tip, _ = pick(strip_prefix(st.get(idx, 2)), r["a_client_tooltip_brz"], o.get("tooltip"),
                      r["a_client_tooltip_usa"])
        for t in (desc, tip):
            if len(t) > DESC_LEN - 1:
                rep.count("description/tooltip truncated to 255 bytes")
        w.lstr(desc, DESC_LEN)
        w.lstr(tip, DESC_LEN)
        w.i32(I(r["a_client_icon_texid"]))
        w.i32(I(r["a_client_icon_row"]))
        w.i32(I(r["a_client_icon_col"]))

        lv = levels.get(idx, {})
        for j in range(maxl):
            L = lv.get(j + 1)
            if L is None:
                rep.count("missing t_skilllevel row -> defaults")
                L = default_level()
            g = lambda c, d=0: I(L.get(c), d)
            for c in LEVEL_COLS:
                w.i32(g(c, -1 if "Index" in c else 0))
            for k in (1, 2, 3):
                w.i32(g("a_learnSkillIndex%d" % k, -1))
                w.i8(s8(g("a_learnSkillLevel%d" % k)))
            for k in (1, 2, 3):
                w.i32(g("a_learnItemIndex%d" % k, -1))
                w.i32(g("a_learnItemCount%d" % k))
            for c in ("a_learnstr", "a_learndex", "a_learnint", "a_learncon"):
                w.i32(g(c))
            for pre in ("a_appMagic", "a_magic"):
                for k in (1, 2, 3):
                    w.i32(g("%sIndex%d" % (pre, k), -1))
                    w.i8(s8(g("%sLevel%d" % (pre, k))))
            w.i32(g("a_learnGP"))           # NEW_GUILD_SYSTEM
            if g("a_needGP"):
                rep.count("level needGP dropped (nonzero)")
        rep.records += 1

    rep.note("2015 per-level fields with no 2009 slot: needGP, attribute att/def (+levels), targetmax, hate, use_count.")
    rep.note("indices missing from old skills_brz.bin: %d; covered now." % len(set(I(r['a_index']) for r in rows) - set(old)))
    path = w.save(out("Skills_brz.bin"))
    return path, rep
