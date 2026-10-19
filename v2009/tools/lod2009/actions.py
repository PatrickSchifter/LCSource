"""Actions_brz.bin (2009 CAction::LoadActionDataFromFile, CTFileStream).

count, then exactly `count` records (index <= count): index, type (SBYTE),
name (len + bytes, char[32] with NUL written at [len]), desc (char[256]),
6 animation names (char[32]), job (LONG), tex id/row/col.
The shipped actions_brz.bin has 52 records but DB t_action has 53 (max index
53), so it is regenerated from DB t_action; text: strAction_br (name, desc) >
DB a_name_brz > old actions_brz.bin.
"""
import db
from common import Reader, Writer, Report, br, out, load_str, pick, strip_prefix, s8

NAME_LEN, DESC_LEN, ANI_LEN, TOTAL_JOB = 32, 256, 32, 6


def read_old():
    r = Reader.open(br("actions_brz.bin"))
    n = r.i32()
    res = {}
    for _ in range(n):
        i = r.i32()
        d = {"type": r.i8(), "name": r.lstr(), "desc": r.lstr(), "anis": [r.lstr() for _ in range(TOTAL_JOB)]}
        d["job"] = r.i32()
        d["tex"] = r.ints(3)
        res[i] = d
    return n, res, r


def build():
    rep = Report("Actions_brz.bin", "DB t_action + strAction_br.lod")
    rows = db.query("t_action", "select * from t_action order by a_index")
    st = load_str("strAction", 2)
    _, old, _ = read_old()
    I = db.I
    n = len(rows)
    mx = max(I(r["a_index"]) for r in rows)
    if mx > n:
        rep.note("max index %d > count %d: header raised, client reads past EOF!" % (mx, n))
    w = Writer()
    w.i32(n)
    rep.header = n
    for r in rows:
        idx = I(r["a_index"])
        o = old.get(idx, {})
        name, src = pick(st.get(idx, 0), r["a_name_brz"], o.get("name"), r["a_name_usa"])
        rep.count("name from " + ["strAction_br", "DB a_name_brz", "old actions_brz", "DB a_name_usa", "<empty>"][src])
        desc, _ = pick(strip_prefix(st.get(idx, 1)), r["a_client_description_brz"], o.get("desc"),
                       r["a_client_description_usa"])
        if len(name) > NAME_LEN - 1:
            rep.count("name truncated to 31 bytes")
        if len(desc) > DESC_LEN - 1:
            rep.count("description truncated to 255 bytes")
        w.i32(idx)
        w.i8(s8(I(r["a_type"])))
        w.lstr(name, NAME_LEN)
        w.lstr(desc, DESC_LEN)
        for j in range(TOTAL_JOB):
            w.lstr(db.S(r["a_client_ani%d" % (j + 1)]), ANI_LEN)
        w.i32(I(r["a_job"]))
        w.i32(I(r["a_iconid"]))
        w.i32(I(r["a_iconrow"]))
        w.i32(I(r["a_iconcol"]))
        rep.records += 1
    rep.note("2009 TOTAL_JOB = 6: animations for later classes (NightShadow, EX) have no slot.")
    return w.save(out("Actions_brz.bin")), rep
