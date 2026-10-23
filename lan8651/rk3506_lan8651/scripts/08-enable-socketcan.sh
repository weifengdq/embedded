#!/bin/bash
# 启用 SocketCAN（vcan / can / can-raw / can-bcm / can-gw / j1939 / isotp / gs_usb 等）
# 幂等，可重复执行。
#
# 背景：厂商内核默认 `# CONFIG_CAN is not set`，因此上述模块根本不存在，modprobe 全部失败。
# 做法：
#   1) 新增内核配置片段 kernel/arch/arm/configs/rk3506-can.config（CAN 核心/协议/虚拟/USB 驱动=m）
#   2) 板级 defconfig 增加该 fragment
#   3) 确保 buildroot 带 can-utils（cangw/candump/cansend/isotp*/j1939* 等命令）
#   4) 增加 /etc/modprobe.d/can.conf 别名（j1939/iso-tp/cangw 等简写）
#
# 之后需：重编 kernel + 安装模块 + 重编 rootfs/firmware + 重新制卡。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

[ -d "$SDK_DIR/kernel" ] || die "SDK 不存在：$SDK_DIR"
KDIR="$SDK_DIR/kernel/arch/arm/configs"
BOARD_DEF="$SDK_DIR/device/rockchip/.chips/rk3506/HZ-RK3506G2_MiniEVM_TF_defconfig"
BDEF="$SDK_DIR/buildroot/configs/rockchip_rk3506_defconfig"
OV="$SDK_DIR/buildroot/board/rockchip/rk3506/fs-overlay"

# 1) 内核 fragment
cat > "$KDIR/rk3506-can.config" <<'EOF'
# SocketCAN (enabled by rk3506_1)
CONFIG_CAN=m
CONFIG_CAN_RAW=m
CONFIG_CAN_BCM=m
CONFIG_CAN_GW=m
CONFIG_CAN_J1939=m
CONFIG_CAN_ISOTP=m
CONFIG_CAN_DEV=m
CONFIG_CAN_VCAN=m
CONFIG_CAN_VXCAN=m
CONFIG_CAN_SLCAN=m
CONFIG_CAN_GS_USB=m
CONFIG_CAN_8DEV_USB=m
CONFIG_CAN_EMS_USB=m
CONFIG_CAN_ESD_USB=m
CONFIG_CAN_ETAS_ES58X=m
CONFIG_CAN_KVASER_USB=m
CONFIG_CAN_MCBA_USB=m
CONFIG_CAN_PEAK_USB=m
CONFIG_CAN_UCAN=m
EOF
log "已写入 $KDIR/rk3506-can.config"

# 2) 板级 defconfig fragments
if grep -q "rk3506-can.config" "$BOARD_DEF"; then
  log "板级 defconfig 已包含 rk3506-can.config"
else
  sed -i 's|^RK_KERNEL_CFG_FRAGMENTS="\(.*\)"|RK_KERNEL_CFG_FRAGMENTS="\1 rk3506-can.config"|' "$BOARD_DEF"
  log "已把 rk3506-can.config 加入 RK_KERNEL_CFG_FRAGMENTS"
fi

# 3) can-utils
grep -q "^BR2_PACKAGE_CAN_UTILS=y$" "$BDEF" || echo "BR2_PACKAGE_CAN_UTILS=y" >> "$BDEF"

# 4) modprobe 别名
mkdir -p "$OV/etc/modprobe.d"
cat > "$OV/etc/modprobe.d/can.conf" <<'EOF'
# [rk3506_1] SocketCAN 模块别名，便于使用简写 modprobe
alias j1939     can-j1939
alias can_j1939 can-j1939
alias isotp     can-isotp
alias iso-tp    can-isotp
alias can_isotp can-isotp
alias cangw     can-gw
alias can_gw    can-gw
alias can_raw   can-raw
alias can_bcm   can-bcm
alias gs-usb    gs_usb
alias 8dev_usb  usb_8dev
EOF
log "已写入 $OV/etc/modprobe.d/can.conf"

cat <<EOF

完成。接下来：
  source $REPO_DIR/scripts/04-setup-env.sh
  cd "\$SDK_DIR"
  ./build.sh HZ-RK3506G2_MiniEVM_TF_defconfig      # 让 output/.config 带上新 fragment
  ./build.sh kernel                               # 生成 CAN 模块
  ./build.sh kernel-modules:"\$SDK_DIR/buildroot/output/rockchip_rk3506/target"
  ./build.sh rootfs && ./build.sh firmware
  sudo bash $REPO_DIR/scripts/30-make-tf-card.sh -y /dev/sdX

板端验证（示例）：
  modprobe vcan; ip link add dev vcan0 type vcan; ip link set vcan0 up
  modprobe can; modprobe can_raw; modprobe can_bcm; modprobe can_gw
  modprobe j1939        # alias -> can-j1939
  modprobe iso-tp       # alias -> can-isotp
  modprobe gs_usb       # USB CAN 适配器
  cangw -L              # 查看网关规则（can-utils）
EOF
