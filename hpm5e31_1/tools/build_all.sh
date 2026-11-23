#!/usr/bin/env bash
# 编译仓库内全部例程（跳过 _ 开头目录与没有 CMakeLists.txt 的目录）
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../scripts/env.sh"
CONFIG=${1:-debug}
fail=0; ok=0
for d in $(find "$REPO_ROOT/samples" -mindepth 2 -maxdepth 2 -type d | sort); do
  base=$(basename "$d")
  case "$base" in _*) continue;; esac
  [ -f "$d/CMakeLists.txt" ] || continue
  out=$(bash "$REPO_ROOT/scripts/build.sh" "$d" -c "$CONFIG" 2>&1 | tail -3)
  if echo "$out" | grep -q 'ELF:'; then ok=$((ok+1)); echo "OK   ${d#$REPO_ROOT/}";
  else fail=$((fail+1)); echo "FAIL ${d#$REPO_ROOT/}"; echo "$out" | head -8; fi
done
echo "---- build_all: ok=$ok fail=$fail ----"
[ "$fail" = 0 ]
