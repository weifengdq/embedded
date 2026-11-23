#!/usr/bin/env bash
# run_daq.sh —— Bench B 一键：起 vcan0 + cannelloni 隧道 -> 跑 xcp_daq.py -> 收掉隧道
# 用法：bash host/xcp/run_daq.sh [秒数]
# 前置：板端固件已执行 "gw on"（经 hwrun --post 时由 --send 完成）
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PY=$HOME/pyenv/bin/python
IFACE="${XCP_CHANNEL:-vcan0}"
BOARD_IP="${BOARD_IP:-192.168.2.100}"
SECS="${1:-8}"
CANNELLONI="$REPO/host/can_bridge/build/cannelloni"
OUT="${OUT:-/tmp/xcp_bench_b}"
mkdir -p "$OUT"
cd "$REPO"

echo "===== 1) 准备 $IFACE ====="
if ! ip link show "$IFACE" >/dev/null 2>&1; then
    sudo modprobe vcan && sudo ip link add dev "$IFACE" type vcan
fi
sudo ip link set "$IFACE" up
# 限速：vcan 默认无速率限制，会把 MCU/网关淹没（约定文档 §3）
sudo tc qdisc del dev "$IFACE" root >/dev/null 2>&1
sudo tc qdisc add dev "$IFACE" root tbf rate 500kbit latency 100ms burst 4000 >/dev/null 2>&1

echo "===== 2) cannelloni 隧道 ====="
if [ ! -x "$CANNELLONI" ]; then
    echo "ERROR: 缺少 $CANNELLONI（先 bash host/can_bridge/build_cannelloni.sh）"
    exit 2
fi
"$CANNELLONI" -I "$IFACE" -R "$BOARD_IP" -r 4000 -l 4000 -4 -t 1000 >"$OUT/cannelloni.log" 2>&1 &
TUNNEL_PID=$!
trap 'kill $TUNNEL_PID 2>/dev/null' EXIT
sleep 0.8

echo "===== 3) XCP DAQ 上位机测试 ====="
"$PY" host/xcp/xcp_daq_raw.py --a2l samples/app/xcp_can_daq/xcp_can_daq.a2l \
      --channel "$IFACE" --secs "$SECS" --csv "$OUT/xcp_daq.csv"
RC=$?
echo "===== 4) 结束（rc=$RC）====="
exit $RC
