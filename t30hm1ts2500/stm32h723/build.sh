#!/usr/bin/env bash
# 编译 STM32H723ZGT6 + T30HM1TS2500 固件
# 依赖: arm-none-eabi-gcc >= 10, cmake >= 3.20, ninja (或 make)
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-Release}

if command -v ninja >/dev/null 2>&1; then
  GEN="Ninja"
else
  GEN="Unix Makefiles"
fi

cmake -S . -B "${BUILD_DIR}" -G "${GEN}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo
echo "==== 产物 ===="
ls -l "${BUILD_DIR}/stm32h723_s2500.elf" "${BUILD_DIR}/stm32h723_s2500.hex" "${BUILD_DIR}/stm32h723_s2500.bin"
