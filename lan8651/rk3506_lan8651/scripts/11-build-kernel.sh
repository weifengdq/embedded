#!/bin/bash
# 编译 Kernel（生成 kernel/zboot.img 与 arch/arm/boot/zImage）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"
run_build kernel kernel
ls -la "$SDK_DIR/kernel/zboot.img" "$SDK_DIR/kernel/arch/arm/boot/zImage" 2>/dev/null || true
