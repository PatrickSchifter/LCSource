#!/bin/bash
# Copies the built client into the game install (C:\LC-BR2014\Bin), clears ThrowLog.txt and starts
# the game on the Windows desktop (scheduled task "LC Nosso Cliente" -> C:\LC-BR2014\jogar.bat).
# Closes a running client first.
"$(dirname "$0")/wps.sh" <<'EOF'
Stop-Process -Name Nksp -Force -ErrorAction SilentlyContinue; Start-Sleep 2
Copy-Item C:\LCSource-build\Bin\*.dll, C:\LCSource-build\Bin\*.pdb, C:\LCSource-build\Bin\Nksp.exe C:\LC-BR2014\Bin -Force
if (Test-Path C:\LCSource-build\Bin\Cursors) { Copy-Item C:\LCSource-build\Bin\Cursors C:\LC-BR2014\Bin -Recurse -Force }
Set-Content C:\LC-BR2014\ThrowLog.txt "" -Encoding ASCII
schtasks /Run /TN "LC Nosso Cliente" | Out-Null
Start-Sleep 30
$p = Get-Process Nksp -ErrorAction SilentlyContinue
if ($p) { "cliente rodando, {0:N0} MB" -f ($p.WorkingSet64/1MB) } else { "cliente NAO esta rodando" }
EOF
