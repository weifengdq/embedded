#!/usr/bin/env bash
# Bench B 的 vcan0 准备：加载模块 + 建 vcan + 限速 500 kbit/s（幂等）
set -euo pipefail
sudo modprobe vcan
if ! ip link show vcan0 >/dev/null 2>&1; then
  sudo ip link add dev vcan0 type vcan
fi
sudo ip link set up vcan0
if ! tc qdisc show dev vcan0 | grep -q "tbf"; then
  sudo tc qdisc add dev vcan0 root tbf rate 500kbit latency 100ms burst 4000
fi
ip -br link show vcan0
tc qdisc show dev vcan0
echo "vcan0 ready; 再启动 cannelloni（见 README 第 2 节）"
