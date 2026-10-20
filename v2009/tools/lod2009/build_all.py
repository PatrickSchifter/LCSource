#!/usr/bin/env python3
"""Build every 2009-format client data file from the BR 2014 install + server DB,
then verify them with verify2009.py.

usage: build_all.py [--offline] [--out DIR] [--db lc2009_data|newproject_data]
  --offline   use the JSON cache of the DB tables (LC_2009_CACHE) instead of mysql
Inputs: LC_BR_ROOT (default /Users/patrickschifter/Projects/LC-BR2014), DB
newproject_data via the mysql CLI (LC_DB_* env). Output: LC_2009_OUT.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def strip_footer_copies(dst_dir):
    """Footer-free copies of the shipped BR files the 2009 client can also read
    (fallback only; the DB-regenerated files are the primary outputs)."""
    from common import br
    os.makedirs(dst_dir, exist_ok=True)
    done = []
    for src, dst in (("mobAll_brz.lod", "mobAll_brz.lod"), ("mobname_brz.lod", "MobName_brz.lod"),
                     ("itemname_brz.lod", "ItemName_brz.lod"), ("actions_brz.bin", "Actions_brz.bin")):
        with open(br(src), "rb") as f:
            d = f.read()
        cut = d.rfind(struct.pack("<i", -9999))
        if cut > 0 and len(d) - cut == 8:            # footer + 4-byte checksum
            d = d[:cut]
        with open(os.path.join(dst_dir, dst), "wb") as f:
            f.write(d)
        done.append(dst)
    return done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--offline", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--db", default=None, help="data DB (default lc2009_data; or newproject_data)")
    a = ap.parse_args()
    if a.out:
        os.environ["LC_2009_OUT"] = a.out
    import common
    import db
    db.OFFLINE = a.offline
    if a.db:
        db.set_db(a.db)
    print("data DB:", db.DB_NAME, "(offline cache)" if a.offline else "")
    import items, skills, options, catalog, npchelp, bigpet, quests, mobs, actions

    results = []
    results.append(items.build())
    results.append(items.build_names())
    results.append(skills.build())
    results.append(options.build())
    results.append(catalog.build())
    results.append(npchelp.build())
    results.append(bigpet.build())
    results.append(quests.build())
    results += mobs.build()
    results.append(actions.build())

    report = "\n\n".join(r.text() for _p, r in results)
    rp = os.path.join(os.path.dirname(common.OUT_DATA), "BUILD_REPORT.txt")
    with open(rp, "w", encoding="utf-8") as f:
        f.write(report + "\n")
    print(report)
    print("\nreport: " + rp)

    nf = os.path.join(os.path.dirname(common.OUT_DATA), "br-nofooter")
    print("footer-free copies of shipped BR files ->", nf, strip_footer_copies(nf))

    print("\n== verify (2009 read sequences) ==")
    args = [sys.executable, os.path.join(HERE, "verify2009.py"), "--dir", common.OUT_DATA, "--db", db.DB_NAME]
    if a.offline:
        args.append("--offline")
    return subprocess.call(args)


if __name__ == "__main__":
    sys.exit(main())
