#!/bin/bash
# Copies the files changed since the last sync (committed, modified or untracked) to C:\LCSource on
# the Windows host. The last synced commit is kept in .git/lc-windows-last-sync; delete that file to
# send everything changed since the source import (07197bf), or use --verify to compare every file
# by hash and copy the ones that differ.
set -e
cd "$(dirname "$0")/../.."
STATE=.git/lc-windows-last-sync
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

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
  { git diff --name-only "$BASE"; git ls-files --others --exclude-standard; } | sort -u |
    while read -r f; do [[ -f $f ]] && printf '%s\0' "$f"; done > "$TMP/list" || true
fi

COPYFILE_DISABLE=1 tar --null -cf "$TMP/sync.tar" -T "$TMP/list"
scp -q -o BatchMode=yes "$TMP/sync.tar" windows:C:/LCSource/sync.tar
ssh -o BatchMode=yes windows "tar -xf C:/LCSource/sync.tar -C C:/LCSource; Remove-Item C:/LCSource/sync.tar"
git rev-parse HEAD > "$STATE"
echo "synced $(tar -tf "$TMP/sync.tar" | wc -l | tr -d ' ') files"
