"""Option_brz.lod (2009 COptionData::LoadOptionDataFromFile, OPTION_MAX_LEVEL 7).

Header = loop count AND array size (records are stored at [type]), so it is
max(record count, max type). Source: DB t_option (values), names from
strOption_br.lod (keyed by a_index) > DB a_name_brz > old option_brz.lod.
"""
import db
from common import Writer, Report, load_str, out, pick
import legacy

OPTION_MAX_LEVEL = 7
NAME_LEN = 50


def build():
    rep = Report("Option_brz.lod", "DB t_option + strOption_br.lod")
    rows = db.query("t_option", "select a_index, a_type, a_name_brz, a_name_usa, a_level from t_option order by a_index")
    st = load_str("strOption", 1)
    _, old, _ = legacy.read_option_brz()
    I = db.I
    types = [I(r["a_type"]) for r in rows]
    if len(set(types)) != len(types):
        rep.note("duplicate a_type values: later rows overwrite earlier ones in the client")
    hdr = max(len(rows), max(types))
    w = Writer()
    w.i32(hdr)
    rep.header = hdr
    for r in rows:
        idx = I(r["a_index"])
        o = old.get(idx, {})
        name, src = pick(st.get(idx), r["a_name_brz"], o.get("name"), r["a_name_usa"])
        if src < 0:
            name = b"Option %d" % I(r["a_type"])
        rep.count("name from " + ["strOption_br", "DB a_name_brz", "old option_brz", "DB a_name_usa", "placeholder"][src])
        vals = [int(v) for v in db.S(r["a_level"]).split()]
        if len(vals) > OPTION_MAX_LEVEL:
            rep.count("options with levels > 7 (values for lv8+ dropped)")
        elif len(vals) < OPTION_MAX_LEVEL:
            rep.count("options with < 7 levels (padded with 0)")
        vals = (vals + [0] * OPTION_MAX_LEVEL)[:OPTION_MAX_LEVEL]
        w.i32(idx)
        w.i32(I(r["a_type"]))
        w.lstr(name, NAME_LEN)
        w.ints(vals)
        rep.records += 1
    rep.note("server-side option levels 8..%d have no client value (2009 array is value[7])."
             % max(len(db.S(r["a_level"]).split()) for r in rows))
    rep.note("no 8-byte timestamp header (old option_brz.lod had one).")
    return w.save(out("Option_brz.lod")), rep
