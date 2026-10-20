#!/bin/bash
# Diagnoses the client on Windows:
#   scripts/windows/crash.sh          last crash in C:\LC-BR2014\ErrorLog.txt -> function and file:line
#   scripts/windows/crash.sh stacks   live stacks of every thread of a running (e.g. hung) Nksp.exe
#   scripts/windows/crash.sh errors   distinct errors thrown by the engine (C:\LC-BR2014\ThrowLog.txt)
#   scripts/windows/crash.sh shot     screenshot of the Windows desktop -> ./shot.png
# Uses C:\LCSource-deps\addr2line.exe and stackdump.exe (sources next to this script; build them
# with cl from a VS x86 prompt: cl /O1 addr2line.cpp).
D=$(dirname "$0")
case "$1" in
stacks)
  "$D/wps.sh" <<'EOF'
$p = Get-Process Nksp -ErrorAction SilentlyContinue | Select -First 1
if ($p) { & C:\LCSource-deps\stackdump.exe $p.Id | Select -First 60 } else { "Nksp nao esta rodando" }
EOF
  ;;
errors)
  "$D/wps.sh" <<'EOF'
Get-Content C:\LC-BR2014\ThrowLog.txt | ? { $_ -notmatch '^\s|must reside|wrong path' } | Group-Object | Sort Count -Desc | Select -First 40 | % { "{0,5} {1}" -f $_.Count, $_.Name }
EOF
  ;;
shot)
  "$D/wps.sh" <<'EOF'
$ps = 'Add-Type -AssemblyName System.Windows.Forms,System.Drawing; $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height; [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size); $bmp.Save("C:\LC-BR2014\shot.png")'
Set-Content C:\LC-BR2014\shot.ps1 $ps
Remove-Item C:\LC-BR2014\shot.png -ErrorAction SilentlyContinue
schtasks /Create /TN "LC Shot" /TR "powershell -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\LC-BR2014\shot.ps1" /SC ONCE /ST 23:59 /IT /F | Out-Null
schtasks /Run /TN "LC Shot" | Out-Null
Start-Sleep 6
EOF
  scp -q windows:C:/LC-BR2014/shot.png ./shot.png && echo "shot.png"
  ;;
*)
  "$D/wps.sh" <<'EOF'
$c = Get-Content C:\LC-BR2014\ErrorLog.txt
$i = ($c | Select-String "Crashed at:" | Select -Last 1).LineNumber
if (-not $i) { "nenhum crash no ErrorLog.txt"; exit }
$c[($i-1)..($i+2)]
# Module-relative offsets ("Engine.dll 0001:XXXXXXXX") of the crash frames, resolved with the PDB.
$frames = $c[($i-1)..($c.Length-1)] | Select-String '(\w+\.(dll|exe)) 0001:([0-9A-F]{8})' | Select -First 15
foreach ($f in $frames) {
  $m = $f.Matches[0]; $mod = "C:\LC-BR2014\Bin\" + $m.Groups[1].Value
  & C:\LCSource-deps\addr2line.exe $mod $m.Groups[3].Value
}
EOF
  ;;
esac
