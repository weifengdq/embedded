#!/bin/bash
# 交叉编译 ethtool 6.11（含 PLCA netlink 命令）到 RK3506 (arm32)
#
# 板端 buildroot 自带 ethtool 6.6，**不支持** --get-plca-cfg/--set-plca-cfg
# （PLCA 支持从 ethtool 6.9 开始）。本脚本用 buildroot 的 arm 工具链 +
# buildroot staging 里的 libmnl 交叉编译 ethtool 6.11，产物可直接放到板端 /userdata。
#
# 依赖：$WORK_DIR/ethtool-6.11.tar.gz（可用 ETHTOOL_TARBALL 覆盖；缺失时尝试下载）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

VER="${ETHTOOL_VER:-6.11}"
TB="${ETHTOOL_TARBALL:-$WORK_DIR/ethtool-$VER.tar.gz}"
SRC="${ETHTOOL_SRC:-$WORK_DIR/ethtool-$VER}"
BR="$SDK_DIR/buildroot/output/rockchip_rk3506"
HOST_BIN="$BR/host/bin"
STAGING="$BR/staging"

[ -x "$HOST_BIN/arm-buildroot-linux-gnueabihf-gcc" ] || die "找不到 buildroot 交叉工具链：$HOST_BIN"

if [ ! -f "$TB" ]; then
  log "下载 ethtool-$VER ..."
  ( export https_proxy="${https_proxy:-http://10.11.1.221:7890}"
    export http_proxy="$https_proxy"
    curl -fL --retry 3 -o "$TB" \
      "https://www.kernel.org/pub/software/network/ethtool/ethtool-$VER.tar.gz" )
fi

if [ ! -d "$SRC" ]; then
  log "解压 $TB"
  tar xzf "$TB" -C "$WORK_DIR"
fi

cd "$SRC"
export PATH="$HOST_BIN:$PATH"
export PKG_CONFIG_PATH="$STAGING/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$STAGING"

log "configure (arm-buildroot-linux-gnueabihf)"
./configure --host=arm-buildroot-linux-gnueabihf >"$WORK_DIR/ethtool-configure.log" 2>&1 \
  || { tail -30 "$WORK_DIR/ethtool-configure.log"; die "configure 失败"; }

log "make -j$(nproc)"
make -j"$(nproc)" >"$WORK_DIR/ethtool-build.log" 2>&1 \
  || { tail -30 "$WORK_DIR/ethtool-build.log"; die "make 失败"; }

file "$SRC/ethtool"
log "产物：$SRC/ethtool"
log "部署：scp $SRC/ethtool root@<board-ip>:/userdata/ && chmod +x /userdata/ethtool"
