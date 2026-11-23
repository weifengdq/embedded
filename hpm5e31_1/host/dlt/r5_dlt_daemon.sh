#!/usr/bin/env bash
# r5_dlt_daemon.sh -- 板端 dlt_log 直连 dlt-daemon（UDP 3490）：dlt-receive + dlt-convert 验证
set -uo pipefail
cd hpm5e31_1
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-dlt-daemon.log
exec > >(tee -a "$OUT") 2>&1
echo "[dlt] $(date +%T) daemon: $(pgrep -a dlt-daemon | head -1)"
if ! pgrep -x dlt-daemon >/dev/null; then
  sudo mkdir -p /tmp/dlt/dltpipes && sudo dlt-daemon -d -t /tmp/dlt/dltpipes -p 3490 -c /dev/null; sleep 1
fi
pkill -x dlt-receive 2>/dev/null || true
rm -f /tmp/r5_dlt.dlt /tmp/r5_dlt_recv.txt
timeout 20 dlt-receive -a localhost -o /tmp/r5_dlt.dlt > /tmp/r5_dlt_recv.txt 2>&1 &
RX=$!
sleep 1; ss -lunp 2>/dev/null | grep 3490 | head -2
wait $RX 2>/dev/null || true
echo '--- dlt-receive 文本行数'; wc -l < /tmp/r5_dlt_recv.txt
echo '--- 前 3 条'; head -3 /tmp/r5_dlt_recv.txt
echo '--- APID 直方图'; grep -oE 'HPM5' /tmp/r5_dlt_recv.txt | wc -l
echo '--- dlt-convert（官方工具）解析 .dlt：'; dlt-convert -a /tmp/r5_dlt.dlt 2>&1 | head -8
echo '--- dlt-convert 行数'; dlt-convert -a /tmp/r5_dlt.dlt 2>/dev/null | wc -l
cp /tmp/r5_dlt.dlt "logs/${TS}-dlt-daemon.dlt" 2>/dev/null || true
echo "[dlt] done $(date +%T) out=$OUT"
