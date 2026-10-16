#!/bin/bash
# Runs the Last Chaos servers locally.
#
#   scripts/run-server.sh start [names...]   start servers (default: all, in dependency order)
#   scripts/run-server.sh stop  [names...]   stop servers (reverse order)
#   scripts/run-server.sh status             show what is running
#   scripts/run-server.sh log <name>         follow a server's console output
#
# Each server runs from run/<Name>/ with newStobm.bin copied from server-config/<Name>.ini.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="$ROOT/build/Server Source"
RUN_DIR="$ROOT/run"
CONFIG_DIR="$ROOT/server-config"
ALL=(Messenger Connector Helper SubHelper GameServer LoginServer)

pid_of() {
  local pidfile="$RUN_DIR/$1/pid"
  [[ -f "$pidfile" ]] && kill -0 "$(cat "$pidfile")" 2>/dev/null && cat "$pidfile"
}

prepare() {
  local name=$1 dir="$RUN_DIR/$1"
  mkdir -p "$dir" "$RUN_DIR/LogFiles"
  cp "$CONFIG_DIR/log.xml" "$RUN_DIR/log.xml"
  if [[ $name == GameServer ]]; then
    mkdir -p "$dir/data"
    cp "$CONFIG_DIR/$name.ini" "$dir/data/newStobm.bin"
    cp "$CONFIG_DIR/GameServer-event.ini" "$dir/data/event.ini"
  else
    cp "$CONFIG_DIR/$name.ini" "$dir/newStobm.bin"
  fi
}

start() {
  local name=$1
  if pid=$(pid_of "$name"); then
    echo "$name already running (pid $pid)"
    return
  fi
  [[ -x "$BIN_DIR/$name" ]] || { echo "$name: not built ($BIN_DIR/$name)" >&2; return 1; }
  [[ -f "$CONFIG_DIR/$name.ini" ]] || { echo "$name: missing $CONFIG_DIR/$name.ini" >&2; return 1; }
  prepare "$name"
  (cd "$RUN_DIR/$name" && exec "$BIN_DIR/$name" >stdout.log 2>&1 </dev/null) &
  echo $! >"$RUN_DIR/$name/pid"
  sleep 2
  if pid=$(pid_of "$name"); then
    echo "$name started (pid $pid)"
  else
    echo "$name exited during startup; last lines of run/$name/stdout.log:" >&2
    tail -n 15 "$RUN_DIR/$name/stdout.log" >&2
    return 1
  fi
}

stop() {
  local name=$1
  if pid=$(pid_of "$name"); then
    kill "$pid"
    for _ in {1..20}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.25; done
    kill -0 "$pid" 2>/dev/null && kill -9 "$pid"
    echo "$name stopped"
  fi
  rm -f "$RUN_DIR/$name/pid"
}

cmd=${1:-status}
shift || true
names=("$@")
[[ ${#names[@]} -eq 0 ]] && names=("${ALL[@]}")

case $cmd in
  start)
    for n in "${names[@]}"; do start "$n" || exit 1; done ;;
  stop)
    for ((i = ${#names[@]} - 1; i >= 0; i--)); do stop "${names[i]}"; done ;;
  status)
    for n in "${names[@]}"; do
      if pid=$(pid_of "$n"); then echo "$n: running (pid $pid)"; else echo "$n: stopped"; fi
    done ;;
  log)
    exec tail -f "$RUN_DIR/${names[0]}/stdout.log" ;;
  *)
    echo "usage: $0 {start|stop|status|log} [names...]" >&2; exit 2 ;;
esac
