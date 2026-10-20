#!/bin/bash
# Copies the files changed since the last sync (committed, modified or untracked) to C:\LCSource on
# the Windows host. C:\LCSource mirrors v2015/ (the 2015 sources were at the top of the repository
# before 1d4c8bc; a last sync from then is compared against that top-level tree). The last synced commit is kept in .git/lc-windows-last-sync; delete that file to
# send everything changed since the source import (07197bf), or use --verify to compare every file
# by hash and copy the ones that differ.
set -e
cd "$(dirname "$0")/../.."
STATE=.git/lc-windows-last-sync
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# files changed since $BASE, relative to v2015/ (git diff options in "$@")
changed() {
  if git cat-file -e "$BASE:v2015" 2>/dev/null; then
    git diff --name-only --relative "$@" "$BASE"
  else
    git diff --name-only "$@" "$BASE:" HEAD:v2015
    git diff --name-only --relative "$@" HEAD
  fi
}

if [[ $1 == --verify ]]; then
  git ls-files "Client Source" "Server Source/ShareLib" | while read -r f; do
    [[ -f $f ]] && printf '%s\t%s\n' "$(md5 -q "$f")" "$f"
  done > "$TMP/hashes.txt"
  scp -q -o BatchMode=yes "$TMP/hashes.txt" windows:C:/LCSource/mac-hashes.txt
  "$(dirname "$0")/wps.sh" > "$TMP/diff.txt" <<'EOF'
$md5 = [System.Security.Cryptography.MD5]::Create()
Get-Content C:\LCSource\mac-hashes.txt -Encoding UTF8 | % {
  $h, $f = $_ -split "`t", 2
  $p = Join-Path C:\LCSource $f
  if (-not (Test-Path -LiteralPath $p)) { $f }
  else { $w = -join ($md5.ComputeHash([IO.File]::ReadAllBytes($p)) | % { $_.ToString("x2") }); if ($w -ne $h) { $f } }
}
EOF
  tr '\n' '\0' < "$TMP/diff.txt" > "$TMP/list"
else
  BASE=$(cat "$STATE" 2>/dev/null || echo 07197bf)
  { changed; git ls-files --others --exclude-standard; } | sort -u |
    while read -r f; do [[ -f $f ]] && printf '%s\0' "$f"; done > "$TMP/list" || true
fi

if [[ $1 != --verify ]]; then
  # Files deleted here since the last sync are deleted there too.
  { changed --diff-filter=D; git ls-files --deleted; } | sort -u |
    while read -r f; do [[ -e $f ]] || printf '%s\n' "$f"; done > "$TMP/deleted.txt"
  if [[ -s $TMP/deleted.txt ]]; then
    scp -q -o BatchMode=yes "$TMP/deleted.txt" windows:C:/LCSource/mac-deleted.txt
    "$(dirname "$0")/wps.sh" <<'EOF'
Get-Content C:\LCSource\mac-deleted.txt -Encoding UTF8 | % {
  $p = Join-Path C:\LCSource $_
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
  # and the directories left empty
  $d = Split-Path $p
  while ((Test-Path -LiteralPath $d) -and -not (Get-ChildItem -LiteralPath $d -Force)) { Remove-Item -LiteralPath $d; $d = Split-Path $d }
}
Remove-Item C:\LCSource\mac-deleted.txt
EOF
    echo "deleted $(wc -l < "$TMP/deleted.txt" | tr -d ' ') files"
  fi
fi

COPYFILE_DISABLE=1 tar --null -cf "$TMP/sync.tar" -T "$TMP/list"
scp -q -o BatchMode=yes "$TMP/sync.tar" windows:C:/LCSource/sync.tar
# -m: stamp files with the extraction time, so ninja never sees a source older than its object
ssh -o BatchMode=yes windows "tar -m -xf C:/LCSource/sync.tar -C C:/LCSource; Remove-Item C:/LCSource/sync.tar"
git rev-parse HEAD > "$STATE"
echo "synced $(tar -tf "$TMP/sync.tar" | wc -l | tr -d ' ') files"
