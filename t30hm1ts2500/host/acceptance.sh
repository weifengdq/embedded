#!/usr/bin/env bash
# acceptance.sh - 一键跑通所有节点的验收测试, 结果写入 docs/results/
# 注意: 板端 lwIP 的 lwiperf 是 iperf2 协议, 这里用 iperf (v2)
set -uo pipefail
IFACE="${IFACE:-enx001ec0d1c337}"
OUTDIR="${1:-docs/results}"
mkdir -p "$OUTDIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$OUTDIR/acceptance-$STAMP.txt"

exec > >(tee "$OUT") 2>&1
echo "== S2500 验收测试 $STAMP =="
echo "接口: $IFACE"; ip -br addr show "$IFACE"
echo; echo "== PLCA =="
ethtool --get-plca-cfg "$IFACE"
ethtool --get-plca-status "$IFACE"

declare -A NODES=(
  [stm32f407]=192.168.10.11
  [hpm5361]=192.168.10.12
  [s32k312]=192.168.10.13
  [tc387]=192.168.10.14
  [rk3506]=192.168.10.15
)

echo; echo "== 链路/ARP/ping =="
for name in "${!NODES[@]}"; do
  ip="${NODES[$name]}"
  echo "--- $name ($ip)"
  if ping -c 3 -W 1 -i 0.2 "$ip" >/dev/null 2>&1; then
    echo "  ping: OK"
    ping -c 20 -i 0.01 -W 1 "$ip" | tail -2
  else
    echo "  ping: FAIL"
    continue
  fi
  echo "  arp: $(ip neigh show "$ip" | head -1)"
done

echo; echo "== TCP 吞吐 (iperf2, 板端需 iperf -s) =="
for name in "${!NODES[@]}"; do
  ip="${NODES[$name]}"
  ping -c 1 -W 1 "$ip" >/dev/null 2>&1 || { echo "--- $name 跳过(不在线)"; continue; }
  echo "--- $name 下行 (主机->板)"
  iperf -c "$ip" -t 8 -i 0 -f m 2>&1 | tail -3
  echo "--- $name 上行 (板->主机)"
  iperf -c "$ip" -t 8 -i 0 -f m -R 2>&1 | tail -3
done

echo; echo "== 结果已保存到 $OUT =="
