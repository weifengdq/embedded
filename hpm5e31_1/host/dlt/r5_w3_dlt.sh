#!/usr/bin/env bash
# R5 W3：dlt_log 直连本机 dlt-daemon（U-38-8）
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-r5-w3-dlt.log
exec > >(tee -a "$OUT") 2>&1
bash scripts/board_lease.sh 100 r5-w3 > /tmp/r5_w3_lease.log 2>&1 &
LEASE=$!
for i in $(seq 1 600); do grep -q acquired /tmp/r5_w3_lease.log 2>/dev/null && break; sleep 0.5; done
grep -q acquired /tmp/r5_w3_lease.log || { echo LEASE_FAIL; exit 4; }
echo "[w3] lease ok $(date +%T)"
HPM_HW_LOCK_HELD=1 timeout 60 bash scripts/flash.sh build/dlt_log-debug/output/demo.elf 2>&1 | grep -E 'Program & Verify|Error' | tail -2
sleep 0.3
for i in $(seq 1 16); do ping -c1 -W1 192.168.2.100 >/dev/null 2>&1 && break; sleep 0.5; done
python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 26 --out "logs/${TS}-dlt_log.log" >/dev/null 2>&1 &
CAP=$!
sleep 2
timeout 40 bash host/dlt/r5_dlt_daemon.sh
wait $CAP 2>/dev/null || true
echo '--- 板端串口'; grep -aE 'dlt|DLT|stat' "logs/${TS}-dlt_log.log" | tail -10
kill $LEASE 2>/dev/null; wait $LEASE 2>/dev/null
echo "[w3] done $(date +%T) out=$OUT"
