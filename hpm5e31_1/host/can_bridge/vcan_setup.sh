#!/usr/bin/env bash
# 创建并启动 vcan 接口 + 限速（cannelloni 的主机侧 "假 CAN 总线"）
#
# 用法：
#   sudo ./vcan_setup.sh            # 只建 vcan0（CAN1 网关）
#   sudo CHANNELS="0 2 3" ./vcan_setup.sh   # 建 vcan0/vcan2/vcan3（gw full 模式）
#   sudo RATE=1mbit ./vcan_setup.sh # 改限速（默认 500kbit，对应经典 500k 总线）
#
# 为什么必须限速：vcan 是内存回环，内核不做速率限制，python-can/cangen 全速灌包
# 会把 480 MHz 的 MCU 网关打爆（UDP 丢包 + CAN TX FIFO 满），测出来的丢包率就不真实了。
set -euo pipefail

CHANNELS="${CHANNELS:-0}"
RATE="${RATE:-500kbit}"
LATENCY="${LATENCY:-10ms}"
BURST="${BURST:-4000}"

if [ "$(id -u)" -ne 0 ]; then
  echo "请用 sudo 运行（需要 modprobe / ip link / tc）" >&2
  exit 2
fi

modprobe vcan
for ch in $CHANNELS; do
  IFACE="vcan$ch"
  if ip link show "$IFACE" >/dev/null 2>&1; then
    echo "[vcan] $IFACE 已存在"
  else
    ip link add dev "$IFACE" type vcan
    echo "[vcan] 创建 $IFACE"
  fi
  ip link set up "$IFACE"
  # tbf 限速：rate=总线速率，latency=排队上限（越小延迟越低、越容易丢）
  tc qdisc del dev "$IFACE" root 2>/dev/null || true
  tc qdisc add dev "$IFACE" root tbf rate "$RATE" latency "$LATENCY" burst "$BURST"
  echo "[vcan] $IFACE UP, tbf rate=$RATE latency=$LATENCY burst=$BURST"
done

echo "---- 当前状态 ----"
ip -br link show | grep vcan || true
for ch in $CHANNELS; do
  tc qdisc show dev "vcan$ch"
done
echo "提示：删除限速 'sudo tc qdisc del dev vcan0 root'；删接口 'sudo ip link del vcan0'"
