#!/bin/bash
# CPU and GPU use of the running macOS client over a few seconds, without sudo:
#   scripts/macos/perf.sh [seconds]       (default 10)
# CPU: % of one core (top). GPU: share of GPU time from the per-process counters of the GPU driver
# (AGXDeviceUserClient "accumulatedGPUTime", nanoseconds), as Activity Monitor's GPU column.
SECS=${1:-10}
PID=$(pgrep -f "Bin/Nksp" | head -1)
[[ -n $PID ]] || { echo "Nksp is not running" >&2; exit 1; }
gputime() {
  ioreg -r -c AGXDeviceUserClient -l -w 0 | awk -v pid="$PID" '
    /\+-o AGXDeviceUserClient/ { if (mine) total += sum; sum = 0; mine = 0 }
    /"accumulatedGPUTime"=/ { n = split($0, a, /"accumulatedGPUTime"=/); for (i = 2; i <= n; i++) sum += a[i] + 0 }
    $0 ~ "\"IOUserClientCreator\" = \"pid " pid "," { mine = 1 }
    END { if (mine) total += sum; printf "%.0f\n", total }'
}
G0=$(gputime); T0=$(date +%s)
CPU=$(top -l $((SECS + 1)) -s 1 -pid "$PID" -stats cpu | grep -E '^ *[0-9.]+ *$' | tail -n "$SECS" | awk '{ s += $1 } END { printf "%.1f", s / NR }')
G1=$(gputime); T1=$(date +%s)
awk -v g0="$G0" -v g1="$G1" -v dt="$((T1 - T0))" -v cpu="$CPU" -v pid="$PID" \
  'BEGIN { printf "Nksp %s: CPU %s%% of a core, GPU %.1f%% (over %d s)\n", pid, cpu, (g1 - g0) / (dt * 1e9) * 100, dt }'
