#!/bin/bash
# 一键整体编译：defconfig -> U-Boot -> Kernel -> rootfs -> 固件打包 -> update.img
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/lib/common.sh"

[ -d "$SDK_DIR" ] || die "SDK 不存在：$SDK_DIR，请先运行 scripts/03-extract-sources.sh"
command -v python2 >/dev/null 2>&1 || die "缺少 python2，请先运行 scripts/02-install-python2.sh"

# 以 root 编译时修复内核模块安装（否则 rootfs 会缺少 /lib/modules/<ver>）
bash "$HERE/06-fix-root-build.sh"

log "选择板级配置：$DEFCONFIG"
( cd "$SDK_DIR" && ./build.sh "$DEFCONFIG" )

run_build uboot  uboot
run_build kernel kernel
run_build rootfs rootfs
run_build firmware firmware
run_build updateimg updateimg

log "全部完成。产物目录：$SDK_DIR/output/firmware"
ls -la "$SDK_DIR/output/firmware" 2>/dev/null || true
