#!/usr/bin/env bash
# run_host_test.sh —— "板卡 + 主机对打"一站式：抢板锁 -> 烧写 -> 起主机 AF_PACKET 脚本
#                     -> 串口发命令 -> 主机脚本退出 -> 归档"板端+主机"合并日志
#
# 为什么需要它：hwrun.sh 的 --send 命令在串口打开后 0.3 s 就发出，而主机侧对打脚本必须
# 与"板端执行命令"的时间窗重叠；用 --post 又跑到串口抓取之后。本脚本把两者放在同一
# 次持锁窗口里，并按 "先起主机、再发命令" 的顺序对齐。
#
# 用法:
#   bash run_host_test.sh <sample> [--host-mode echo|flood|listen|rtt] [--host-secs N]
#                        [--iface enp6s0] [--len 64] [--n 500]
#                        [--secs 30] [--send "cmd1;cmd2"] [--lock-wait 1800] [--no-flash]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$here/../../../.." && pwd)"
source "$REPO/scripts/env.sh"

SAMPLE="eth_raw_echo"; HOST_MODE="echo"; HOST_SECS=30; IFACE="enp6s0"
LEN=64; N=500; SECS=30; SEND=""; LOCKWAIT=1800; DOFLASH=1; HOST_DELAY=0.2
while [ $# -gt 0 ]; do
  case "$1" in
    --host-mode) HOST_MODE="$2"; shift 2;;
    --host-secs) HOST_SECS="$2"; shift 2;;
    --iface) IFACE="$2"; shift 2;;
    --len) LEN="$2"; shift 2;;
    --n) N="$2"; shift 2;;
    --secs) SECS="$2"; shift 2;;
    --send) SEND="$2"; shift 2;;
    --lock-wait) LOCKWAIT="$2"; shift 2;;
    --no-flash) DOFLASH=0; shift;;
    -*) echo "unknown opt $1"; exit 2;;
    *) SAMPLE="$1"; shift;;
  esac
done
SRC="$REPO/samples/mcal/$SAMPLE"
[ -d "$SRC" ] || SRC="$(find "$REPO/samples" -maxdepth 3 -type d -name "$SAMPLE" | head -1)"
NAME="$(basename "$SRC")"
ELF="$(ls "$REPO/build/$NAME-debug"/output/*.elf | head -1)"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$REPO/logs/${TS}-${NAME}-host.log"
HOSTLOG="/tmp/host_${NAME}_$$.log"

source "$REPO/scripts/hwlock.sh"
HPM_HW_LOCK_NAME="hwruntest:$NAME"
hw_lock_acquire "$LOCKWAIT" || { echo "ERROR: 等锁超时"; exit 4; }
export HPM_HW_LOCK_HELD=1
trap 'hw_lock_release' EXIT INT TERM

if [ "$DOFLASH" = 1 ]; then bash "$REPO/scripts/flash.sh" "$ELF" >/dev/null 2>&1 || { echo "flash failed"; exit 5; }; fi
sleep 0.3

# 主机脚本：延迟 HOST_DELAY 秒启动，保证板端已经启动完成、命令即将发出
( sleep "$HOST_DELAY"; sudo -n python3 "$here/eth_host.py" --iface "$IFACE" --len "$LEN" --n "$N" \
    --secs "$HOST_SECS" "$HOST_MODE" ) >"$HOSTLOG" 2>&1 &
HOSTPID=$!

python3 "$REPO/scripts/serial_cap.py" --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" \
        --secs "$SECS" --out "$LOG" ${SEND:+--send "$SEND"} || true

wait "$HOSTPID" 2>/dev/null || true
{ echo "===== HOST SIDE ====="; cat "$HOSTLOG"; } >>"$LOG"
cp -f "$LOG" "$REPO/logs/.last_${NAME}"
hw_lock_release
echo "---- LOG: $LOG ----"
grep -E 'HOST |RTT |TX |RX |LINK |TRCV ' "$LOG" || true
