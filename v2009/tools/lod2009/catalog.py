"""catalog.lod (2009 CCashShopData::LoadShopDataFromFile with NEW_SHOP + ITEM_ENABLE).

Records/prices/item lists: server DB t_catalog + t_ct_item (the server sells by
a_ctid, so ids must be the DB ones; the BR catalog*.lod files are renumbered
1..N and do not match). The 2016 category/type codes are mapped onto the ten
2009 tabs; products are matched to the BR catalog files by their item list to
recover Portuguese names/descriptions, else the BR item name is used.
"""
import db
import items
from common import Reader, Writer, Report, br, out, pick

NAME_LEN, DESC_LEN = 1024, 2048
MAX_CASHSHOP_CLASS = 10
KINDS = ["CONSUME", "TIME_LIMIT", "POTION", "AVATA", "WEAPON", "DEFEND", "SET", "PLATINUM"]
CATALOG_FLAG_NEW, CATALOG_FLAG_POP = 1, 2


def kind_of(cat, typ, sub):
    """2016 category/type/subtype -> 2009 CASH_SHOP_KIND (0..7)."""
    if cat == 20000:
        return 7
    if cat == 30000:
        return 2 if typ == 30400 else 0
    if cat == 40000:
        if sub == 40202:
            return 4
        if sub == 40204:
            return 3
        if typ in (40100, 40200):
            return 5
        return 0                      # 40300 equipment build-up scrolls
    if cat == 50000:
        return 3
    if cat == 60000:
        return 1
    if cat == 70000:
        return 6
    return 0


def read_catalog_2015(path):
    """2015 catalog format (idx, type, cash, mileage, flag, enable, name, desc,
    n x 5 ints, refItem)."""
    r = Reader.open(path)
    n = r.i32()
    res = []
    for _ in range(n):
        d = {"idx": r.i32(), "type": r.i32(), "cash": r.i32(), "mileage": r.i32(), "flag": r.i32(),
             "enable": r.u8(), "name": r.lstr(), "desc": r.lstr()}
        k = r.i32()
        d["items"] = [r.ints(5) for _ in range(k)]
        d["ref"] = r.i32()
        res.append(d)
    return res


def item_key(lst):
    return tuple(sorted((it[0], it[4]) for it in lst))


def build():
    rep = Report("catalog.lod", "DB t_catalog + t_ct_item; BR names matched from catalog_brz.lod/catalog.lod by item list")
    cats = db.query("t_catalog", "select a_ctid, a_category, a_type, a_subtype, a_cash, a_mileage, a_flag, a_enable, "
                                 "a_texture_id, a_texture_row, a_texture_col from t_catalog order by a_ctid")
    cti = db.query("t_ct_item", "select a_index, a_ctid, a_item_idx, a_item_flag, a_item_plus, a_item_option, a_item_num "
                                "from t_ct_item order by a_ctid, a_index")
    irows = {db.I(r["a_index"]): r for r in db.query("t_item", items.SQL)}
    tx = items.texts()
    lists = {}
    for c in cti:
        lists.setdefault(db.I(c["a_ctid"]), []).append(
            [db.I(c["a_item_idx"]), db.I(c["a_item_flag"]), db.I(c["a_item_plus"]),
             db.I(c["a_item_option"]), db.I(c["a_item_num"])])
    brnames = {}
    for f in ("catalog.lod", "catalog_brz.lod"):          # later file wins
        for d in read_catalog_2015(br(f)):
            if d["items"] and d["name"].strip():
                brnames[item_key(d["items"])] = (d["name"], d["desc"])

    I = db.I
    w = Writer()
    w.i32(len(cats))
    rep.header = len(cats)
    for c in cats:
        ctid = I(c["a_ctid"])
        cat, typ, sub = I(c["a_category"]), I(c["a_type"]), I(c["a_subtype"])
        lst = lists.get(ctid, [])
        kind = kind_of(cat, typ, sub)
        enable = I(c["a_enable"], 1)
        if cat == 90000:
            enable = 0
            rep.count("category 90000 (not shown by 2015 client) -> enable=0")
        rep.count("tab %d %s" % (kind, KINDS[kind]))
        flag = I(c["a_flag"])
        if flag & ~3:
            rep.count("flag bits other than NEW/POP dropped")
        flag &= CATALOG_FLAG_NEW | CATALOG_FLAG_POP

        tex = [I(c["a_texture_id"], -1), I(c["a_texture_row"], -1), I(c["a_texture_col"], -1)]
        if tex[0] < 0 and lst and lst[0][0] in irows:
            ir = irows[lst[0][0]]
            tex = [I(ir["a_texture_id"]), I(ir["a_texture_row"]), I(ir["a_texture_col"])]
            rep.count("icon taken from first item (2015 refItem rule)")
        m = brnames.get(item_key(lst))
        if m:
            name, desc = m
            rep.count("name/desc matched from BR catalog file")
        else:
            first = tx.get(lst[0][0], (b"", b"", -1)) if lst else (b"", b"", -1)
            name = first[0]
            if len(lst) == 1 and lst[0][4] > 1:
                name += b" x%d" % lst[0][4]
            elif len(lst) > 1:
                name += b" (+%d)" % (len(lst) - 1)
            desc = first[1]
            rep.count("name/desc built from item names")
        name, _ = pick(name, "Produto %d" % ctid)

        w.i32(ctid)
        w.i32(kind)
        w.i32(I(c["a_cash"]))
        w.i32(I(c["a_mileage"]))
        w.ints(tex)
        w.i32(flag)                    # NEW_SHOP
        w.u8(1 if enable else 0)       # ITEM_ENABLE
        w.lstr(name, NAME_LEN)
        w.lstr(desc, DESC_LEN)
        w.i32(len(lst))
        for it in lst:
            w.ints(it)
        rep.records += 1
    rep.note("2016 category/type/subtype collapsed onto 2009 tabs (see kind_of); 10000 HOT&NEW would map via flag.")
    return w.save(out("catalog.lod")), rep
