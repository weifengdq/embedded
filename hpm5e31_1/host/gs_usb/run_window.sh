#!/usr/bin/env bash
# run_window.sh —— 一次性跑完剩余测试项（约 4 min，持锁不烧写）
#
# 背景：gs_usb 固件必须持续在线。别人一烧别的固件设备就消失，测试会全红（假 FAIL）。
# 流程：预检设备 -> 后台 hwrun --no-build --no-flash 持锁 -> 二次确认 -> 跑测试 -> 收尾释放。
#
# 用法：
#   bash host/gs_usb/run_window.sh              # 240 s 窗口，含 stress
#   bash host/gs_usb/run_window.sh 240 --no-stress
#
# 前置：板子上已烧好 gs_usb 固件且设备在线（本脚本不烧写）。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
cd "$REPO"
WINDOW=240
STRESS=1
for a in "$@"; do
    case "$a" in
        --no-stress) STRESS=0;;
        ''|*[!0-9]*) ;;
        *) WINDOW="$a";;
    esac
done
TS=$(date +%Y%m%d-%H%M%S)
OUT="$REPO/logs/$TS-gs-usb-window2.log"
HOLDLOG=/tmp/gs_usb_hold_window.log
mkdir -p "$REPO/logs"
: > "$OUT"
say() { echo "$*" | tee -a "$OUT"; }

find_dev() {
    local d
    for d in /sys/bus/usb/devices/*/; do
        [ -f "$d/idVendor" ] || continue
        if [ "$(cat $d/idVendor 2>/dev/null)" = "1209" ] && [ "$(cat $d/idProduct 2>/dev/null)" = "2323" ]; then
            basename "$d"; return 0
        fi
    done
    return 1
}

say "== gs_usb 窗口测试 $TS（窗口 $WINDOW s，stress=$STRESS）=="
DEV=$(find_dev || true)
NIF=$(ip -br link show type can 2>/dev/null | wc -l)
say "预检: 设备=${DEV:-无} 接口数=$NIF"
if [ -z "${DEV:-}" ] || [ "$NIF" -lt 4 ]; then
    say "中止: 设备/接口不满足（当前固件不是 gs_usb 或设备不在）"
    exit 2
fi

nohup bash scripts/hwrun.sh app/gs_usb_candle --no-build --no-flash --secs "$WINDOW" --lock-wait 900 > "$HOLDLOG" 2>&1 &
HOLD=$!
for i in $(seq 1 900); do grep -q 'board lock acquired' "$HOLDLOG" 2>/dev/null && break; sleep 1; done
if ! grep -q 'board lock acquired' "$HOLDLOG" 2>/dev/null; then
    say "中止: 等锁超时"
    kill "$HOLD" 2>/dev/null
    exit 3
fi
say "已持锁 pid=$HOLD（不烧写）"

DEV2=$(find_dev || true)
NIF2=$(ip -br link show type can 2>/dev/null | wc -l)
say "持锁后: 设备=${DEV2:-无} 接口数=$NIF2"
if [ -z "${DEV2:-}" ] || [ "$NIF2" -lt 4 ]; then
    say "中止: 持锁后设备不在（被别的烧写打断）"
    kill "$HOLD" 2>/dev/null
    exit 4
fi

run() {
    local n="$1"; shift
    say ""
    say "======== $n ========"
    local o rc
    o=$(timeout 200 "$@" 2>&1); rc=$?
    printf '%s\n' "$o" | tail -16 | tee -a "$OUT"
    printf '%s\n' "$o" >> "$OUT"
    say "[rc=$rc] $n"
}

run setup       bash "$HERE/setup.sh"
run timestamp   bash "$HERE/test_timestamp.sh"
run bench_enum  sudo -n $HOME/pyenv/bin/python "$HERE/bench_enum.py" --json
run bench_lat   sudo -n $HOME/pyenv/bin/python "$HERE/bench_latency.py" --json
run modes       bash "$HERE/test_modes.sh"
run termination bash "$HERE/test_termination.sh"
run compat      bash "$HERE/test_compat.sh"
if [ "$STRESS" = 1 ]; then run stress bash "$HERE/test_stress.sh"; fi

for i in 0 1 2 3; do sudo -n ip link set "can$i" down 2>/dev/null; done
kill "$HOLD" 2>/dev/null; wait "$HOLD" 2>/dev/null
say ""
say "== 汇总（$OUT）=="
grep -E 'GS_RESULT|verdict|netdev_ready|median_us|tx_fps' "$OUT" | tail -20 | tee -a "$OUT"
say "== 结束 $(date +%H:%M:%S) 锁=$(cat /tmp/hpm5e31_hw.lock.d/what 2>/dev/null || echo none) =="
