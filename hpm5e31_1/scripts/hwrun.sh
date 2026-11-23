#!/usr/bin/env bash
# 一条龙硬件测试: 编译 -> 烧写 -> 抓串口 -> [可选 --post 上位机测试] -> 存档日志 (整机串行锁)
# 用法: scripts/hwrun.sh <sample> [--secs N] [--send "cmd1;cmd2"] [--expect "REGEX"]
#                       [--no-build] [--lock-wait SEC] [--post "host cmd"]
#   --post: 串口抓完后仍持锁执行一条上位机命令（例: 主机 candump/cansend、python 测试），
#           用于"烧写+上位机联调"原子化，避免固件被别的测试换掉；单次持锁建议 <=90s。
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/env.sh"
[ $# -ge 1 ] || { echo "usage: $0 <sample> [--secs N] [--send ..] [--expect ..] [--no-build] [--post ..]"; exit 1; }
SAMPLE="$1"; shift
SECS=5; SEND=""; EXPECT=""; DOBUILD=1; POST=""; DOFLASH=1
while [ $# -gt 0 ]; do
  case "$1" in
    --secs) SECS="$2"; shift 2;;
    --send) SEND="$2"; shift 2;;
    --expect) EXPECT="$2"; shift 2;;
    --no-build) DOBUILD=0; shift;;
    --lock-wait) LOCKWAIT="$2"; shift 2;;
    --post) POST="$2"; shift 2;;
    --no-flash) DOFLASH=0; shift;;
    *) shift;;
  esac
done
if [ -d "$SAMPLE" ]; then NAME="$(basename "$SAMPLE")"; SRC="$SAMPLE"; elif [ -d "$REPO_ROOT/samples/$SAMPLE" ]; then NAME="$(basename "$SAMPLE")"; SRC="$REPO_ROOT/samples/$SAMPLE"; else
  SRC="$(find "$REPO_ROOT/samples" -maxdepth 3 -type d -name "$SAMPLE" | head -1)"; NAME="$(basename "$SRC")"
fi
[ -d "$SRC" ] || { echo "sample not found: $SAMPLE"; exit 2; }
BD="$REPO_ROOT/build/$NAME-debug"
LASTRUN="$REPO_ROOT/logs/.last_$NAME"
# 整机串行: 编译在锁外进行, 烧写+串口(+post)在锁内
if [ "$DOBUILD" = 1 ]; then bash "$here/build.sh" "$SRC" >/tmp/build_$$.log 2>&1 || { tail -40 /tmp/build_$$.log; exit 1; }; fi
ELF="$(ls "$BD"/output/*.elf | head -1)"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$REPO_ROOT/logs/${TS}-${NAME}.log"
source "$here/hwlock.sh"
LOCKWAIT=${LOCKWAIT:-${HPM_LOCK_WAIT:-600}}
HPM_HW_LOCK_NAME="hwrun:$NAME"
if ! hw_lock_acquire "$LOCKWAIT"; then
  echo "ERROR: 等待板子锁超时(${LOCKWAIT}s)，可能有别人的长测试在跑，稍后重试" >&2
  exit 4
fi
echo "[hwrun] board lock acquired ($NAME) pid=$$"
export HPM_HW_LOCK_HELD=1
trap 'hw_lock_release' EXIT INT TERM
if [ "$DOFLASH" = 1 ]; then bash "$here/flash.sh" "$ELF"; else echo "[hwrun] --no-flash: 使用板内当前固件"; fi
sleep 0.3
ARGS=(--port "$SERIAL_PORT" --baud "$SERIAL_BAUD" --secs "$SECS" --out "$LOG")
[ -n "$SEND" ] && ARGS+=(--send "$SEND")
[ -n "$EXPECT" ] && ARGS+=(--expect "$EXPECT")
set +e
python3 "$here/serial_cap.py" "${ARGS[@]}"
RC=$?
set -e
if [ -n "$POST" ]; then
  echo "[hwrun] post (持锁运行上位机测试): $POST"
  set +e
  bash -c "$POST"
  echo "[hwrun] post rc=$?"
  set -e
fi
if [ -f "$LOG" ]; then cp -f "$LOG" "$LASTRUN"; fi
hw_lock_release
echo "---- LOG: $LOG (rc=$RC) ----"
exit 0
