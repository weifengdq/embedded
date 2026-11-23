#!/usr/bin/env bash
# run_variant.sh - 以任意 CMake 变量组合构建并上板，然后把日志归档
#
# 用途: bench 目录下的基准需要"同一份代码、不同编译/内存布局"多次上板，
#       而 scripts/build.sh 只支持一个固定配置。本脚本在临时 build 目录里
#       用额外 -D 变量配置，构建完把 elf 复制到 build/<name>-debug/output/
#       再用 scripts/hwrun.sh --no-build 上板（仍然走整机 flock 串行）。
#
# 用法:
#   bash samples/bench/_bench_tools/run_variant.sh <sample相对路径> <变体名> "<额外 cmake -D 参数>" -- <hwrun 参数...>
# 例:
#   bash samples/bench/_bench_tools/run_variant.sh bench/bench_coremark o2 '-DBENCH_OPT=-O2' -- --secs 48
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
source "$REPO_ROOT/scripts/env.sh"

SAMPLE="$1"; VARIANT="$2"; CMAKE_ARGS="$3"; shift 3
[ "${1:-}" = "--" ] && shift
HWRUN_ARGS=("$@")

SRC="$REPO_ROOT/samples/$SAMPLE"
NAME="$(basename "$SRC")"
BD="$REPO_ROOT/build/$NAME-$VARIANT"

if [ ! -f "$BD/CMakeCache.txt" ]; then
  # shellcheck disable=SC2086
  cmake -GNinja -B "$BD" -S "$SRC" \
    -DBOARD="$BOARD" -DBOARD_SEARCH_PATH="$BOARD_SEARCH_PATH" \
    -DHPM_BUILD_TYPE=flash_xip -DCMAKE_BUILD_TYPE=debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CMAKE_ARGS
fi
cmake --build "$BD" 2>&1 | tail -3

# hwrun.sh --no-build 固定从 build/<name>-debug/output 取 elf
mkdir -p "$REPO_ROOT/build/$NAME-debug/output"
cp -f "$BD"/output/*.elf "$REPO_ROOT/build/$NAME-debug/output/"

echo "== variant $VARIANT: $CMAKE_ARGS =="
bash "$REPO_ROOT/scripts/hwrun.sh" "$SAMPLE" --no-build "${HWRUN_ARGS[@]}"
