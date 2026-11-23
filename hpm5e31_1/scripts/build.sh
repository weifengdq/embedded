#!/usr/bin/env bash
# 编译一个示例: scripts/build.sh <sample 目录名或路径> [-c release]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/env.sh"
[ $# -ge 1 ] || { echo "usage: $0 <sample> [-c debug|release]"; exit 1; }
SAMPLE="$1"; shift
CONFIG=debug
while [ $# -gt 0 ]; do case "$1" in -c) CONFIG="$2"; shift 2;; *) shift;; esac; done
if [ -d "$SAMPLE" ]; then SRC="$(cd "$SAMPLE" && pwd)"; elif [ -d "$REPO_ROOT/samples/$SAMPLE" ]; then SRC="$REPO_ROOT/samples/$SAMPLE"; else
  SRC="$(find "$REPO_ROOT/samples" -maxdepth 3 -type d -name "$SAMPLE" | head -1)"
fi
[ -n "${SRC:-}" ] && [ -d "$SRC" ] || { echo "sample not found: $SAMPLE"; ls "$REPO_ROOT/samples"; exit 2; }
NAME="$(basename "$SRC")"
BD="$REPO_ROOT/build/$NAME-$CONFIG"
if [ ! -f "$BD/CMakeCache.txt" ]; then
  cmake -GNinja -B "$BD" -S "$SRC" \
    -DBOARD="$BOARD" -DBOARD_SEARCH_PATH="$BOARD_SEARCH_PATH" \
    -DHPM_BUILD_TYPE=flash_xip -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi
cmake --build "$BD"
ELF="$(ls "$BD"/output/*.elf 2>/dev/null | head -1)"
echo "ELF: $ELF"
