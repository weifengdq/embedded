#!/usr/bin/env bash
# run_irq_matrix.sh —— eth_irq 中断/轮询对照矩阵：一次持锁跑完 空闲/1000fps/20kfps/全速灌包
#
# 为什么不用 hwrun.sh：本矩阵需要"主机灌包"与"板端 irq rx 窗口"严格重叠，且一次烧写后
# 要连续跑 4 组场景（hwrun 一条 --send 串行发命令会因 irq rx 阻塞而丢后续命令）。
# 本脚本：抢整机锁 -> 烧写 -> banner 自证 -> 逐场景起主机 flood / 逐步发命令 / 抓串口
#          -> 归档 板端+主机 合并日志。
#
# 用法:
#   bash samples/mcal/eth_irq/host/run_irq_matrix.sh [--iface enp6s0] [--len 256]
#        [--lock-wait 1800] [--no-flash]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$here/../../../.." && pwd)"
source "$REPO/scripts/env.sh"

NAME=eth_irq
IFACE=enp6s0
LEN=256
LOCKWAIT=1800
DOFLASH=1
while [ $# -gt 0 ]; do
  case "$1" in
    --iface) IFACE="$2"; shift 2;;
    --len) LEN="$2"; shift 2;;
    --lock-wait) LOCKWAIT="$2"; shift 2;;
    --no-flash) DOFLASH=0; shift;;
    -*) echo "unknown opt $1"; exit 2;;
    *) echo "unknown arg $1"; exit 2;;
  esac
done
PID=$$
ELF="$(ls "$REPO/build/$NAME-debug"/output/*.elf | head -1)"
HOSTPY="$REPO/samples/mcal/eth_raw_echo/host/eth_host.py"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$REPO/logs/${TS}-${NAME}-host.log"

source "$REPO/scripts/hwlock.sh"
HPM_HW_LOCK_NAME="hwruntest:$NAME"
hw_lock_acquire "$LOCKWAIT" || { echo "ERROR: 等板子锁超时(${LOCKWAIT}s)"; exit 4; }
export HPM_HW_LOCK_HELD=1
trap 'hw_lock_release' EXIT INT TERM

# 烧写 + banner 自证（J-Link 偶发 "Verify failed" 但仍返回 0，必须看输出+串口双重确认；
# 实测 2026-11-02 18:2x 出现过"烧写返回 0 但板内还是旧固件"的情况，这里最多重试 3 次）
flash_ok=0
for try in 1 2 3; do
  if [ "$DOFLASH" = 1 ]; then
    fout="$(bash "$REPO/scripts/flash.sh" "$ELF" 2>&1)" || { echo "[matrix] flash try $try: 命令失败"; continue; }
    { echo "===== FLASH try $try ====="; echo "$fout"; } >> "$LOG"
    if echo "$fout" | grep -qE 'Verify failed|Error while programming|Cannot|FAILED'; then
      echo "[matrix] flash try $try: 输出有错误，重试"; sleep 1; continue
    fi
  fi
  sleep 0.3
  python3 "$REPO/scripts/serial_cap.py" --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" \
          --secs 2 --out "/tmp/irq_banner_$PID.log" >/dev/null 2>&1 || true
  { echo "===== BANNER try $try ====="; cat "/tmp/irq_banner_$PID.log"; } >> "$LOG"
  if grep -qE '=== eth_irq|RQ mode=|IRQ mode=' "/tmp/irq_banner_$PID.log"; then
    flash_ok=1; break
  fi
  echo "[matrix] banner try $try: 不是 eth_irq，重试"
done
if [ "$flash_ok" != 1 ]; then
  echo "ERROR: 连续 3 次烧写/自证失败，放弃本次数据"; cat "/tmp/irq_banner_$PID.log"; exit 6
fi



cap() {  # $1=抓取秒数 $2=发送命令 $3=输出文件
  python3 "$REPO/scripts/serial_cap.py" --port "$SERIAL_PORT" --baud "$SERIAL_BAUD" \
          --secs "$1" --send-delay 0.8 --send "$2" --out "$3" >/dev/null 2>&1 || true
}

LAST_HP=""
host_flood() {  # $1=rate(fps,0=全速) $2=主机秒数 $3=标签；后台 PID 放 LAST_HP
  local hl="/tmp/host_irq_$3_$PID.log"
  ( sleep 0.3; sudo -n python3 "$HOSTPY" --iface "$IFACE" --len "$LEN" --secs "$2" \
      --rate "$1" flood ) >"$hl" 2>&1 &
  LAST_HP=$!
}

merge() {  # $1=标签 $2=主机日志 $3..=板端日志
  local tag="$1" hl="$2"; shift 2
  { echo "===== [$tag] BOARD ====="; cat "$@"; echo "===== [$tag] HOST ====="; cat "$hl" 2>/dev/null || true; } >> "$LOG"
}

check_alive() {  # $1=标签 $2=本场景板端日志...；缺 IRQX/IRQ mode 说明板端卡死，提前收手
  local tag="$1"; shift
  if ! grep -q 'IRQX \|IRQ mode=' "$@"; then
    echo "ERROR: [$tag] 板端没有统计输出（可能卡死），提前结束本次持锁"
    return 1
  fi
  return 0
}

echo "[matrix] lock acquired, start"

# ---------- 场景 1：空闲（无主机流量），看纯轮询空转 vs 中断待命 ----------
cap 4.5 "irq off;irq rx 3" "/tmp/b_idle_p_$PID.log"
cap 4.5 "irq on;irq rx 3"  "/tmp/b_idle_i_$PID.log"
merge idle /dev/null "/tmp/b_idle_p_$PID.log" "/tmp/b_idle_i_$PID.log"
check_alive idle "/tmp/b_idle_p_$PID.log" "/tmp/b_idle_i_$PID.log" || { hw_lock_release; exit 7; }
echo "[matrix] idle done"

# ---------- 场景 2：低速 1000 帧/s ----------
host_flood 1000 12 low
cap 5.6 "irq off;irq rx 4" "/tmp/b_low_p_$PID.log"
cap 5.6 "irq on;irq rx 4"  "/tmp/b_low_i_$PID.log"
wait "$LAST_HP" 2>/dev/null || true
merge low "/tmp/host_irq_low_$PID.log" "/tmp/b_low_p_$PID.log" "/tmp/b_low_i_$PID.log"
check_alive low "/tmp/b_low_p_$PID.log" "/tmp/b_low_i_$PID.log" || { hw_lock_release; exit 7; }
echo "[matrix] low done"

# ---------- 场景 3：中速 20k 帧/s（中断模式单点，补延迟-速率曲线） ----------
host_flood 20000 7 mid
cap 5.6 "irq on;irq rx 4" "/tmp/b_mid_i_$PID.log"
wait "$LAST_HP" 2>/dev/null || true
merge mid "/tmp/host_irq_mid_$PID.log" "/tmp/b_mid_i_$PID.log"
check_alive mid "/tmp/b_mid_i_$PID.log" || { hw_lock_release; exit 7; }
echo "[matrix] mid done"

# ---------- 场景 4：全速灌包（主机 flood 不限速） ----------
host_flood 0 12 high
cap 5.6 "irq off;irq rx 4" "/tmp/b_high_p_$PID.log"
cap 5.6 "irq on;irq rx 4"  "/tmp/b_high_i_$PID.log"
wait "$LAST_HP" 2>/dev/null || true
merge high "/tmp/host_irq_high_$PID.log" "/tmp/b_high_p_$PID.log" "/tmp/b_high_i_$PID.log"
check_alive high "/tmp/b_high_p_$PID.log" "/tmp/b_high_i_$PID.log" || { hw_lock_release; exit 7; }
echo "[matrix] high done"

cp -f "$LOG" "$REPO/logs/.last_${NAME}"
hw_lock_release
echo "---- LOG: $LOG ----"
grep -E 'IRQ mode=|IRQX |HOST ' "$LOG" || true