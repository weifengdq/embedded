#!/usr/bin/env bash
# run_cal.sh —— Bench B 一键：起隧道 -> 跑 xcp_cal.py -> 收掉隧道
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PY=$HOME/pyenv/bin/python
IFACE="${XCP_CHANNEL:-vcan0}"
BOARD_IP="${BOARD_IP:-192.168.2.100}"
SWEEP="${XCP_SWEEP:-0.1:2.0:10}"
CANNELLONI="$REPO/host/can_bridge/build/cannelloni"
cd "$REPO"
[ -x "$CANNELLONI" ] || { echo "ERROR: 缺少 cannelloni"; exit 2; }
if ! ip link show "$IFACE" >/dev/null 2>&1; then sudo modprobe vcan && sudo ip link add dev "$IFACE" type vcan; fi
sudo ip link set "$IFACE" up
"$CANNELLONI" -I "$IFACE" -R "$BOARD_IP" -r 4000 -l 4000 -4 -t 1000 >/tmp/xcp_bench_b_cal.log 2>&1 &
TUNNEL_PID=$!
trap 'kill $TUNNEL_PID 2>/dev/null' EXIT
sleep 0.8
"$PY" host/xcp/xcp_cal.py --a2l samples/app/xcp_can_cal/xcp_can_cal.a2l --channel "$IFACE" --sweep "$SWEEP"
