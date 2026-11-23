#!/usr/bin/env bash
# run_can_regress.sh —— CAN 路径回归（XCP on CAN 未受 XCPonEth 改动影响）
#
# 做两件事：
#   [A] xcp_daq_raw.py：自写 CAN 客户端（bench B 实测 PASS 的那套），验证 CAN 传输层没回归；
#   [B] xcp_daq.py：pyxcp 默认 CAN 路径，记录当前状态（第 35 章 U-35-2 的上游 bug）。
#
# 前置：板端已运行 samples/app/xcp_can_daq 且 shell 执行了 "gw on"（hwrun --send 完成）。
# 用法：bash host/xcp/run_can_regress.sh [秒数]
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PY=$HOME/pyenv/bin/python
IFACE="${XCP_CHANNEL:-vcan0}"
BOARD_IP="${BOARD_IP:-192.168.2.100}"
SECS="${1:-5}"
CANNELLONI="$REPO/host/can_bridge/build/cannelloni"
OUT=$HOME/tmp2/xcp_can_regress
mkdir -p "$OUT"
cd "$REPO"

if [ ! -x "$CANNELLONI" ]; then
    echo "ERROR: 缺少 $CANNELLONI（先 bash host/can_bridge/build_cannelloni.sh）"
    exit 2
fi

echo "===== 0) 准备 $IFACE（解绑限速 500 kbit/s）====="
if ! ip link show "$IFACE" >/dev/null 2>&1; then
    sudo modprobe vcan && sudo ip link add dev "$IFACE" type vcan
fi
sudo ip link set "$IFACE" up
sudo tc qdisc del dev "$IFACE" root >/dev/null 2>&1
sudo tc qdisc add dev "$IFACE" root tbf rate 500kbit latency 100ms burst 4000 >/dev/null 2>&1

echo "===== 1) cannelloni 隧道 vcan0 <-> $BOARD_IP:4000 ====="
"$CANNELLONI" -I "$IFACE" -R "$BOARD_IP" -r 4000 -l 4000 -4 -t 1000 >"$OUT/cannelloni.log" 2>&1 &
TUNNEL_PID=$!
trap 'kill $TUNNEL_PID 2>/dev/null' EXIT
sleep 0.8

echo "===== 2) [A] xcp_daq_raw.py（自写客户端，CAN 回归）====="
"$PY" host/xcp/xcp_daq_raw.py --a2l samples/app/xcp_can_daq/xcp_can_daq.a2l \
      --channel "$IFACE" --secs "$SECS" --csv "$OUT/raw.csv"
RC_A=$?

echo "===== 3) [B] xcp_daq.py（pyxcp 默认 CAN 路径，仅记录状态）====="
timeout 45 "$PY" host/xcp/xcp_daq.py --a2l samples/app/xcp_can_daq/xcp_can_daq.a2l \
      --channel "$IFACE" --secs 4 --csv "$OUT/pyxcp.csv" 2>&1 | tail -25
RC_B=${PIPESTATUS[0]}

echo "CAN-REGRESS raw_rc=$RC_A pyxcp_rc=$RC_B（raw=0 表示 CAN 数据面未回归；pyxcp 失败见 U-35-2）"
exit 0
