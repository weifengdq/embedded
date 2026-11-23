#!/usr/bin/env bash
# r5_board_someip_all.sh -- R5 第 35 章一次持租约内完成：
#   A) someip_rpc：自写 Python 客户端（发现/RPC/事件）+ vsomeip 3.7.6 互操作
#   B) someip_tp ：板端自测 + 主机侧 4096 B 双向 TP（tcpdump 抓 3 段）
# 用法: bash host/someip/r5_board_someip_all.sh
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S)
OUT=logs/${TS}-host-r5-someip-all.log
exec > >(tee -a "$OUT") 2>&1

lease_up() {
  for i in $(seq 1 3600); do
    grep -q 'acquired' /tmp/r5_lease_someip.log 2>/dev/null && return 0
    sleep 0.5
  done
  return 1
}
echo "[r5] $(date '+%T') 申请板卡租约 300 s"
: > /tmp/r5_lease_someip.log
bash scripts/board_lease.sh 300 r5-someip > /tmp/r5_lease_someip.log 2>&1 &
LEASE=$!
lease_up || { echo "[r5] FAIL 租约超时"; exit 4; }
echo "[r5] 租约已持有: $(cat /tmp/hpm5e31_hw.lock.d/what 2>/dev/null)"

flash_app() {   # $1=elf  $2=名字
  echo "[r5] 烧写 $2"
  HPM_HW_LOCK_HELD=1 timeout 90 bash scripts/flash.sh "$1" 2>&1 | grep -E "Programming|Verifying|Flash download|Error|FAILED" | tail -3
}

wait_ping() {
  for i in $(seq 1 24); do
    if ping -c 1 -W 1 192.168.2.100 >/dev/null 2>&1; then echo "[r5] ping 192.168.2.100 OK (第 $i 次)"; return 0; fi
    sleep 0.5
  done
  echo "[r5] FAIL ping 192.168.2.100（板子没跑我们烧的固件？）"
  ip neigh show 192.168.2.100
  return 1
}

# ---------------- A) someip_rpc ----------------
flash_app build/someip_rpc-debug/output/demo.elf someip_rpc
sleep 0.3
python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 50 --out "logs/${TS}-someip_rpc.log" >/dev/null 2>&1 &
CAP=$!
sleep 4
if wait_ping; then
  echo "===== [A1] 自写 Python 客户端 core ====="
  timeout 45 python3 host/someip/someip_client.py --board 192.168.2.100 core 2>&1 | tail -45
  echo "===== [A2] vsomeip 3.7.6 互操作 ====="
  VSTIMEOUT=60 timeout 75 bash host/someip/vsomeip_client/run_interop.sh 2>&1 \
    | grep -E 'PASS|FAIL|AVAILABLE|GONE|RTT n|事件订阅|汇总|error|Join|KPI|state =|服务发现' | tail -40
fi
kill $CAP 2>/dev/null; wait $CAP 2>/dev/null
echo "[r5] A 阶段板端串口: logs/${TS}-someip_rpc.log"

# ---------------- B) someip_tp ----------------
flash_app build/someip_tp-debug/output/demo.elf someip_tp
sleep 0.3
python3 scripts/serial_cap.py --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs 22 --send "run" --out "logs/${TS}-someip_tp.log" >/dev/null 2>&1 &
CAP=$!
sleep 4
if wait_ping; then
  echo "===== [B] 主机侧 SOME/IP-TP 4096 B（tcpdump 抓段） ====="
  sudo -n timeout 25 tcpdump -i enp6s0 -nn -s0 'udp and port 30502' -w /tmp/r5_tp.pcap >/tmp/r5_tcpdump.log 2>&1 &
  TD=$!
  sleep 1
  timeout 40 python3 host/someip/someip_client.py --board 192.168.2.100 tp --size 4096 2>&1 | tail -25
  kill -$TD 2>/dev/null; wait $TD 2>/dev/null
  echo "[r5] tcpdump 段统计:"
  sudo -n tcpdump -nn -r /tmp/r5_tp.pcap 2>/dev/null | grep -c 'length' || true
  sudo -n tcpdump -nn -r /tmp/r5_tp.pcap 2>/dev/null | head -12
  cp /tmp/r5_tp.pcap "logs/${TS}-someip-tp.pcap" 2>/dev/null || true
fi
kill $CAP 2>/dev/null; wait $CAP 2>/dev/null
echo "[r5] B 阶段板端串口: logs/${TS}-someip_tp.log"

# ---------------- 收尾 ----------------
kill $LEASE 2>/dev/null; wait $LEASE 2>/dev/null
echo "[r5] $(date '+%T') 租约已释放; 证据日志 $OUT"
grep -E 'IPv4|reconnects|PASS|FAIL|AVAIL' logs/${TS}-someip_rpc.log logs/${TS}-someip_tp.log 2>/dev/null | tail -12
