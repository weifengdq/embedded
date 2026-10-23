#!/bin/bash
# 编译 U-Boot（生成 uboot.img / MiniLoaderAll.bin）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"
run_build uboot uboot
ls -la "$SDK_DIR/u-boot/uboot.img" "$SDK_DIR/u-boot/"*idblock*.img 2>/dev/null || true
