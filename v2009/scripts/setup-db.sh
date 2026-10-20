#!/bin/bash
# Builds the databases for the 2009 servers from the Feb/2016 official dump.
#
#   v2009/scripts/setup-db.sh [--reset]
#
# Creates lc2009_data, lc2009_db and lc2009_auth (never touches the newproject_* databases of the
# 2015 port), loads newproject_{data,db,db_auth}.sql (the *-unused.sql dumps first: the 2009 code
# still uses several of those tables), grants the lcserver user and applies
# v2009/server-config/db-fixes.sql, then the regen position fixes (zone0-oldjuno.sql,
# zone0-mobs-map.sql, zone0-town-mapdta.sql, zone-maps-regen.sql). Without --reset an existing lc2009_* database is kept and only the fixes run.
#
# LC_DB_DUMPS points at the dump checkout (default: ~/Projects/lastchaos-database).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DUMPS=${LC_DB_DUMPS:-$HOME/Projects/lastchaos-database}
MYSQL=(mysql --default-character-set=utf8)

load() { # db dump...
  local db=$1; shift
  if [[ ${RESET:-0} == 0 ]] && "${MYSQL[@]}" -N -e "SELECT 1 FROM information_schema.schemata WHERE schema_name='$db'" | grep -q 1; then
    echo "$db exists (use --reset to reload)"
    return
  fi
  echo "loading $db"
  "${MYSQL[@]}" -e "DROP DATABASE IF EXISTS \`$db\`; CREATE DATABASE \`$db\` DEFAULT CHARACTER SET utf8"
  for f in "$@"; do
    echo "  $f"
    "${MYSQL[@]}" "$db" <"$DUMPS/$f"
  done
}

[[ ${1:-} == --reset ]] && RESET=1

load lc2009_data newproject_data-unused.sql newproject_data.sql
load lc2009_db   newproject_db-unused.sql newproject_db.sql
load lc2009_auth newproject_db_auth-unused.sql newproject_db_auth.sql

for host in localhost 127.0.0.1; do
  for db in lc2009_data lc2009_db lc2009_auth; do
    "${MYSQL[@]}" -e "GRANT ALL PRIVILEGES ON \`$db\`.* TO 'lcserver'@'$host'"
  done
done

echo "applying db-fixes.sql"
"${MYSQL[@]}" <"$ROOT/server-config/db-fixes.sql"

# NPC regen / shop positions fitted to the zone maps the 2009 server reads (written by
# v2009/scripts/fix-regen-positions.py): zone 0 is the old Juno, the others are small fixes for
# maps converted from the 2015 port. zone0-mobs-map.sql (v2009/scripts/juno-from-map.py) then lays
# out the zone 0 monster spawns after the classic Juno map, and zone0-town-mapdta.sql
# (v2009/scripts/juno-town-from-mapdta.py) puts the town NPCs where the classic client's Map.dta shows them;
# zone0-town-removed.sql disables the zone 0 NPCs the classic game doesn't have; zone0-town-manual.sql has
# the positions set by hand in game (/whereami).
for f in zone0-oldjuno.sql zone0-mobs-map.sql zone0-town-mapdta.sql zone0-town-removed.sql zone0-town-manual.sql zone-maps-regen.sql; do
  echo "applying $f"
  "${MYSQL[@]}" <"$ROOT/server-config/$f"
done
echo done
