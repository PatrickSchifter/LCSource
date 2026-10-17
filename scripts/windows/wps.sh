#!/bin/bash
# Runs a PowerShell script read from stdin on the 'windows' SSH host, without quoting problems.
#   scripts/windows/wps.sh <<'EOF'
#   Get-Process Nksp
#   EOF
prelude=$'$ProgressPreference = "SilentlyContinue"\n[Console]::OutputEncoding = [Text.Encoding]::UTF8\n'
script="$prelude$(cat)"
enc=$(printf '%s' "$script" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
ssh -o ConnectTimeout=10 -o BatchMode=yes windows "powershell -NoProfile -NonInteractive -EncodedCommand $enc" 2>&1 | LC_ALL=C tr -d '\r'
