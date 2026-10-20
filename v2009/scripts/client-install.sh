#!/bin/bash
# Builds the 2009 client's install as an overlay, without copying the 10+ GB of assets:
#   ~/Projects/LC-2009          (LC_GAME_DIR)
#     Bin/                      our binaries (client-play.sh copies them)
#     Data/                     a symlink per entry of the BR 2014 Data/, except
#       Etc/                    a copy (the game writes ps.dat there)
#       <files of LC-2009-gen>  the data files regenerated in the 2009 format
#       World/                  a link per world, from LC-2009-gen/Data/World when it has one
#     everything else           symlinks into the BR 2014 install, except the files the game
#                               writes or that differ per install (sl.dta, vtm.brn: copies)
# Run it again after LC-2009-gen/Data changes; the copies already there are kept.
#   v2009/scripts/client-install.sh
set -e
SRC=${LC_BR2014_DIR:-$HOME/Projects/LC-BR2014}
GEN=${LC_2009_GEN_DIR:-$HOME/Projects/LC-2009-gen/Data}
DST=${LC_GAME_DIR:-$HOME/Projects/LC-2009}
[[ -d $SRC/Data ]] || { echo "no BR 2014 install in $SRC" >&2; exit 1; }
mkdir -p "$DST/Bin" "$DST/Data"
lower() { tr '[:upper:]' '[:lower:]' <<< "$1"; }

# top level
for f in "$SRC"/*; do
  n=$(basename "$f")
  case $(lower "$n") in
    bin|data|*.log|errorlog.txt|throwlog.txt|asan.*|ubsan.*|hw-shaders.txt|jogar.bat) continue;;
    sl.dta|sl.dta.*|vtm.brn|lccnct.dta|luncher.dat|update.txt|modext.txt)
      [[ -e $DST/$n ]] || cp -p "$f" "$DST/$n";;
    *) ln -sfn "$f" "$DST/$n";;
  esac
done

# Data: the regenerated files first, then the rest by name (the file system ignores case)
genlist=" "
if [[ -d $GEN ]]; then
  for f in "$GEN"/*; do
    n=$(basename "$f")
    [[ $(lower "$n") == world ]] && continue   # merged world by world below
    genlist="$genlist$(lower "$n") "
    rm -f "$DST/Data/$n"; ln -s "$f" "$DST/Data/$n"
  done
fi
for f in "$SRC"/Data/*; do
  n=$(basename "$f"); l=$(lower "$n")
  [[ $genlist == *" $l "* ]] && continue
  if [[ $l == etc ]]; then
    [[ -d $DST/Data/$n && ! -L $DST/Data/$n ]] || { rm -f "$DST/Data/$n"; cp -Rp "$f" "$DST/Data/$n"; }
  elif [[ $l == world ]]; then
    # a link per world, except the worlds in $GEN/World (the classic LoginWorld the 2009 code's
    # entity numbers belong to)
    [[ -L $DST/Data/$n ]] && rm -f "$DST/Data/$n"
    mkdir -p "$DST/Data/$n"
    for w in "$f"/*; do
      wn=$(basename "$w")
      if [[ -e $GEN/World/$wn ]]; then ln -sfn "$GEN/World/$wn" "$DST/Data/$n/$wn"
      else ln -sfn "$w" "$DST/Data/$n/$wn"; fi
    done
  else
    ln -sfn "$f" "$DST/Data/$n"
  fi
done
echo "install: $DST (assets from $SRC, regenerated data from $GEN)"
