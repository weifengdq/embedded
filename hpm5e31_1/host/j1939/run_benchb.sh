#!/usr/bin/env bash
# run_benchb.sh —— Bench B 联调：主机 vcan0 <-cannelloni-> 板端 CAN1 网关 -> CAN0 J1939 ECU
#
# 前置：板端烧 J1939_WITH_GW=1 的 j1939_ecu（CAN0=ECU@500k，CAN1=app_can_gw UDP4000），
#       主机 enp6s0=192.168.2.1/24、板=192.168.2.100/24 已通。
# 用法：sudo 需要（建 vcan/tc），在本脚本内对 vcan_setup 使用 sudo。
#   bash host/j1939/run_benchb.sh
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$here/../.." && pwd)"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$ROOT/logs/${TS}-host-j1939-benchb.log"
IFACE="${IFACE:-vcan0}"
PY="$HOME/pyenv/bin/python"
[ -x "$PY" ] || PY=python3

echo "[benchb] vcan + cannelloni 隧道 (interface=$IFACE)"
sudo bash "$ROOT/host/can_bridge/vcan_setup.sh" >/tmp/j1939_vcan.log 2>&1 || {
  echo "[benchb] vcan_setup 失败"; tail -5 /tmp/j1939_vcan.log; exit 2; }

"$ROOT/host/can_bridge/run_tunnel.sh" >/tmp/j1939_tunnel.log 2>&1 &
TUN=$!
trap 'kill $TUN 2>/dev/null || true' EXIT
sleep 1.5

# 发两帧非 J1939 的 11 位帧，让板端网关学习主机 UDP peer（cannelloni 为 fire-and-forget）
cansend "$IFACE" 123#00 2>/dev/null || true
sleep 0.3
cansend "$IFACE" 123#00 2>/dev/null || true
sleep 1.0

{
  echo "=== j1939_monitor（8 s，冻结/quiet 只出统计）==="
  "$PY" "$here/j1939_monitor.py" -i "$IFACE" --duration 8 --quiet
  echo
  echo "=== dm1_decode（3 s）==="
  "$PY" "$here/dm1_decode.py" -i "$IFACE" --duration 3
} >"$LOG" 2>&1 || true

tail -24 "$LOG"
echo
if grep -qE "PGN=0x0F004|EEC1" "$LOG"; then
  echo "BENCHB RESULT: PASS（vcan0 上收到板端 J1939 EEC1）"
  echo "LOG: $LOG"
  exit 0
fi
echo "BENCHB RESULT: FAIL（未在 vcan0 收到 EEC1，见 $LOG）"
tail -5 /tmp/j1939_tunnel.log
exit 1
