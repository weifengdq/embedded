#!/usr/bin/env bash
# r5_loop.sh —— R5 长测循环：反复"租约窗口 + 采样"，用于 >1 h 分段长稳与时间戳漂移累积
#
# 用法（后台跑）：
#   nohup bash host/gs_usb/r5_loop.sh 8 >/tmp/r5_loop.log 2>&1 &
# 每个窗口：board_lease 110 s（最多等锁 120 s）→ r5_window.sh → 释放 → 休息 25 s。
# 结果：
#   /tmp/gs_usb_longstab.csv  每窗口一行（设备在线/丢帧/errors/dmesg 异常数）
#   /tmp/gs_usb_ts_drift.csv  每窗口一行（时间戳偏移中位数/段内 ppm）
set -u
cd "$(dirname "$0")/../.." || exit 1
N=6
if [ $# -ge 1 ]; then N="$1"; fi
SLEEP_BETWEEN=25
if [ -n "$SLEEP_BETWEEN_ENV" ]; then SLEEP_BETWEEN="$SLEEP_BETWEEN_ENV"; fi
STAB_SECS=10
TS_SECS=45
if [ -n "$STAB_SECS_ENV" ]; then STAB_SECS="$STAB_SECS_ENV"; fi
if [ -n "$TS_SECS_ENV" ]; then TS_SECS="$TS_SECS_ENV"; fi
echo "[loop] start $(date '+%F %T') windows=$N"
for i in $(seq 1 "$N"); do
    LLOG=/tmp/lease_win_$i.log
    : > "$LLOG"
    bash scripts/board_lease.sh 110 gs_usb >"$LLOG" 2>&1 &
    LPID=$!
    acquired=0
    for _k in $(seq 1 240); do
        if grep -q 'acquired' "$LLOG" 2>/dev/null; then acquired=1; break; fi
        if ! kill -0 "$LPID" 2>/dev/null; then break; fi
        sleep 0.5
    done
    if [ "$acquired" != 1 ]; then
        echo "[loop] window $i: 等锁失败，跳过"
        kill "$LPID" 2>/dev/null || true
        wait "$LPID" 2>/dev/null || true
        sleep 20
        continue
    fi
    echo "[loop] window $i start $(date +%T)"
    bash host/gs_usb/r5_window.sh --iter "$i" --stability-secs "$STAB_SECS" --ts-secs "$TS_SECS"
    kill "$LPID" 2>/dev/null || true
    wait "$LPID" 2>/dev/null || true
    echo "[loop] window $i done $(date +%T)"
    sleep "$SLEEP_BETWEEN"
done
echo "[loop] all done $(date '+%F %T')"
