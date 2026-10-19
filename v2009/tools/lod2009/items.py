"""ItemAll_brz.lod (2009 CItemData::LoadItemDataFromFile, LOAD_ITEMARMOR_EFFECT on,
DEL_PET_AI off).

Record set and numbers: server DB t_item (every row, so any index the server
sends exists client-side). Text: Local/br/String/strItem_br.lod > DB a_*_brz >
old itemALL_brz.lod > DB a_*_usa.
"""
import db
from common import Writer, Report, load_str, out, pick, strip_prefix, s32
import legacy

OTHER_LANGS = ("spn", "mex", "ita", "frc", "ger", "uk")
SRC_NAMES = ["strItem_br", "DB a_name_brz", "old itemALL_brz", "DB a_name_usa"]
NAME_LEN, DESCR_LEN, SMC_LEN, EFFECT_LEN = 50, 255, 255, 255
MAX_MAKE_ITEM_MATERIAL = 10

SQL = """select a_index, a_name_brz, a_descr_brz, a_name_usa, a_descr_usa,
 a_name_spn, a_name_mex, a_name_ita, a_name_frc, a_name_ger, a_name_uk, a_job_flag, a_weight,
 a_fame, a_level, a_flag, a_wearing, a_type_idx, a_subtype_idx,
 %s, %s,
 a_need_sskill, a_need_sskill_level, a_need_sskill2, a_need_sskill_level2,
 a_num_0, a_num_1, a_num_2, a_num_3, a_price, a_file_smc,
 a_texture_id, a_texture_row, a_texture_col, a_effect_name, a_enable
 from t_item order by a_index""" % (
    ", ".join("a_need_item%d" % i for i in range(10)),
    ", ".join("a_need_item_count%d" % i for i in range(10)))


_TEXTS = None


def texts():
    """{index: (name, descr, name_source)} as cp1252 bytes (untruncated)."""
    global _TEXTS
    if _TEXTS is not None:
        return _TEXTS
    rows = db.query("t_item", SQL)
    st = load_str("strItem", 2)
    _, old = legacy.read_items_brz()
    _TEXTS = {}
    for r in rows:
        idx = db.I(r["a_index"])
        o = old.get(idx, {})
        name, src = pick(st.get(idx, 0), r["a_name_brz"], o.get("name"), r["a_name_usa"],
                         *[r["a_name_" + k] for k in OTHER_LANGS])
        if src < 0:
            name = ("Item %d" % idx).encode()
        descr, _ = pick(strip_prefix(st.get(idx, 1)), r["a_descr_brz"], o.get("descr"), r["a_descr_usa"])
        _TEXTS[idx] = (name, descr, src)
    return _TEXTS


def build():
    rep = Report("ItemAll_brz.lod", "DB t_item + strItem_br.lod (+old itemALL_brz.lod text fallback)")
    rows = db.query("t_item", SQL)
    _, old = legacy.read_items_brz()
    tx = texts()
    I = db.I

    w = Writer()
    last = max(I(r["a_index"]) for r in rows)
    w.i32(last)
    rep.header = last
    for r in rows:
        idx = I(r["a_index"])
        name, descr, src = tx[idx]
        rep.count("name from " + (SRC_NAMES + ["DB other latin1 language", "placeholder 'Item <n>' (Korean-only row)"])[min(src, 4) if src >= 0 else 5])
        if len(name) > NAME_LEN - 1:
            rep.count("name truncated to 49 bytes")
        if len(descr) > DESCR_LEN - 1:
            rep.count("descr truncated to 254 bytes")
        flag = I(r["a_flag"])
        if flag >> 32:
            rep.count("flag bits >= 32 dropped (int64 -> int)")
        if I(r["a_enable"]) == 0:
            rep.count("disabled rows kept (a_enable=0)")

        w.i32(idx)
        w.lstr(name, NAME_LEN)
        w.i32(4)                      # job is read as LOADINT(len) + fread(&job, len)
        w.i32(I(r["a_job_flag"]))
        w.i32(I(r["a_weight"]))
        w.i32(I(r["a_fame"], -1))
        w.i32(I(r["a_level"]))
        w.i32(s32(flag))
        w.i32(I(r["a_wearing"], -1))
        w.i32(I(r["a_type_idx"]))
        w.i32(I(r["a_subtype_idx"]))
        for j in range(MAX_MAKE_ITEM_MATERIAL):
            w.i32(I(r["a_need_item%d" % j], -1))
            w.i32(I(r["a_need_item_count%d" % j]))
        w.i32(I(r["a_need_sskill"], -1))
        w.i32(I(r["a_need_sskill_level"]))
        w.i32(I(r["a_need_sskill2"], -1))
        w.i32(I(r["a_need_sskill_level2"]))
        for k in range(4):
            w.i32(I(r["a_num_%d" % k]))
        w.i32(I(r["a_price"]))
        smc = db.S(r["a_file_smc"])
        if len(smc) > SMC_LEN - 1:
            rep.count("smc path truncated")
        w.lstr(smc, SMC_LEN)
        w.i32(I(r["a_texture_id"]))
        w.i32(I(r["a_texture_row"]))
        w.i32(I(r["a_texture_col"]))
        w.lstr(descr, DESCR_LEN)
        w.lstr(db.S(r["a_effect_name"]), EFFECT_LEN)
        rep.records += 1

    rep.note("2009 has no attack/damage effect, origin options, rare options, set/fortune, "
             "syndicate or castle-war fields: dropped (DB a_attack_effect_name etc. unused).")
    rep.note("weight = DB a_weight (the 2016 DB uses it as max stack for stackables, same as the BR files).")
    rep.note("indices missing from old itemALL_brz.lod: %d; covered now." % len(set(I(r['a_index']) for r in rows) - set(old)))
    path = w.save(out("ItemAll_brz.lod"))
    return path, rep


def build_names():
    """ItemName_brz.lod (2009 CItemName::LoadItemNameFromFile): index, name[50], descr[255]."""
    rep = Report("ItemName_brz.lod", "same texts as ItemAll_brz.lod")
    tx = texts()
    w = Writer()
    last = max(tx)
    w.i32(last)
    rep.header = last
    for idx in sorted(tx):
        name, descr, _ = tx[idx]
        w.i32(idx)
        w.lstr(name, NAME_LEN)
        w.lstr(descr, DESCR_LEN)
        rep.records += 1
    rep.note("shipped itemname_brz.lod stops at 10013 and has the -9999 footer; regenerated.")
    return w.save(out("ItemName_brz.lod")), rep
