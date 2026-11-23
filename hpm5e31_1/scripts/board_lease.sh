#!/usr/bin/env bash
# scripts/board_lease.sh —— 板卡"租约"：持锁 N 秒，供需要长期独占板卡/USB 设备的测试使用
#
# 背景：hwrun.sh 的锁只在"烧写+抓串口"期间持有；gs_usb 这类"设备必须一直在 USB 上"的测试，
#       如果别人中途 hwrun 烧写，板子会被复位、USB 设备掉线，表现为"固件崩溃"，极易误判。
#
# 用法：
#   bash scripts/board_lease.sh <秒数> [名字]          # 前台持锁（Ctrl-C 释放）
#   nohup bash scripts/board_lease.sh 300 gs_usb >/tmp/lease.log 2>&1 &   # 后台持锁
#   cat /tmp/hpm5e31_hw.lock.d/what                                       # 看当前占用者
#   kill <pid>                                                            # 提前释放
#
# 注意：租约期间其它成员的 hwrun 会排队等待；租约结束自动释放。
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/env.sh"
source "$here/hwlock.sh"
SECS="${1:-120}"
NAME="${2:-manual}"
HPM_HW_LOCK_NAME="lease:${NAME}"
hw_lock_acquire 1800 || { echo "[lease] 等锁超时" >&2; exit 4; }
echo "[lease] acquired (${NAME}) for ${SECS}s pid=$$ since=$(date '+%F %T')"
trap 'hw_lock_release; echo "[lease] released $(date +%T)"' EXIT INT TERM
sleep "$SECS"
