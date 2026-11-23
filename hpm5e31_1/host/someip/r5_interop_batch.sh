#!/usr/bin/env bash
# r5_interop_batch.sh -- R5 一次持锁内跑完：ping + 自写 Python 客户端 + vsomeip 互操作
# 用法: bash scripts/hwrun.sh app/someip_rpc --secs 10 --lock-wait 1800 \
#         --post "bash host/someip/r5_interop_batch.sh"
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TS=$(date +%Y%m%d-%H%M%S)
OUT="$here/../../logs/${TS}-host-someip-batch.log"
exec > >(tee -a "$OUT") 2>&1
IFACE="${IFACE:-enp6s0}"
echo "== [r5] $(date '+%F %T') 板端连通性"
ok=0
for i in $(seq 1 10); do
  if ping -c 1 -W 1 192.168.2.100 >/dev/null 2>&1; then ok=1; echo "PASS ping (第 $i 次)"; break; fi
  sleep 0.5
done
if [ "$ok" != 1 ]; then
  echo "FAIL ping 192.168.2.100"
  ip -4 -br addr show "$IFACE" || true
  ip neigh show 192.168.2.100 || true
fi
echo "== [r5] 自写 Python 客户端 core（发现/RPC/事件）"
timeout 45 python3 "$here/someip_client.py" --board 192.168.2.100 core 2>&1 | tail -55
echo "== [r5] vsomeip 3.7.6 互操作"
VSTIMEOUT=60 timeout 75 bash "$here/vsomeip_client/run_interop.sh" 2>&1 \
  | grep -E 'PASS|FAIL|AVAILABLE|GONE|RTT|事件订阅|汇总|error|Join|KPI|state =|服务发现' | tail -45
echo "== [r5] done $(date '+%F %T') 证据=$OUT"
