#!/bin/bash
# Picks a Last Chaos version, starts its servers and opens its macOS client.
#
#   scripts/macos/launcher.sh            asks which version (a macOS list dialog)
#   scripts/macos/launcher.sh 2018       goes straight to that version (2009, 2015 or 2018)
#   scripts/macos/launcher.sh 2018 nuvem plays on the cloud server instead (deploy/linux)
#   scripts/macos/launcher.sh --stop     stops the servers of every version
#   scripts/macos/launcher.sh --status   shows which servers run
#
# Each version lives in its directory of this repository (HANDOFF.md, "Fase V — versões"):
# v2009/, v2015/ and v2018/, each with its build, game install and servers. The versions use
# different ports, so their servers can run side by side. When a version's LoginServer already runs, its address is kept; otherwise the
# servers start on 127.0.0.1 (LC_PUBLIC_IP overrides it). The install's sl.dta is rewritten to
# the address the LoginServer listens on. Extra environment (LC_GFX_API, LC_SCREEN...) reaches
# the client. On the cloud server (the dialog's "<version> (nuvem)"), nothing starts here and
# the sl.dta points to the cloud host, at the same ports.
#
# LC_PROJECTS: the directory holding the game installs (default ~/Projects).
# LC_CLOUD_HOST: the cloud server (default the Oracle Cloud instance of deploy/linux/README.md).
set -euo pipefail

PROJECTS=${LC_PROJECTS:-$HOME/Projects}
HERE="$(cd "$(dirname "$0")/../.." && pwd)"   # this repository
CLOUD_HOST=${LC_CLOUD_HOST:-136.248.115.250}

# version | directory | server script | client script | game install | LoginServer port | name in sl.dta
VERSIONS=(
  "2015|v2015|scripts/run-server.sh|scripts/macos/play.sh|LC-BR2014|4001|Mac"
  "2009|v2009|scripts/run-server.sh|scripts/client-play.sh|LC-2009|4201|Mac2009"
  "2018|v2018|scripts/run-server.sh|scripts/macos/play.sh|LC-2018|4301|Mac2018"
)

fail() {
  echo "launcher: $*" >&2
  [[ ${GUI:-0} == 1 ]] && osascript -e "display alert \"Last Chaos\" message \"$*\" as critical" >/dev/null 2>&1 || true
  exit 1
}

field() { # version field-number
  local v
  for v in "${VERSIONS[@]}"; do
    [[ ${v%%|*} == "$1" ]] && { cut -d'|' -f"$2" <<<"$v"; return; }
  done
  return 1
}

# Address the LoginServer of a port listens on, if it runs.
listen_addr() {
  lsof -nP -iTCP:"$1" -sTCP:LISTEN 2>/dev/null | awk 'NR > 1 { sub(/:[0-9]+$/, "", $9); print $9; exit }'
}

servers() { # command
  local v ver dir script
  for v in "${VERSIONS[@]}"; do
    IFS='|' read -r ver dir script _ <<<"$v"
    [[ -x $HERE/$dir/$script ]] || continue
    echo "== $ver"
    "$HERE/$dir/$script" "$1"
  done
}

case ${1:-} in
  --stop) servers stop; exit ;;
  --status) servers status; exit ;;
esac

version=${1:-}
cloud=0
[[ ${2:-} == nuvem ]] && cloud=1
if [[ -z $version ]]; then
  choices=$(for v in "${VERSIONS[@]}"; do printf '"%s","%s (nuvem)",' "${v%%|*}" "${v%%|*}"; done)
  version=$(osascript -e "choose from list {${choices%,}} with title \"Last Chaos\" with prompt \"Qual versão jogar?\" default items {\"2015\"}") || true
  [[ -n $version && $version != false ]] || exit 0
  [[ $version == *" (nuvem)" ]] && { cloud=1; version=${version% (nuvem)}; }
  GUI=1   # chosen in the dialog: errors are shown as alerts too
fi
field "$version" 1 >/dev/null || fail "versão desconhecida: $version (2009, 2015 ou 2018)"

dir=$HERE/$(field "$version" 2)
server_script=$dir/$(field "$version" 3)
client_script=$dir/$(field "$version" 4)
game=$PROJECTS/$(field "$version" 5)
port=$(field "$version" 6)
name=$(field "$version" 7)

[[ -d $game/Data ]] || fail "a instalação da $version não existe: $game"

if [[ $cloud == 1 ]]; then
  addr=$CLOUD_HOST
  nc -z -G 5 "$addr" "$port" 2>/dev/null || fail "o servidor da $version na nuvem ($addr:$port) não responde"
  name="Nuvem"
else
  addr=$(listen_addr "$port")
fi
if [[ -z $addr ]]; then
  echo "starting the $version servers"
  LC_PUBLIC_IP=${LC_PUBLIC_IP:-127.0.0.1} "$server_script" start || fail "os servidores da $version não subiram (veja os stdout.log em run/)"
  for _ in {1..30}; do
    addr=$(listen_addr "$port")
    [[ -n $addr ]] && break
    sleep 1
  done
  [[ -n $addr ]] || fail "o LoginServer da $version não abriu a porta $port"
fi
echo "$version LoginServer on $addr:$port"

line="$name $addr $port 1500 1000"
if [[ $(python3 -I "$HERE/scripts/sl-dta.py" read "$game/sl.dta" 2>/dev/null) != "$line" ]]; then
  [[ -f $game/sl.dta && ! -f $game/sl.dta.original ]] && cp "$game/sl.dta" "$game/sl.dta.original"
  python3 -I "$HERE/scripts/sl-dta.py" write "$game/sl.dta" "$line" >/dev/null
fi

LC_GAME_DIR=$game exec "$client_script"
