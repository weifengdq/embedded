#!/bin/bash
# 修复“以 root 编译 SDK”时的问题（本机环境特有，见 docs/07-troubleshooting.md 第 3 节）
#
# 背景：
#   Rockchip SDK 会按 SDK 目录属主决定 RK_OWNER_UID；若 SDK 目录属主是普通用户(如 1000)
#   而我们以 root 编译，则内核模块构建会用 `sudo -u #1000 make modules_install`，
#   而 buildroot 的 target 目录是 root 属主 → mkdir: Permission denied →
#   **内核模块静默未安装**（导致板端没有 eth0、USB 等驱动）。
#
# 处理：
#   1) 把 SDK 顶层目录改为 root 属主，使 RK_OWNER_UID=0（不再降权），模块可正常安装；
#   2) 打补丁使 buildroot 的 post-build-fast-display.sh 幂等（重复构建不再报错）。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

[ -d "$SDK_DIR" ] || die "SDK 不存在：$SDK_DIR"

# 1) SDK 顶层属主 -> root（仅在以 root 编译时）
if [ "$(id -u)" = "0" ]; then
  cur_uid="$(stat --format %u "$SDK_DIR")"
  if [ "$cur_uid" != "0" ]; then
    log "将 $SDK_DIR 属主改为 root:root（原 uid=$cur_uid）以修复内核模块安装"
    chown root:root "$SDK_DIR"
  else
    log "SDK 顶层已是 root 属主，跳过"
  fi
fi

# 2) 幂等补丁
PATCH_SRC="$REPO_DIR/patches/0001-buildroot-rk3506-post-build-fast-display-idempotent.patch"
TARGET_SH="$SDK_DIR/buildroot/board/rockchip/rk3506/post-build-fast-display.sh"
if [ -f "$PATCH_SRC" ] && [ -f "$TARGET_SH" ]; then
  if grep -q "rk3506_1 patch" "$TARGET_SH"; then
    log "post-build-fast-display.sh 已打补丁，跳过"
  else
    log "为 post-build-fast-display.sh 打幂等补丁"
    ( cd "$SDK_DIR" && patch -p1 < "$PATCH_SRC" ) || die "打补丁失败"
  fi
fi

log "完成。之后可正常执行 ./build.sh（模块会随 rootfs 一起安装）。"
