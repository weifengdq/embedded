#!/bin/bash
# 把适配到 Linux 6.1 的新版 gs_usb 驱动安装进 RK3506 SDK 内核源码树
#
# 背景：`/mnt/sda3/rk3506/gs_usb_x-main` 提供了基于较新内核（6.8/6.14）的 gs_usb
#       （支持 N32H765 8 路 CAN-FD 板 + 标准 candleLight 等）。本仓库
#       `drivers/gs_usb/gs_usb.c` 是适配到 **6.1.118** 的版本。
#
# 适配点（相对 6.8 版，见 patches/0002）：
#   1) 去掉 6.5+ 的 __counted_by(channel_cnt)
#   2) 用 6.1 的 can_rx_offload_get_echo_skb() 替代
#      6.8 的 can_rx_offload_get_echo_skb_queue_{timestamp,tail}()
#   3) type_max(parent->channel_cnt) -> U8_MAX（6.1 的 type_max 只接受类型）
# 另需（见 patches/0003）：
#   CAN_GS_USB 选择 CAN_RX_OFFLOAD（新版驱动依赖 rx-offload，6.1 默认不选）
#
# 幂等。之后重编 kernel + 安装模块 + 重编 rootfs/firmware + 重新制卡。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

SRC="$REPO_DIR/drivers/gs_usb/gs_usb.c"
DST="$SDK_DIR/kernel/drivers/net/can/usb/gs_usb.c"
KCFG="$SDK_DIR/kernel/drivers/net/can/usb/Kconfig"
FRAG="$SDK_DIR/kernel/arch/arm/configs/rk3506-can.config"

[ -f "$SRC" ] || die "找不到适配后的驱动：$SRC"
[ -f "$DST" ] || die "找不到内核驱动路径：$DST（SDK 是否正确？）"

# 1) 驱动源码
if cmp -s "$SRC" "$DST"; then
  log "gs_usb 驱动已是适配版本，跳过"
else
  [ -f "$DST.orig" ] || cp "$DST" "$DST.orig"
  cp "$SRC" "$DST"
  log "已安装适配版 gs_usb 到 $DST（原文件备份为 gs_usb.c.orig）"
fi

# 2) Kconfig: CAN_GS_USB select CAN_RX_OFFLOAD
if [ -f "$KCFG" ] && ! awk '/^config CAN_GS_USB/{f=1} f&&/select CAN_RX_OFFLOAD/{found=1} f&&/^config /&&!/^config CAN_GS_USB/{exit} END{exit !found}' "$KCFG"; then
  python3 - "$KCFG" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="config CAN_GS_USB\n\ttristate \"Geschwister Schneider UG interfaces\"\n\thelp\n"
new="config CAN_GS_USB\n\ttristate \"Geschwister Schneider UG interfaces\"\n\tselect CAN_RX_OFFLOAD\n\thelp\n"
if old in s:
    open(p,'w').write(s.replace(old,new,1)); print("patched Kconfig")
else:
    print("CAN_GS_USB block not found")
PY
  log "已让 CAN_GS_USB select CAN_RX_OFFLOAD"
else
  log "Kconfig 已包含 select CAN_RX_OFFLOAD，跳过"
fi

# 3) 确保 CAN 片段含 CONFIG_CAN_GS_USB=m
if [ -f "$FRAG" ] && ! grep -q "^CONFIG_CAN_GS_USB=m$" "$FRAG"; then
  echo "CONFIG_CAN_GS_USB=m" >> "$FRAG"
  log "已在 rk3506-can.config 中补充 CONFIG_CAN_GS_USB=m"
fi

cat <<EOF

完成。接下来：
  source $REPO_DIR/scripts/04-setup-env.sh
  cd "\$SDK_DIR"
  ./build.sh kernel
  ./build.sh kernel-modules:"\$SDK_DIR/buildroot/output/rockchip_rk3506/target"
  ./build.sh rootfs && ./build.sh firmware
  sudo bash $REPO_DIR/scripts/30-make-tf-card.sh -y /dev/sdX

板端（支持标准 gs_usb / candleLight 及 N32H765 8路CANFD）：
  modprobe gs_usb
  dmesg | tail
  ip -details link show
EOF
