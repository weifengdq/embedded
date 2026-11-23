#!/usr/bin/env bash
# R5 W1：110s 租约内跑两个固件——someip_tp（主机侧 4096B TP，含 tpgap 对比）+ someip_rpc（vsomeip debug）
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-r5-w1.log
exec > >(tee -a "$OUT") 2>&1
bash scripts/board_lease.sh 110 r5-w1 > /tmp/r5_w1_lease.log 2>&1 &
LEASE=$!
for i in $(seq 1 600); do grep -q acquired /tmp/r5_w1_lease.log 2>/dev/null && break; sleep 0.5; done
grep -q acquired /tmp/r5_w1_lease.log || { echo LEASE_FAIL; exit 4; }
echo "[w1] lease ok $(date +%T) lock=$(cat /tmp/hpm5e31_hw.lock.d/what 2>/dev/null)"
flash() { HPM_HW_LOCK_HELD=1 timeout 60 bash scripts/flash.sh "$1" 2>&1 | grep -E 'Program & Verify|Error|FAILED' | tail -2; }
waitping() { for i in $(seq 1 16); do ping -c1 -W1 192.168.2.100 >/dev/null 2>&1 && { echo "[w1] ping ok"; return 0; }; sleep 0.5; done; echo "[w1] FAIL ping"; return 1; }

echo "########## A. someip_tp：主机侧 4096 B TP（默认 tpgap） ##########"
flash build/someip_tp-debug/output/demo.elf; sleep 0.3
if waitping; then timeout 45 bash host/someip/r5_tp_host.sh; fi
echo "########## A2. 板端 tpgap 2000 后再测 5 次 ##########"
if waitping; then
  timeout 10 python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 3 --send "tpgap 2000" --out /tmp/r5_tpgap.txt >/dev/null 2>&1 || true
  grep -a tpgap /tmp/r5_tpgap.txt | tail -2 || true
  timeout 40 bash host/someip/r5_tp_host.sh
fi
echo "########## B. someip_rpc：vsomeip 互操作 debug ##########"
flash build/someip_rpc-debug/output/demo.elf; sleep 0.3
if waitping; then timeout 45 bash host/someip/r5_vsomeip_debug.sh; fi
kill $LEASE 2>/dev/null; wait $LEASE 2>/dev/null
echo "[w1] done $(date +%T) out=$OUT"
