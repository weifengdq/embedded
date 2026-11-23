#!/usr/bin/env bash
# R5 W4：PTP 软件打戳对照版（U-37-6）：主机 hw 主钟 + 板端 LWIP_PTP=0 从钟
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-r5-w4-ptp-sw.log
exec > >(tee -a "$OUT") 2>&1
bash host/ptp/run_ptp_master.sh start 2>&1 | tail -3
bash scripts/board_lease.sh 110 r5-w4 > /tmp/r5_w4_lease.log 2>&1 &
LEASE=$!
for i in $(seq 1 600); do grep -q acquired /tmp/r5_w4_lease.log 2>/dev/null && break; sleep 0.5; done
grep -q acquired /tmp/r5_w4_lease.log || { echo LEASE_FAIL; bash host/ptp/run_ptp_master.sh stop >/dev/null 2>&1; exit 4; }
echo "[w4] lease ok $(date +%T)"
HPM_HW_LOCK_HELD=1 timeout 60 bash scripts/flash.sh build/ptp_slave-swts/output/demo.elf 2>&1 | grep -E 'Program & Verify|Error' | tail -2
sleep 0.3
timeout 62 python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 48 --send "sleep 1500;ptpbench 30" --out "logs/${TS}-ptp_slave-swts.log" >/dev/null 2>&1 || true
echo '--- 板端统计（软件打戳）'
python3 host/ptp/ptp_stats.py --log "logs/${TS}-ptp_slave-swts.log" 2>&1 | tail -25
echo '--- 主机 ptp4l（HW 打戳主钟）'
python3 host/ptp/ptp_stats.py --log /tmp/ptp4l-master.log --kind host 2>&1 | tail -12
bash host/ptp/run_ptp_master.sh stop >/dev/null 2>&1 || true
kill $LEASE 2>/dev/null; wait $LEASE 2>/dev/null
echo "[w4] done $(date +%T) out=$OUT"
