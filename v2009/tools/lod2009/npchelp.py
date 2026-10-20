"""npchelp_brz.lod (2009 CNpcHelp::LoadNpcListFromFile, HELP_SYSTEM_1).

2009 record: zone, npc, name (char[50]), explanation (char[1024]).
NPC list: 2015 Data/npchelp.lod (packed {npc, zone}). Name: strNpcName_br col 0 >
DB t_npc a_name_brz > a_name_usa; explanation: strNpcName_br col 1 (the 2015
client's NPC-help text) > old npchelp_brz.lod explanation > strNPCHelp_br col 0.
"""
import db
from common import Reader, Writer, Report, br, out, load_str, pick, strip_prefix
import legacy

NAME_LEN, EXPLAN_LEN = 50, 1024


def read_npchelp_2015(path=None):
    r = Reader.open(path or br("npchelp.lod"))
    n = r.i32()
    lst = [(r.i32(), r.i32()) for _ in range(n)]
    assert r.eof()
    return lst


def build():
    rep = Report("npchelp_brz.lod", "Data/npchelp.lod list + strNpcName_br.lod (+old npchelp_brz.lod text)")
    lst = read_npchelp_2015()
    names = load_str("strNpcName", 2)
    helps = load_str("strNPCHelp", 2)
    _, _, old_expl = legacy.read_npchelp_brz()
    npc = {db.I(r["a_index"]): r for r in db.query("t_npc_names", "select a_index, a_name_brz, a_name_usa from t_npc")}
    w = Writer()
    w.i32(len(lst))
    rep.header = len(lst)
    for npc_idx, zone in lst:
        r = npc.get(npc_idx, {})
        if not r:
            rep.count("npc not in DB t_npc (kept)")
        name, src = pick(names.get(npc_idx, 0), r.get("a_name_brz"), r.get("a_name_usa"))
        if src < 0:
            name = b"NPC %d" % npc_idx
        rep.count("name from " + ["strNpcName_br", "DB a_name_brz", "DB a_name_usa", "placeholder"][src])
        expl, esrc = pick(strip_prefix(names.get(npc_idx, 1)), old_expl.get(npc_idx), helps.get(npc_idx, 0))
        rep.count("explanation from " + ["strNpcName_br col1", "old npchelp_brz", "strNPCHelp_br", "<empty>"][esrc])
        w.i32(zone)
        w.i32(npc_idx)
        w.lstr(name, NAME_LEN)
        w.lstr(expl, EXPLAN_LEN)
        rep.records += 1
    rep.note("2009 has no monster-help list (old npchelp_brz.lod's 281 mob entries have no 2009 slot).")
    return w.save(out("npchelp_brz.lod")), rep
