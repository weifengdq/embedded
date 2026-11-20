#!/usr/bin/env bash
# 编译 HPM5361 + T30HM1TS2500 固件
# 依赖: hpm_sdk v1.13.0, riscv32-unknown-elf 工具链, cmake >= 3.20, ninja
set -euo pipefail
cd "$(dirname "$0")"

HPM_SDK_BASE=${HPM_SDK_BASE:-${HOME}/hpm5e31/sdk_env_v1.13.0/hpm_sdk}
GNURISCV_TOOLCHAIN_PATH=${GNURISCV_TOOLCHAIN_PATH:-${HOME}/hpm5e31/toolchains/riscv32-unknown-elf-newlib-multilib}
export HPM_SDK_BASE GNURISCV_TOOLCHAIN_PATH
export PATH="${GNURISCV_TOOLCHAIN_PATH}/bin:${PATH}"

BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-release}

if [ ! -d "${HPM_SDK_BASE}" ]; then
    echo "找不到 HPM_SDK_BASE=${HPM_SDK_BASE}" >&2
    exit 1
fi

if command -v ninja >/dev/null 2>&1; then
    GEN="Ninja"
else
    GEN="Unix Makefiles"
fi

cmake -S . -B "${BUILD_DIR}" -G "${GEN}" \
      -DBOARD=hpm5300evk \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DHPM_BUILD_TYPE=flash_xip

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo
echo "==== 产物 ===="
ls -l "${BUILD_DIR}/output/demo.elf"
