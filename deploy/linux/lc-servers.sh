#!/bin/bash
# Runs one version's servers in the foreground, as the container's main process (the Linux
# counterpart of each version's scripts/run-server.sh).
#
#   lc-servers.sh v2009|v2015|v2018
#
# /srv/lc/<version> (a volume from the host) holds server-config/ (the version's, as in the
# repository) and run/ (one directory per server; run/GameServer/data has the zone maps). Each
# server's newStobm.bin is rendered from server-config/<Name>.ini: @PUBLIC_IP@ becomes
# LC_PUBLIC_IP, and in every "[... DB]" section IP, User and Password become LC_DB_HOST,
# LC_DB_USER and LC_DB_PASSWORD. The servers connect with port 0, which the MariaDB client takes
# from MYSQL_TCP_PORT (LC_DB_PORT). When one server exits, the others are stopped and the script
# exits, so Docker restarts the version as a whole.
set -euo pipefail

V=${1:?usage: lc-servers.sh v2009|v2015|v2018}
BIN=/opt/lc/$V/bin
ROOT=/srv/lc/$V
CONFIG=$ROOT/server-config
RUN=$ROOT/run
: "${LC_PUBLIC_IP:?}" "${LC_DB_HOST:?}" "${LC_DB_USER:?}" "${LC_DB_PASSWORD:?}"
export MYSQL_TCP_PORT=${LC_DB_PORT:-3306}

case $V in
  v2009) ALL=(Messenger Connector Helper GameServer LoginServer) ;;
  *) ALL=(Messenger Connector Helper SubHelper GameServer LoginServer) ;;
esac

render() { # ini -> newStobm.bin
  awk '
    /^\[/ { db = ($0 ~ / DB\]/) }
    db && /^IP=/ { print "IP=" ENVIRON["LC_DB_HOST"]; next }
    db && /^User=/ { print "User=" ENVIRON["LC_DB_USER"]; next }
    db && /^Password=/ { print "Password=" ENVIRON["LC_DB_PASSWORD"]; next }
    { gsub(/@PUBLIC_IP@/, ENVIRON["LC_PUBLIC_IP"]); print }
  ' "$1"
}

prepare() {
  local name=$1 dir=$RUN/$1 ini=$RUN/$1/newStobm.bin
  mkdir -p "$dir" "$RUN/LogFiles"
  [[ -f $CONFIG/log.xml ]] && cp "$CONFIG/log.xml" "$RUN/log.xml"
  case $name in
    GameServer)
      mkdir -p "$dir/data"
      ini=$dir/data/newStobm.bin
      [[ -f $CONFIG/GameServer-event.ini ]] && cp "$CONFIG/GameServer-event.ini" "$dir/data/event.ini" ;;
    LoginServer)
      [[ $V == v2009 ]] && touch "$dir/login.open" ;;
  esac
  render "$CONFIG/$name.ini" > "$ini"
}

declare -A PIDS
stop_all() {
  local i
  for ((i = ${#ALL[@]} - 1; i >= 0; i--)); do
    [[ -n ${PIDS[${ALL[i]}]:-} ]] && kill "${PIDS[${ALL[i]}]}" 2>/dev/null || true
  done
  wait || true
}
trap 'echo "$V: stopping"; stop_all; exit 0' TERM INT

for name in "${ALL[@]}"; do
  prepare "$name"
  (cd "$RUN/$name" && exec "$BIN/$name" > stdout.log 2>&1 < /dev/null) &
  PIDS[$name]=$!
  sleep $([[ $name == GameServer ]] && echo 8 || echo 2)
  if ! kill -0 "${PIDS[$name]}" 2>/dev/null; then
    echo "$V: $name exited during startup; last lines of run/$name/stdout.log:" >&2
    tail -n 15 "$RUN/$name/stdout.log" >&2
    stop_all
    exit 1
  fi
  echo "$V: $name started (pid ${PIDS[$name]})"
done

wait -n || true
echo "$V: a server exited; stopping the others" >&2
for name in "${ALL[@]}"; do
  kill -0 "${PIDS[$name]}" 2>/dev/null || { echo "$V: $name is down; last lines:" >&2; tail -n 15 "$RUN/$name/stdout.log" >&2; }
done
stop_all
exit 1
