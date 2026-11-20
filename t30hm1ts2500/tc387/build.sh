#!/usr/bin/env bash
# TC387 + T30HM1TS2500 构建脚本
set -e
cd "$(dirname "$0")"

cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/tricore-gcc-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo
echo "产物:"
ls -l build/tc387_s2500 build/tc387_s2500.hex
