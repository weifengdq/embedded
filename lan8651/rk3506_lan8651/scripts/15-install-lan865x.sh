#!/bin/bash
# 把 LAN8651 (10BASE-T1S, OPEN Alliance TC6) 支持安装进 RK3506 6.1.118 内核源码树
#
# 组成：
#   1) patches/0004-rk3506-6.1-lan865x-plca-core.patch
#      - 把 mainline v6.9+ 的 PLCA（ethtool netlink + genphy_c45_plca_*）回移到 6.1
#      - 补 10BASE-T1S link mode 位（93/94/95）并在 phy-core/common.c 注册
#      - 增加 OA_TC6 / LAN865X / MICROCHIP_T1S_PHY 的 Kconfig/Makefile 挂点
#   2) drivers/lan865x/*  （从 Microchip lan865x-linux-driver-1v1 + mainline 6.12
#      适配到 6.1 的新文件；含自动恢复、硬件 reset、PLCA 配置保持）
#   3) patches/0005-dts-rk3506-lan8651-t1s.patch  （SPI0 + GPIO + 禁 EXP_D1）
#
# 幂等，可重复执行。之后：
#   source scripts/04-setup-env.sh
#   cd "$SDK_DIR" && ./build.sh kernel && ./build.sh kernel-modules:"$SDK_DIR/buildroot/output/rockchip_rk3506/target"
#   （或直接 bash scripts/31-deploy-kernel-ssh.sh <board-ip> 做增量部署）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

KDIR="$SDK_DIR/kernel"
PATCH_CORE="$REPO_DIR/patches/0004-rk3506-6.1-lan865x-plca-core.patch"
PATCH_DTS="$REPO_DIR/patches/0005-dts-rk3506-lan8651-t1s.patch"
SRC="$REPO_DIR/drivers/lan865x"
DTS="$KDIR/arch/arm/boot/dts/HZ-RK3506G2_MiniEVM_TF.dts"

[ -d "$KDIR" ] || die "内核源码树不存在：$KDIR"
[ -f "$PATCH_CORE" ] || die "缺少补丁：$PATCH_CORE"
[ -f "$PATCH_DTS" ] || die "缺少补丁：$PATCH_DTS"
[ -d "$SRC" ] || die "缺少驱动源码：$SRC"

# ---------------------------------------------------------------------------
# 1) 内核核心补丁（PLCA / link mode / Kconfig）
# ---------------------------------------------------------------------------
if grep -q "genphy_c45_plca_get_cfg" "$KDIR/drivers/net/phy/phy-c45.c" 2>/dev/null; then
  log "内核 PLCA 回移已存在，跳过 0004"
else
  log "应用 0004（6.1 PLCA/OA-TC6 回移）..."
  patch -d "$KDIR" -p1 --forward < "$PATCH_CORE"
fi

# ---------------------------------------------------------------------------
# 2) 新增驱动源文件
# ---------------------------------------------------------------------------
install -D -m 644 "$SRC/oa_tc6.h"                 "$KDIR/include/linux/oa_tc6.h"
install -D -m 644 "$SRC/oa_tc6.c"                 "$KDIR/drivers/net/ethernet/oa_tc6.c"
install -D -m 644 "$SRC/lan865x.c"                "$KDIR/drivers/net/ethernet/microchip/lan865x/lan865x.c"
install -D -m 644 "$SRC/Kconfig.lan865x"          "$KDIR/drivers/net/ethernet/microchip/lan865x/Kconfig"
install -D -m 644 "$SRC/Makefile.lan865x"         "$KDIR/drivers/net/ethernet/microchip/lan865x/Makefile"
install -D -m 644 "$SRC/microchip_t1s.c"          "$KDIR/drivers/net/phy/microchip_t1s.c"
install -D -m 644 "$SRC/mdio-open-alliance.h"     "$KDIR/drivers/net/phy/mdio-open-alliance.h"
install -D -m 644 "$SRC/plca.c"                   "$KDIR/net/ethtool/plca.c"
log "已安装 drivers/lan865x/* 到内核树"

# ---------------------------------------------------------------------------
# 3) 板级 DTS
# ---------------------------------------------------------------------------
if grep -q "lan865x: ethernet@0" "$DTS" 2>/dev/null; then
  log "DTS 已包含 lan865x 节点，跳过 0005"
else
  log "应用 0005（SPI0/LAN8651 DTS）..."
  patch -d "$KDIR" -p1 --forward < "$PATCH_DTS"
fi

# ---------------------------------------------------------------------------
# 4) 内核配置片段 + 板级 defconfig
# ---------------------------------------------------------------------------
FRAG="$KDIR/arch/arm/configs/rk3506-lan8651.config"
cat > "$FRAG" <<'EOF'
# 10BASE-T1S / LAN8651 (enabled by rk3506_1)
CONFIG_SPI=y
CONFIG_SPI_ROCKCHIP=y
CONFIG_PHYLIB=y
CONFIG_MDIO_DEVICE=y
CONFIG_OA_TC6=y
CONFIG_LAN865X=y
CONFIG_MICROCHIP_T1S_PHY=y
EOF
log "已写入 $FRAG"

BOARD_DEF="$SDK_DIR/device/rockchip/rk3506/HZ-RK3506G2_MiniEVM_TF_defconfig"
[ -f "$BOARD_DEF" ] || BOARD_DEF="$SDK_DIR/device/rockchip/.chips/rk3506/HZ-RK3506G2_MiniEVM_TF_defconfig"
if [ -f "$BOARD_DEF" ]; then
  if grep -q "rk3506-lan8651.config" "$BOARD_DEF"; then
    log "板级 defconfig 已包含 rk3506-lan8651.config"
  else
    sed -i 's|^RK_KERNEL_CFG_FRAGMENTS="\(.*\)"|RK_KERNEL_CFG_FRAGMENTS="\1 rk3506-lan8651.config"|' "$BOARD_DEF"
    log "已把 rk3506-lan8651.config 加入 RK_KERNEL_CFG_FRAGMENTS"
  fi
else
  warn "未找到板级 defconfig，请手工把 rk3506-lan8651.config 加入 RK_KERNEL_CFG_FRAGMENTS"
fi

# 当前构建树 .config 立即生效（便于不重新 defconfig 直接编译）
if [ -f "$KDIR/.config" ]; then
  ( cd "$KDIR" && ./scripts/config --file .config \
      -e SPI -e SPI_ROCKCHIP -e PHYLIB -e MDIO_DEVICE \
      -e OA_TC6 -e LAN865X -e MICROCHIP_T1S_PHY )
  log "已在当前 .config 中启用 OA_TC6/LAN865X/MICROCHIP_T1S_PHY"
fi

cat <<EOF

完成。下一步：
  source $REPO_DIR/scripts/04-setup-env.sh
  cd "\$SDK_DIR"
  ./build.sh kernel
  # 生成 boot FIT（thunderboot.its）：
  bash $REPO_DIR/scripts/31-deploy-kernel-ssh.sh <board-ip>

或完整重编：
  ./build.sh kernel && ./build.sh kernel-modules:"\$SDK_DIR/buildroot/output/rockchip_rk3506/target"
  ./build.sh rootfs && ./build.sh firmware

板端：
  ethtool --get-plca-cfg eth1
  ethtool --set-plca-cfg eth1 enable on node-id 0 node-cnt 8 to-tmr 32 \\
      burst-cnt 0 burst-tmr 128
EOF
