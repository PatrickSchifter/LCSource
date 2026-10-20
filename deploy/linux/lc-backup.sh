#!/bin/bash
# Backs up the game's databases (the local MariaDB of compose.yml) and copies them to the cloud
# database. Run by lc-backup.timer; also by hand.
#
#   lc-backup.sh            dump locally, then load the dump into the cloud database
#   lc-backup.sh --local    only the local dump
#
# The dumps go to /srv/lc/backups/<date>/<db>.sql.gz (the last 3 are kept). The cloud database's
# login is /srv/lc/backup.cnf, mounted in the db container as /backup.cnf (a [client] section with
# host, port, user, password and ssl=1; not in the repository). Loading a dump there replaces its
# tables, so the cloud copy is the state of the last backup.
set -euo pipefail
umask 077   # the dumps hold the accounts' password hashes

DBS=(lc2009_auth lc2009_data lc2009_db newproject_db_auth newproject_data newproject_db lc2018_auth lc2018_data lc2018_db)
DIR=/srv/lc/backups/$(date +%Y%m%d-%H%M)
mkdir -p "$DIR"

for db in "${DBS[@]}"; do
  docker exec lc-db-1 sh -c 'exec mariadb-dump -uroot -p"$MARIADB_ROOT_PASSWORD" --single-transaction --routines --triggers --databases "$1"' sh "$db" \
    | gzip > "$DIR/$db.sql.gz"
done
echo "local dump: $DIR ($(du -sh "$DIR" | cut -f1))"
ls -1d /srv/lc/backups/*/ | head -n -3 | xargs -r rm -rf

[[ ${1:-} == --local ]] && exit 0
for db in "${DBS[@]}"; do
  gunzip -c "$DIR/$db.sql.gz" \
    | docker exec -i lc-db-1 mariadb --defaults-extra-file=/backup.cnf --init-command="SET SESSION sql_mode=''"
  echo "cloud: $db"
done
