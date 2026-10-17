#!/bin/bash
# Syncs the sources to Windows, builds the given ninja targets there and summarizes the errors.
#   scripts/windows/build.sh Engine Shaders EntitiesMP GameMP Nksp
# Exit status is non-zero when the build has errors. Full log: C:\LCSource-build.log
set -e
D=$(dirname "$0")
"$D/sync.sh" > /dev/null
"$D/wps.sh" <<PS | tee /dev/stderr | grep -q '^erros: 0 '
cmd /c "C:\LCSource-deps\build.cmd ${*:-Engine Shaders EntitiesMP GameMP Nksp}" 2>&1 | Select-Object -Last 1
\$errs = Select-String -Path C:\LCSource-build.log -Pattern 'error (C|LNK)\d{4}' | % { \$_.Line }
"erros: " + \$errs.Count + " / arquivos com falha: " + (Select-String -Path C:\LCSource-build.log -Pattern '^FAILED').Count
\$errs | Sort-Object -Unique | Select -First 25 | % { \$_ -replace '^C:\\\\LCSource\\\\Client Source\\\\','' }
PS
