#!/usr/bin/env bash
# acceptance.sh - can_gateway 主机侧一键验收（板端固件运行中执行）
#
# 用法（板端已跑起 can_gateway 后）：
#   timeout 80 bash host/can_bridge/acceptance.sh
# 或经 hwrun --post 自动在持锁期间执行：
#   bash scripts/hwrun.sh samples/app/can_gateway --secs 10 --send "quiet;gw on;pass" \
#        --post "timeout 90 bash host/can_bridge/acceptance.sh" --lock-wait 600
#
# 依次测：ping / candump 心跳 / cansend 回显 / python-can 延迟+丢包
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_IP="${BOARD_IP:-192.168.2.100}"
IFACE="${IFACE:-vcan0}"
OUT="${OUT:-/tmp/can_bridge_accept}"
PY="${PY:-$HOME/pyenv/bin/python}"
mkdir -p "$OUT"
PASS=0
FAIL=0
chk() { if [ "$2" = PASS ]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi; echo "  [$2] $1"; }

echo "===== 0) 准备 vcan0 + cannelloni 隧道 ====="
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  sudo modprobe vcan && sudo ip link add dev "$IFACE" type vcan
fi
sudo ip link set up "$IFACE"
sudo tc qdisc del dev "$IFACE" root 2>/dev/null
sudo tc qdisc add dev "$IFACE" root tbf rate 500kbit latency 10ms burst 4000 2>/dev/null
echo "  $(tc qdisc show dev $IFACE | head -1)"

pkill -f "can_bridge/build/cannelloni" 2>/dev/null
sleep 0.3
"$here/run_tunnel.sh" > "$OUT/tunnel.log" 2>&1 &
TPID=$!
sleep 1

echo "===== 1) ping ====="
ping -c 5 -i 0.2 -W 1 "$BOARD_IP" > "$OUT/ping.log" 2>&1
if grep -q "0% packet loss" "$OUT/ping.log"; then
  grep -oE "rtt min/avg/max/mdev = [0-9./]+ ms" "$OUT/ping.log"
  chk "ping 192.168.2.100 5/5" PASS
else
  chk "ping 192.168.2.100" FAIL
fi

echo "===== 2) candump 心跳（10 Hz，2 s）====="
# 板端网关是"被动方"：必须先收到主机的一个 UDP 包才会学习 peer 并开始回发 CAN 帧，
# 否则这里会抓到 0 帧（踩过：心跳 0 帧但后面的回显正常）。
cansend "$IFACE" 7FE#00 >/dev/null 2>&1
sleep 0.3
timeout 2 candump "$IFACE" > "$OUT/candump_hb.log" 2>&1
HB=$(grep -c "321" "$OUT/candump_hb.log" 2>/dev/null || true)
HB=${HB:-0}
echo "  0x321 帧数 = $HB"
if [ "$HB" -ge 10 ]; then chk "心跳 10 Hz" PASS; else chk "心跳 10 Hz" FAIL; fi

echo "===== 3) cansend 回显 0x123 -> 0x321 ====="
( timeout 2 candump "$IFACE" > "$OUT/candump_echo.log" 2>&1 ) &
CD=$!
sleep 0.3
cansend "$IFACE" 123#1122334455667788
wait $CD
if grep -q "321.*11 22 33 44 55 66 77 88" "$OUT/candump_echo.log"; then
  chk "0x123 回显成 0x321（载荷一致）" PASS
else
  chk "0x123 回显" FAIL
  head -5 "$OUT/candump_echo.log"
fi

echo "===== 4) python-can 延迟 / 丢包 ====="
"$PY" "$here/bench_gateway.py" --hb-secs 1.5 --lat-n 100 --lat-interval-ms 20 \
    --loss-rate 300 --loss-secs 5 --tag "${TAG:-t1000}" --json-out "$OUT/bench.json" > "$OUT/bench.log" 2>&1
BRC=$?
tail -14 "$OUT/bench.log"
if [ "$BRC" -eq 0 ]; then chk "延迟/丢包验收" PASS; else chk "延迟/丢包验收" FAIL; fi

kill "$TPID" 2>/dev/null
pkill -f "can_bridge/build/cannelloni" 2>/dev/null
sleep 0.2

echo "===== 汇总 ====="
echo "  PASS=$PASS FAIL=$FAIL  日志目录=$OUT"
[ "$FAIL" -eq 0 ] && echo "  RESULT: PASS" || echo "  RESULT: FAIL"
[ "$FAIL" -eq 0 ]