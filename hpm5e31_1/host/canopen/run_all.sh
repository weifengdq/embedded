#!/usr/bin/env bash
# 一键跑完三个 CANopen 上位机测试（Bench B）
#
# 前提：板端固件已执行 'gw on'（CAN1 <-> UDP4000 <-> 主机 vcan0），CAN0 为 CANopen 节点 1。
# 典型用法（由 hwrun --post 调用，持锁执行）：
#   bash scripts/hwrun.sh app/canopen_node --secs 15 --send "gw on" \
#        --post "timeout 70 bash host/canopen/run_all.sh" --lock-wait 1800
set -uo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
REPO="$(cd ../.. && pwd)"
BR="$REPO/host/can_bridge"

# 1) vcan0 + 限速（幂等）
bash "$BR/vcan_setup.sh" >/dev/null 2>&1 || true

# 2) 起 cannelloni（延迟模式 -t 1000us，避免默认 100 ms 抖动）
pkill -f "cannelloni -I vcan0" 2>/dev/null || true
sleep 0.3
if [ -x "$BR/build/cannelloni" ]; then
  "$BR/build/cannelloni" -I vcan0 -R 192.168.2.100 -r 4000 -l 4000 -t 1000 >/tmp/co_cannelloni.log 2>&1 &
  CPID=$!
  sleep 0.8
else
  echo "WARN: $BR/build/cannelloni 不存在，跳过网关（只做脚本自检）"
  CPID=0
fi

rc=0
for s in co_node_test.py co_pdo_bench.py co_sdo_seg.py; do
  echo "===== $s ====="
  timeout 28 "$PY" "$s" || { echo "---- $s rc=$? ----"; rc=1; }
done

[ "$CPID" != "0" ] && kill "$CPID" 2>/dev/null
pkill -f "cannelloni -I vcan0" 2>/dev/null || true
exit "$rc"
