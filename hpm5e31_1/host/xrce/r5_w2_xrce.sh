#!/usr/bin/env bash
# R5 W2：xrce_pubsub 非阻塞会话（U-36-5 改动）+ 断线重连（U-36-4）
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-r5-w2-xrce.log
exec > >(tee -a "$OUT") 2>&1
bash scripts/board_lease.sh 110 r5-w2 > /tmp/r5_w2_lease.log 2>&1 &
LEASE=$!
for i in $(seq 1 600); do grep -q acquired /tmp/r5_w2_lease.log 2>/dev/null && break; sleep 0.5; done
grep -q acquired /tmp/r5_w2_lease.log || { echo LEASE_FAIL; exit 4; }
echo "[w2] lease ok $(date +%T)"
ss -lunp 2>/dev/null | grep -F ':2018' || echo "[w2] WARN agent 未监听 2018"
HPM_HW_LOCK_HELD=1 timeout 60 bash scripts/flash.sh build/xrce_pubsub-debug/output/demo.elf 2>&1 | grep -E 'Program & Verify|Error' | tail -2
sleep 0.3
python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 42 --out "logs/${TS}-xrce_pubsub.log" >/dev/null 2>&1 &
CAP=$!
sleep 6
source /opt/ros/lyrical/setup.bash
echo '--- [1] ros2 话题'; timeout 15 ros2 topic list 2>&1 | grep hpm5e31 || true
echo '--- [2] 100 Hz 基线 hz'; timeout 14 ros2 topic hz /hpm5e31/adc --window 30 2>&1 | tail -4
echo '--- [3] 断线重连：kill Agent 8 s 后重启'
pkill -x MicroXRCEAgent 2>/dev/null || true
sleep 8
bash host/xrce/run_agent.sh -d >/dev/null 2>&1 || true
sleep 6
ss -lunp 2>/dev/null | grep -F ':2018' && echo '[w2] agent 已重启'
echo '--- [4] 恢复后 hz'; timeout 14 ros2 topic hz /hpm5e31/adc --window 30 2>&1 | tail -4
wait $CAP 2>/dev/null || true
echo '--- [5] 板端串口统计（loop/poll_cost/reconnects）'
grep -aE 'loop=|reconnects|STAT|reliable|state=' "logs/${TS}-xrce_pubsub.log" | tail -24
kill $LEASE 2>/dev/null; wait $LEASE 2>/dev/null
echo "[w2] done $(date +%T) out=$OUT"
