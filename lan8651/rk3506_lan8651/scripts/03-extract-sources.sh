#!/bin/bash
# 解压 SDK 源码包与交叉编译工具链到英文路径（默认 /mnt/sda3/rk3506/work）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/lib/common.sh"

mkdir -p "$WORK_DIR" "$TOOLCHAIN_DIR" "$LOG_DIR"

if [ -d "$SDK_DIR" ] && [ -e "$SDK_DIR/build.sh" ]; then
  log "SDK 已存在，跳过解压：$SDK_DIR"
else
  [ -f "$SDK_TARBALL" ] || die "找不到 SDK 压缩包：$SDK_TARBALL"
  log "解压 SDK（约 10 GB 压缩 / 17 GB 展开，请耐心等待）..."
  tar -xzf "$SDK_TARBALL" -C "$WORK_DIR"
  log "SDK 解压完成：$SDK_DIR"
fi

if [ -x "$TOOLCHAIN_DIR/bin/arm-none-linux-gnueabihf-gcc" ]; then
  log "工具链已存在，跳过解压：$TOOLCHAIN_DIR"
else
  [ -f "$TOOLCHAIN_TARBALL" ] || die "找不到工具链压缩包：$TOOLCHAIN_TARBALL"
  log "解压交叉编译工具链..."
  tar -xf "$TOOLCHAIN_TARBALL" -C "$(dirname "$TOOLCHAIN_DIR")"
  log "工具链解压完成：$TOOLCHAIN_DIR"
fi

log "校验工具链："
"$TOOLCHAIN_DIR/bin/arm-none-linux-gnueabihf-gcc" -v 2>&1 | tail -1
log "SDK 版本 manifest："
head -20 "$SDK_DIR/.repo/manifest.xml" 2>/dev/null | grep -i include || true
