#!/bin/bash
# T30HM1TS2500 (S2500) out-of-tree 驱动: 编译 / 部署 / DTS 补丁
#
# 用法:
#   bash scripts/18-install-s2500.sh build
#   bash scripts/18-install-s2500.sh deploy <board-ip> [insmod 参数...]
#   bash scripts/18-install-s2500.sh dts
#   bash scripts/18-install-s2500.sh clean
#
# 环境变量:
#   SDK_DIR         RK3506 Linux SDK 根目录 (build/dts 时需要)
#   KDIR            内核构建树, 默认 $SDK_DIR/kernel
#   ARCH            默认 arm
#   CROSS_COMPILE   默认 arm-none-linux-gnueabihf-
#
# 说明: 驱动是 out-of-tree 模块, 不修改内核源码, 不覆盖 boot 分区。
set -euo pipefail

ROOT="$(cd "$(dirname "$BASH_SOURCE")/.." && pwd)"
DRV="$ROOT/drivers/s2500"
SDK_DIR="${SDK_DIR:-}"
KDIR="${KDIR:-$SDK_DIR/kernel}"
ARCH="${ARCH:-arm}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-none-linux-gnueabihf-}"
PARAMS="${PARAMS:-plca_en=1 plca_id=5 plca_ncnt=8 plca_totmr=32 spi_hz=25000000}"

log() { printf '\033[36m[s2500] %s\033[0m\n' "$*"; }
die() { printf '\033[31m[s2500][ERROR] %s\033[0m\n' "$*" >&2; exit 1; }

need_sdk() {
    [ -n "$SDK_DIR" ] || die "请先 source SDK 的 scripts/04-setup-env.sh，或 export SDK_DIR=<rk3506-sdk>"
    [ -f "$KDIR/Makefile" ] || die "找不到内核构建树: $KDIR"
}

build() {
    need_sdk
    command -v "$CROSS_COMPILE"gcc >/dev/null 2>&1 || \
        die "找不到 $CROSS_COMPILE""gcc，请把交叉工具链 bin 加入 PATH"
    log "编译 s2500.ko (kernel=$KDIR)"
    make -C "$KDIR" M="$DRV" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" modules
    if command -v "$CROSS_COMPILE"strip >/dev/null 2>&1; then
        "$CROSS_COMPILE"strip --strip-debug "$DRV/s2500.ko" -o "$DRV/s2500.stripped.ko"
        log "已生成 $DRV/s2500.stripped.ko"
    fi
    ls -l "$DRV/s2500.ko"
}

clean() {
    need_sdk
    make -C "$KDIR" M="$DRV" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" clean
}

deploy() {
    [ "$#" -gt 0 ] || die "用法: bash scripts/18-install-s2500.sh deploy <board-ip> [insmod 参数...]"
    local ip="$1"
    shift
    if [ "$#" -gt 0 ]; then
        PARAMS="$*"
    fi

    build
    log "上传到 root@$ip:/tmp/s2500.ko"
    scp "$DRV/s2500.ko" "root@$ip:/tmp/s2500.ko"

    local tmp
    tmp="$(mktemp)"
    cat > "$tmp" <<'EOS'
set -e
rmmod s2500 2>/dev/null || true
if [ -d /sys/bus/spi/drivers/lan8650 ]; then
    echo spi0.0 > /sys/bus/spi/drivers/lan8650/unbind 2>/dev/null || true
fi
insmod /tmp/s2500.ko @PARAMS@ || { echo "insmod 失败"; exit 1; }
sleep 0.5
IF=$(ls /sys/class/net | grep -E '^eth[0-9]+$' | grep -v '^eth0$' | head -1)
if [ -n "$IF" ]; then
    ip link set "$IF" name t1s0 2>/dev/null || true
fi
ip addr add 192.168.10.15/24 dev t1s0 2>/dev/null || true
ip link set t1s0 up
sleep 1
echo "----- dmesg -----"
dmesg | grep -i s2500 | tail -6 || true
echo "----- link -----"
ip -br addr show t1s0 || true
echo "----- PLCA -----"
cat /sys/class/net/t1s0/plca_status || true
EOS
    sed -i "s|@PARAMS@|$PARAMS|" "$tmp"
    ssh "root@$ip" 'sh -s' < "$tmp"
    rm -f "$tmp"
}

apply_dts() {
    need_sdk
    local dts="$KDIR/arch/arm/boot/dts/HZ-RK3506G2_MiniEVM_TF.dts"
    local p="$ROOT/patches/0006-dts-rk3506-s2500-t1s.patch"
    [ -f "$dts" ] || die "找不到 $dts，请按你的板级 DTS 文件名修改本脚本和补丁"
    log "给 $dts 打 0006 S2500 节点补丁"
    # SDK 自带的这块板 DTS 是 CRLF，补丁也按 CRLF 保存。
    # 若目标 DTS 是 LF，普通 patch 会剥掉补丁行尾的 CR，直接可用。
    if LC_ALL=C grep -q $'\r' "$dts"; then
        local tmp
        tmp="$(mktemp)"
        sed 's/\r$//; s/$/\r/' "$p" > "$tmp"
        patch --binary -p1 -d "$KDIR" < "$tmp"
        rm -f "$tmp"
    else
        patch -p1 -d "$KDIR" < "$p"
    fi
    log "补丁已应用，请按 SDK 流程重编 DTB/FIT 并烧录"
}

case "${1:-build}" in
    build)  build ;;
    deploy) shift; deploy "$@" ;;
    dts)    apply_dts ;;
    clean)  clean ;;
    *)      die "未知命令: $1" ;;
esac
