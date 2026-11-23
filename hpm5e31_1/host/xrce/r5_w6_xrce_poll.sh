#!/usr/bin/env bash
# R5 W6：xrce_pubsub 非阻塞会话（U-36-5）— 只量串口 loop/poll_cost
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-r5-w6-xrce-poll.log
exec > >(tee -a "$OUT") 2>&1
bash scripts/board_lease.sh 90 r5-w6 > /tmp/r5_w6_lease.log 2>&1 &
LEASE=$!
for i in $(seq 1 1100); do grep -q acquired /tmp/r5_w6_lease.log 2>/dev/null && break; sleep 0.5; done
grep -q acquired /tmp/r5_w6_lease.log || { echo LEASE_FAIL; kill $LEASE 2>/dev/null; exit 4; }
echo "[w6] lease ok $(date +%T)"
HPM_HW_LOCK_HELD=1 timeout 60 bash scripts/flash.sh build/xrce_pubsub-debug/output/demo.elf 2>&1 | grep -E 'Program & Verify|Cannot|Error' | tail -2
sleep 0.5
ok=0; for i in $(seq 1 20); do ping -c1 -W1 192.168.2.100 >/dev/null 2>&1 && { ok=1; break; }; sleep 0.5; done
[ "$ok" = 1 ] || echo '[w6] FAIL ping'
timeout 40 python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 32 --out "logs/${TS}-xrce_pubsub.log" >/dev/null 2>&1 || true
echo '--- 板端统计（改后）'; grep -aE 'loop=|poll_cost|STAT|state=' "logs/${TS}-xrce_pubsub.log" | tail -20
kill $LEASE 2>/dev/null; wait $LEASE 2>/dev/null
echo "[w6] done $(date +%T) out=$OUT"
