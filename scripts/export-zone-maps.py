#!/usr/bin/env python3
"""Export the GameServer's zone maps (.sat/.sht) from the client worlds and install them.

Runs ZoneMapExport.exe (Client Source/ZoneMapExport) on the Windows test host, which loads each
zone's .wld with our engine and draws the maps the way the World Editor's exporters do. The world
of each zone comes from the client's Data/zone_data.bin; map sizes, floors and file names come
from t_zonedata. Results are copied to run/GameServer/, replacing placeholders and older maps.

    scripts/export-zone-maps.py [zone_index ...]   (default: every zone in t_zonedata)

Needs ZoneMapExport.exe and our Engine.dll in C:\\LC-BR2014\\Bin (scripts/windows/build.sh
ZoneMapExport, then copy from C:\\LCSource-build\\Bin).
"""
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "run" / "GameServer"
GAME = r"C:\LC-BR2014"
GAME_SCP = GAME.replace("\\", "/")
REMOTE_DIR = "ServerMaps"          # relative to GAME; the engine resolves paths from there
ZONE_RECORD = 276                  # zone_data.bin: int count, then records with the .wld at +12
TIMEOUT_S = 900


def zone_worlds():
    with tempfile.TemporaryDirectory() as tmp:
        local = pathlib.Path(tmp) / "zone_data.bin"
        subprocess.run(["scp", "-q", "-o", "BatchMode=yes", f"windows:{GAME_SCP}/Data/zone_data.bin", str(local)],
                       check=True)
        data = local.read_bytes()
    count, = struct.unpack_from("<i", data)
    worlds = {}
    for zone in range(count):
        name = data[4 + zone * ZONE_RECORD + 12:][:128].split(b"\0")[0].decode("ascii")
        worlds[zone] = "Data\\World\\" + name
    return worlds


def zone_rows(zones):
    query = "SELECT a_zone_index, a_width, a_height, a_attrmap, a_heightmap FROM t_zonedata"
    if zones:
        query += " WHERE a_zone_index IN (%s)" % ",".join(str(z) for z in zones)
    out = subprocess.run(["mariadb", "-N", "-B", "newproject_data", "-e", query + " ORDER BY a_zone_index"],
                         check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        zone, w, h, attrs, heights = line.split("\t")
        yield int(zone), int(w), int(h), attrs.split(), heights.split()


def powershell_quote(s):
    return "'" + s.replace("'", "''") + "'"


def main():
    zones = [int(z) for z in sys.argv[1:]]
    worlds = zone_worlds()
    jobs = []
    for zone, w, h, attrs, heights in zone_rows(zones):
        if zone not in worlds:
            print(f"zone {zone}: not in zone_data.bin; skipped")
            continue
        # (remote file, path under run/GameServer), floor by floor: .sat then .sht
        files = [(f"{REMOTE_DIR}\\{pathlib.PurePosixPath(name).name}", name)
                 for floor in zip(attrs, heights) for name in floor]
        jobs.append((zone, [worlds[zone], str(w), str(h)] + [remote for remote, _ in files], files))

    # One PowerShell session runs every export; a hung one (e.g. waiting on an engine MessageBox)
    # gets its stacks dumped and is killed.
    script = [f"New-Item -ItemType Directory -Force {GAME}\\{REMOTE_DIR} | Out-Null", f"cd {GAME}\\Bin"]
    for zone, args, _ in jobs:
        arglist = ",".join(powershell_quote('"%s"' % a) for a in args)
        script += [
            f"if (-not (Test-Path -LiteralPath {powershell_quote(GAME + chr(92) + args[0])})) "
            f"{{ 'zone {zone}: {args[0]} not found' }} else {{",
            f"  $p = Start-Process .\\ZoneMapExport.exe -ArgumentList {arglist} -NoNewWindow -PassThru "
            f"-RedirectStandardOutput $env:TEMP\\zme.out -RedirectStandardError $env:TEMP\\zme.err; $null = $p.Handle",
            f"  if (-not $p.WaitForExit({TIMEOUT_S * 1000})) {{ 'zone {zone}: TIMEOUT'; "
            f"& C:\\LCSource-deps\\stackdump.exe $p.Id | Select -First 20; Stop-Process -Id $p.Id -Force }}",
            f"  else {{ 'zone {zone}: exit ' + $p.ExitCode }}",
            "  Get-Content $env:TEMP\\zme.out, $env:TEMP\\zme.err | ? { $_ -notmatch '^(Qtns|Tris|Vtxs|Tiles|Tile |Geometry)' }",
            "}",
        ]
    # Too long for wps.sh's command line: run it from a file.
    with tempfile.TemporaryDirectory() as tmp:
        ps1 = pathlib.Path(tmp) / "export-zone-maps.ps1"
        ps1.write_text("\n".join(script), encoding="utf-8-sig")
        subprocess.run(["scp", "-q", "-o", "BatchMode=yes", str(ps1), f"windows:{GAME_SCP}/{REMOTE_DIR}.ps1"],
                       check=True)
    subprocess.run([str(ROOT / "scripts" / "windows" / "wps.sh")],
                   input=f"Invoke-Expression (Get-Content -Raw {GAME}\\{REMOTE_DIR}.ps1)", text=True, check=True)

    (OUT / "data").mkdir(parents=True, exist_ok=True)
    for zone, _, files in jobs:
        for remote, name in files:
            r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes",
                                f"windows:{GAME_SCP}/{remote.replace(chr(92), '/')}", str(OUT / name)])
            if r.returncode == 0:
                print(f"zone {zone}: installed {name}")


if __name__ == "__main__":
    main()
