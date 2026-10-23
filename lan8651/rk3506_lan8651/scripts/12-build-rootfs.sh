#!/bin/bash
# 编译 Buildroot 根文件系统（生成 output/buildroot/images/rootfs.ext4）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"
run_build rootfs rootfs
find "$SDK_DIR/output" -name 'rootfs*' -maxdepth 4 2>/dev/null | head || true

# ★ 关键自检：内核模块是否随 rootfs 安装（缺失会导致板端无 eth0/USB）
MODDIR="$SDK_DIR/buildroot/output/rockchip_rk3506/target/lib/modules"
if [ -d "$MODDIR" ] && ls "$MODDIR" | grep -qE '^[0-9]'; then
  n=$(find "$MODDIR" -name '*.ko' | wc -l)
  log "内核模块已安装：$MODDIR（$n 个 .ko）"
else
  warn "未在 $MODDIR 发现内核模块！板端将缺少 eth0/USB 等驱动。"
  warn "请确认已运行 scripts/06-fix-root-build.sh（chown root:root \$SDK_DIR），见 docs/07 2.2.1。"
fi
