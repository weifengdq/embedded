#!/usr/bin/env bash
# Bench B：CANopen 分段 SDO 吞吐（UDP 网关），可指定 vcan 限速
# 用法：bash host/canopen/run_sdo_bench.sh [rate]     rate 例: 500kbit / 2mbit / none
set -uo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
REPO="$(cd ../.. && pwd)"
BR="$REPO/host/can_bridge"
RATE="${1:-2mbit}"
[ $# -gt 0 ] && shift

bash "$BR/vcan_setup.sh" >/dev/null 2>&1 || true
# 按参数重设限速（none = 删除 qdisc）
sudo tc qdisc del dev vcan0 root 2>/dev/null || true
if [ "$RATE" != "none" ]; then
  sudo tc qdisc add dev vcan0 root tbf rate "$RATE" latency 50ms burst 16000
fi
echo "[sdo] vcan0 qdisc: $(tc qdisc show dev vcan0 | head -1)"

pkill -f "cannelloni -I vcan0" 2>/dev/null || true
sleep 0.3
if [ -x "$BR/build/cannelloni" ]; then
  "$BR/build/cannelloni" -I vcan0 -R 192.168.2.100 -r 4000 -l 4000 -t 1000 >/tmp/sdo_cannelloni.log 2>&1 &
  CPID=$!
  sleep 0.8
else
  echo "WARN: cannelloni 未构建"; CPID=0
fi

rc=0
"$PY" co_sdo_seg.py --iterations 3 "$@" 2>&1 | tail -30 || rc=1

[ "$CPID" != "0" ] && kill "$CPID" 2>/dev/null
pkill -f "cannelloni -I vcan0" 2>/dev/null || true
exit $rc
