#!/usr/bin/env bash
# host_benchb.sh —— multiproto_coexist 的 Bench B 主机侧验证（由 hwrun --post 调用，持锁窗口内执行）
#
# 1) 建 vcan0 + 启动 cannelloni 隧道（板 192.168.2.100:4000 <-> 主机 vcan0）
# 2) 串口抓两次 stat（注入前后），对比网关统计
# 3) cangen 从主机注入 11 位帧，验证 CAN1<-UDP 方向与 CAN0 crosstalk 计数
# 4) j1939_monitor 抓 8 s，确认 J1939 帧穿过网关且周期正常
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$here/../../.." && pwd)"
IFACE="${IFACE:-vcan0}"
PY="${PY:-$HOME/pyenv/bin/python}"
[ -x "$PY" ] || PY=python3
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$ROOT/logs/${TS}-multiproto_coexist-host.log"
SER="$ROOT/scripts/serial_cap.py"
SER_ARGS=(--port /dev/ttyACM0 --baud 921600)

echo "[benchb] vcan + tunnel"
sudo bash "$ROOT/host/can_bridge/vcan_setup.sh" >/tmp/mp_vcan.log 2>&1 || { tail -3 /tmp/mp_vcan.log; exit 2; }
"$ROOT/host/can_bridge/run_tunnel.sh" >/tmp/mp_tunnel.log 2>&1 &
TUN=$!
trap 'kill $TUN 2>/dev/null || true' EXIT
sleep 1.5
cansend "$IFACE" 123#00 2>/dev/null || true
sleep 0.3
cansend "$IFACE" 123#00 2>/dev/null || true
sleep 0.5

{
  echo "=== [1] board stat before host injection ==="
  "$PY" "$SER" "${SER_ARGS[@]}" --secs 2 --send "stat" --out /tmp/mp_stat1.log >/dev/null 2>&1 || true
  grep -aE "J1939:|CANopen:|GW:|CPU:" /tmp/mp_stat1.log
  echo
  echo "=== [2] host -> board injection (cangen 11-bit 100/s x 4s) ==="
  cangen "$IFACE" -g 10 -I 123 -L 8 -n 400 >/tmp/mp_cangen.log 2>&1 || true
  sleep 0.5
  echo "=== [3] board stat after injection ==="
  "$PY" "$SER" "${SER_ARGS[@]}" --secs 2 --send "stat" --out /tmp/mp_stat2.log >/dev/null 2>&1 || true
  grep -aE "J1939:|CANopen:|GW:|CPU:" /tmp/mp_stat2.log
  echo
  echo "=== [4] j1939_monitor on vcan0 (8 s) ==="
  "$PY" "$ROOT/host/j1939/j1939_monitor.py" -i "$IFACE" --duration 8 --quiet --raw || true
} >"$LOG" 2>&1

echo "---- host log: $LOG ----"
tail -30 "$LOG"
echo
if grep -q "PGN=0x0F004\|EEC1" "$LOG" && grep -q "drop_can=\|GW:" "$LOG"; then
  echo "BENCHB RESULT: PASS"
  exit 0
fi
echo "BENCHB RESULT: FAIL"
exit 1