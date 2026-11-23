#!/usr/bin/env bash
# 构建主机侧 cannelloni（cannelloni v2.1.2，CAN over UDP/TCP 隧道）
#
# 源码：ref2/cannelloni（GPL-2.0，Lead 已 clone）
# 产物：host/can_bridge/build/cannelloni（build/ 不入 git）
# 依赖：cmake、ninja-build、g++（C++20）、pthread；SCTP 关闭（-DSCTP_SUPPORT=OFF）
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${CANNELLONI_SRC:-ref2/cannelloni}"
BUILD="${CANNELLONI_BUILD:-$here/build}"

if [ ! -d "$SRC" ]; then
  echo "ERROR: cannelloni 源码不在 $SRC（可用 CANNELLONI_SRC=... 指定）" >&2
  echo "       参考：git clone https://github.com/mguentner/cannelloni.git" >&2
  exit 2
fi
command -v cmake >/dev/null || { echo "ERROR: 需要 cmake" >&2; exit 2; }
command -v ninja >/dev/null || { echo "WARN: 没装 ninja，回退到 make" >&2; }

GEN=ninja
command -v ninja >/dev/null || GEN="Unix Makefiles"

echo "[1/2] cmake configure: $SRC -> $BUILD (SCTP_SUPPORT=OFF)"
cmake -G"$GEN" -S "$SRC" -B "$BUILD" \
      -DSCTP_SUPPORT=OFF -DCMAKE_BUILD_TYPE=Release

echo "[2/2] build"
cmake --build "$BUILD" -j"$(nproc)"

if [ -x "$BUILD/cannelloni" ]; then
  "$BUILD/cannelloni" -h 2>&1 | head -2
  echo "OK: $BUILD/cannelloni"
else
  echo "FAIL: 未生成 $BUILD/cannelloni" >&2
  exit 1
fi
