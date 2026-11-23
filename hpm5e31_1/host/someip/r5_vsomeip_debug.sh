#!/usr/bin/env bash
# r5_vsomeip_debug.sh -- vsomeip 收不到板端 Offer 的定位：debug 日志 + routing manager 清理
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-vsomeip-debug.log
exec > >(tee -a "$OUT") 2>&1
IFACE=${IFACE:-enp6s0}
ip route get 224.244.224.245 | head -1
echo '[vsd] 清理残留 routing manager（用 pgrep 取 PID，避免 pkill -f 自杀）'
for p in $(pgrep -x someip_client 2>/dev/null); do kill "$p"; done
for p in $(pgrep -f 'vsomeip-src/build/examples' 2>/dev/null); do kill "$p"; done
sleep 0.3; rm -f /tmp/vsomeip.lck /tmp/vsomeip-1200 /tmp/vsomeip-4444 2>/dev/null || true
sed 's/"level": "info"/"level": "debug"/' host/someip/vsomeip_client/config/someip_client.json > /tmp/someip_client_dbg.json
export VSOMEIP_CONFIGURATION=/tmp/someip_client_dbg.json
export VSOMEIP_APPLICATION_NAME=someip_client
export LD_LIBRARY_PATH=$HOME/tmp/tp-someip/vsomeip-install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
timeout 28 host/someip/vsomeip_client/build/someip_client > /tmp/vsomeip_dbg.out 2>&1; echo "client rc=$?"
echo '--- 关键行（服务发现/服务状态/SD 解析）'
grep -aE 'AVAIL|GONE|offer|Offer|OFFER|reboot|Reboot|ignore|malformed|error|warn|KPI|0x1234|service.*(found|lost|add|remove)' /tmp/vsomeip_dbg.out | head -60
echo '--- SD 报文行数'; grep -ac 'sd_' /tmp/vsomeip_dbg.out || true
echo '--- 汇总'; grep -aE 'PASS|FAIL|汇总' /tmp/vsomeip_dbg.out | tail -6
cp /tmp/vsomeip_dbg.out "$OUT.dbg" 2>/dev/null || true
echo "[vsd] done $(date +%T)"
