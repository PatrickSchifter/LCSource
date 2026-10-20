"""questAll_brz.lod (2009 CQuestSystem::Load; PKPENALTY_QUEST and NEW_QUESTBOOK off).

CTFileStream-based: the loop runs while i <= lastIndex and jumps i to each
record's index, so records must be ascending and the LAST record's index must
equal the header (otherwise it reads past EOF and throws).
Source: server DB t_quest (a_enable=1 rows = the quests the server loads);
text: strQuest_br.lod (name, desc, desc2, desc3) > DB a_*_brz > a_*_usa.
"""
import db
from common import Writer, Report, out, load_str, pick, strip_prefix

NAME_LEN, DESC_LEN = 50, 2048
MAX_NEED_ITEM, MAX_COND, MAX_COND_DATA, MAX_PRIZE = 5, 3, 4, 5
INT_MIN, INT_MAX = -2 ** 31, 2 ** 31 - 1


def build():
    rep = Report("questAll_brz.lod", "DB t_quest (a_enable=1) + strQuest_br.lod")
    rows = db.query("t_quest", "select * from t_quest order by a_index")
    st = load_str("strQuest", 4)
    total = len(rows)
    rows = [r for r in rows if db.I(r["a_enable"]) == 1]
    rep.note("%d disabled quests (a_enable=0) left out; the server does not load them." % (total - len(rows)))
    last = max(db.I(r["a_index"]) for r in rows)
    w = Writer()
    w.i32(last)
    rep.header = last

    def i(v, d=0):
        x = db.I(v, d)
        if x < INT_MIN or x > INT_MAX:
            rep.count("value clamped to int32 (bigint a_prize_data*, e.g. EXP > 2^31)")
            x = max(INT_MIN, min(INT_MAX, x))
        return x

    for r in rows:
        idx = db.I(r["a_index"])
        name, src = pick(st.get(idx, 0), r["a_name_brz"], r["a_name_usa"])
        if src < 0:
            name = b"Quest %d" % idx
        rep.count("name from " + ["strQuest_br", "DB a_name_brz", "DB a_name_usa", "placeholder"][src])
        if len(name) > NAME_LEN - 1:
            rep.count("name truncated to 49 bytes")
        descs = []
        for k, col in ((1, "a_desc"), (2, "a_desc2"), (3, "a_desc3")):
            d, _ = pick(strip_prefix(st.get(idx, k)), r[col + "_brz"], r[col + "_usa"])
            if len(d) > DESC_LEN - 1:
                rep.count("description truncated to 2047 bytes")
            descs.append(d)

        w.i32(idx)
        w.lstr(name, NAME_LEN)
        for c in ("a_type1", "a_type2", "a_start_type", "a_start_data", "a_prize_npc", "a_prequest_num",
                  "a_start_npc_zone_num", "a_prize_npc_zone_num", "a_need_exp", "a_need_min_level",
                  "a_need_max_level", "a_need_job"):
            w.i32(i(r[c]))
        for j in range(MAX_NEED_ITEM):
            w.i32(i(r["a_need_item%d" % j], -1))
            w.i32(i(r["a_need_item_count%d" % j]))
        for j in range(MAX_COND):
            w.i32(i(r["a_condition%d_type" % j], -1))
            w.i32(i(r["a_condition%d_index" % j], -1))
            w.i32(i(r["a_condition%d_num" % j]))
            for t in range(MAX_COND_DATA):
                w.i32(i(r["a_condition%d_data%d" % (j, t)]))
        for j in range(MAX_PRIZE):
            w.i32(i(r["a_prize_type%d" % j], -1))
            w.i32(i(r["a_prize_index%d" % j], -1))
            w.i32(i(r["a_prize_data%d" % j]))
        w.i32(i(r["a_option_prize"]))
        for j in range(MAX_PRIZE):
            w.i32(i(r["a_opt_prize_type%d" % j], -1))
            w.i32(i(r["a_opt_prize_index%d" % j], -1))
            w.i32(i(r["a_opt_prize_data%d" % j]))
            w.i32(i(r["a_opt_prize_plus%d" % j]))
        for j in (5, 6):
            if db.I(r["a_opt_prize_type%d" % j], -1) >= 0 and db.I(r["a_opt_prize_index%d" % j], -1) > 0:
                rep.count("option prize slot 6/7 dropped (2009 has 5)")
        for c in ("a_need_rvr_type", "a_need_rvr_grade", "a_only_opt_prize", "a_start_give_item", "a_partyscale"):
            if db.I(r.get(c), 0) > 0:
                rep.count("2016-only field %s nonzero (dropped)" % c)
        for d in descs:
            w.lstr(d, DESC_LEN)
        rep.records += 1
    return w.save(out("questAll_brz.lod")), rep
