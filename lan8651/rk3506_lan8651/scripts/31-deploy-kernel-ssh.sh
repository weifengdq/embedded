#!/bin/bash
# 通过 SSH 增量部署新内核（FIT）与内核模块到 HZ-RK3506G2_MiniEVM (TF)
#
# 为什么可以不用读卡器：
#   - TF 卡分区 p3 = boot，裸放 zboot.img（FIT: zImage + DTB，thunderboot.its）
#   - 在板端直接 dd 覆盖 /dev/mmcblk0p3，再重启即可
#   - 根文件系统 /lib/modules/6.1.118 直接替换（先自动备份到 /userdata）
#
# 用法：
#   bash scripts/31-deploy-kernel-ssh.sh <board-ip> [--no-reboot]
#   BOARD_PASS=rockchip（默认）
#
# 前置：已 source scripts/04-setup-env.sh 并完成内核编译；
#       或先跑 scripts/15-install-lan865x.sh + ./build.sh kernel。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

IP="${1:-}"; [ -n "$IP" ] || die "用法：$0 <board-ip> [--no-reboot]"
REBOOT=1; [ "${2:-}" = "--no-reboot" ] && REBOOT=0
PASS="${BOARD_PASS:-rockchip}"
KDIR="$SDK_DIR/kernel"
DTS="$KDIR/arch/arm/boot/dts/HZ-RK3506G2_MiniEVM_TF.dtb"
IMG="$KDIR/arch/arm/boot/zImage"
FIT="$KDIR/zboot-lan8651.img"
ITS="$SDK_DIR/device/rockchip/rk3506/thunderboot.its"
TMPMOD="$WORK_DIR/modroot"

[ -f "$IMG" ] || die "缺少 zImage：$IMG（先编译内核）"
[ -f "$DTS" ] || die "缺少 DTB：$DTS"
[ -f "$ITS" ] || die "缺少 ITS：$ITS"

log "生成 FIT：$FIT"
TMP=$(mktemp)
sed -e "s~@KERNEL_DTB@~$(realpath "$DTS")~" \
    -e "s~@KERNEL_IMG@~$(realpath "$IMG")~" \
    -e "s~@RAMDISK_IMG@~~" -e "s~@RESOURCE_IMG@~~" "$ITS" > "$TMP"
"$SDK_DIR/rkbin/tools/mkimage" -f "$TMP" -E -p 0x800 "$FIT" >/dev/null
rm -f "$TMP"
ls -la "$FIT"

log "安装模块到临时目录：$TMPMOD"
rm -rf "$TMPMOD"; mkdir -p "$TMPMOD"
( cd "$KDIR" && make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" \
    modules_install INSTALL_MOD_PATH="$TMPMOD" >/dev/null )
[ -d "$TMPMOD/lib/modules/6.1.118" ] || die "modules_install 失败"
TAR="$WORK_DIR/modules-6.1.118-new.tar.gz"
( cd "$TMPMOD/lib/modules/6.1.118" && tar czf "$TAR" . )
ls -la "$TAR"

SSH="sshpass -p $PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
SCP="sshpass -p $PASS scp -o StrictHostKeyChecking=no -o ConnectTimeout=10"

log "上传到 $IP:/tmp ..."
$SCP "$FIT" "$TAR" "root@$IP:/tmp/"

log "板端部署（备份旧模块 + 覆盖 p3 + 替换模块）..."
$SSH "root@$IP" '
set -e
TS=$(date +%Y%m%d-%H%M%S)
if [ -d /lib/modules/6.1.118 ] && [ ! -f /userdata/modules-6.1.118-old-$TS.tar.gz ]; then
  tar czf /userdata/modules-6.1.118-old-$TS.tar.gz -C /lib/modules/6.1.118 . || true
fi
rm -rf /lib/modules/6.1.118.new
mkdir -p /lib/modules/6.1.118.new
tar xzf /tmp/modules-6.1.118-new.tar.gz -C /lib/modules/6.1.118.new
rm -rf /lib/modules/6.1.118.old
mv /lib/modules/6.1.118 /lib/modules/6.1.118.old
mv /lib/modules/6.1.118.new /lib/modules/6.1.118
dd if=/tmp/zboot-lan8651.img of=/dev/mmcblk0p3 bs=1M conv=fsync
sync
echo "boot partition written: $(md5sum /tmp/zboot-lan8651.img)"
'

if [ "$REBOOT" = 1 ]; then
  log "重启板卡..."
  $SSH "root@$IP" 'reboot' || true
  log "已发送 reboot。串口 1500000 观察启动；若 IP 变化请重新查看 DHCP 地址。"
else
  log "未重启；请手动 reboot。"
fi
