#!/usr/bin/env bash
# replay_check.sh —— canplayer 回放：vcan0 -> cannelloni -> 板端网关(CAN1) -> CAN0 can_logger -> 串口 verify
#
# 前提：板端已烧 samples/app/can_logger 并处于 "gw on" 模式（CAN1=网关，CAN0=记录）
# 用法（由 hwrun --post 调用，持锁执行）：
#   bash scripts/hwrun.sh app/can_logger --secs 10 --send "gw on" \
#        --post "timeout 80 bash host/canlog/replay_check.sh /tmp/replay.dump" --lock-wait 1800
set -uo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
REPO="$(cd ../.. && pwd)"
BR="$REPO/host/can_bridge"
FILE="${1:-/tmp/replay.dump}"

# 没有回放文件时，自动从最近的 can_logger 日志里抽取 candump 段，并把通道名改成 vcan0
if [ ! -f "$FILE" ]; then
  SRC="$(ls -t "$REPO"/logs/*can_logger*.log 2>/dev/null | head -1)"
  if [ -n "${SRC:-}" ]; then
    sed -n '/# candump:/,/# end candump/p' "$SRC" | grep -E '^\(' | sed 's/ can0 / vcan0 /' > "$FILE"
    echo "replay: 从 $SRC 生成 $FILE"
  fi
fi
if [ ! -f "$FILE" ]; then
  echo "replay: 缺少回放文件 $FILE（可传参指定 candump 文本）"; exit 2
fi
echo "[replay] 文件 $FILE（$(grep -cE '^\(' "$FILE") 帧）"

bash "$BR/vcan_setup.sh" >/dev/null 2>&1 || true
# 回放是突发流量，去掉 500k 限速（否则 canplayer 一次性灌入会被 tbf 丢弃）
sudo tc qdisc del dev vcan0 root 2>/dev/null || true

pkill -f "cannelloni -I vcan0" 2>/dev/null || true
sleep 0.3
if [ -x "$BR/build/cannelloni" ]; then
  "$BR/build/cannelloni" -I vcan0 -R 192.168.2.100 -r 4000 -l 4000 -t 1000 >/tmp/replay_cannelloni.log 2>&1 &
  CPID=$!
  sleep 0.8
else
  echo "replay: cannelloni 未构建"; exit 3
fi

echo "[replay] canplayer 回放中（-g 1 ms 间隔，避免网关突发丢帧）"
canplayer -I "$FILE" -g 1 >/tmp/replay_canplayer.log 2>&1
RC=$?
echo "[replay] canplayer rc=$RC"
sleep 1.5

kill "$CPID" 2>/dev/null
pkill -f "cannelloni -I vcan0" 2>/dev/null || true

# 板端校验：通过串口发 verify（HPM_HW_LOCK_HELD 由 hwrun 设置，serial_cap 不复用锁）
echo "[replay] 板端校验：log dump 40;verify"
"$PY" "$REPO/scripts/serial_cap.py" --port /dev/ttyACM0 --baud 921600 --secs 8 \
      --send "log dump 40;verify;stat" --out /tmp/replay_board.log
RC2=$?
echo "[replay] 板端日志 /tmp/replay_board.log rc=$RC2"
grep -E "verify:|bad_order|replay:" /tmp/replay_board.log 2>/dev/null | tail -5
grep -q "result=PASS" /tmp/replay_board.log && echo "[replay] RESULT: PASS" || echo "[replay] RESULT: FAIL"
[ "$RC2" = "0" ] && exit 0 || exit 1
