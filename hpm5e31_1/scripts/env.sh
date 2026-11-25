#!/usr/bin/env bash
# hpm5e31_1 构建环境。用法: source scripts/env.sh
export REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export HPM_SDK_BASE="${HPM_SDK_BASE:-$HOME/sdk_env_v1.13.0/hpm_sdk}"
export GNURISCV_TOOLCHAIN_PATH="${GNURISCV_TOOLCHAIN_PATH:-$HOME/toolchains/riscv32-unknown-elf-newlib-multilib}"
export HPM_SDK_TOOLCHAIN_VARIANT=gcc
export BOARD="${BOARD:-hpm5e31_1}"
export BOARD_SEARCH_PATH="${BOARD_SEARCH_PATH:-$REPO_ROOT/boards}"
# 板载调试串口是 CH343(/dev/ttyACM1)；/dev/ttyACM0 是 J-Link 的 VCOM（R8 曾把两者写反）
export SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM1}"
export SERIAL_BAUD="${SERIAL_BAUD:-921600}"
export JLINK_DEVICE="${JLINK_DEVICE:-HPM5E31xGNx}"
export JLINK_SPEED="${JLINK_SPEED:-4000}"
export HPM_HW_LOCK="${HPM_HW_LOCK:-/tmp/hpm5e31_hw.lock}"
export PATH="$GNURISCV_TOOLCHAIN_PATH/bin:$PATH"
