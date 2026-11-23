#!/usr/bin/env bash
# r5_window.sh —— R5 单窗口采样：长稳（设备在线/丢帧/berr）+ 时间戳绝对漂移分段
#
# 由 r5_loop.sh 在持租约的窗口里调用。每个窗口：
#   1) USB 设备与 can0..can3 在线检查 + dmesg 新增行扫描（掉线/oops）
#   2) 经典 500k、10 s、1000 帧/s 流量：can0 -> can1，算丢帧与 errors 增量
#   3) 时间戳漂移采样：bench_ts_drift.py --secs 45，追加到 CSV
#   4) 结果追加到长稳 CSV（/tmp/gs_usb_longstab.csv）
#
# 用法：bash host/gs_usb/r5_window.sh [--iter N] [--stability-secs 10] [--ts-secs 45]
set -u
. "$(dirname "$0")/common.sh"
ITER=0
STAB_SECS=10
TS_SECS=45
while [ $# -gt 0 ]; do
    case "$1" in
        --iter) ITER="$2"; shift 2;;
        --stability-secs) STAB_SECS="$2"; shift 2;;
        --ts-secs) TS_SECS="$2"; shift 2;;
        *) shift;;
    esac
done
if [ -z "$STAB_CSV" ]; then STAB_CSV=/tmp/gs_usb_longstab.csv; fi
if [ -z "$TS_CSV" ]; then TS_CSV=/tmp/gs_usb_ts_drift.csv; fi
if [ -z "$WLOG" ]; then WLOG=/tmp/gs_usb_window_$ITER.log; fi
: > "$WLOG"

stat_of() { cat "/sys/class/net/$1/statistics/$2" 2>/dev/null || echo 0; }
logw() { echo "$*" | tee -a "$WLOG"; }

dev=$(bash -c 'for d in /sys/bus/usb/devices/*/; do [ -f "$d/idVendor" ] || continue; v=$(cat "$d/idVendor" 2>/dev/null); p=$(cat "$d/idProduct" 2>/dev/null); if [ "$v" = 1209 ] && [ "$p" = 2323 ]; then basename "$d"; break; fi; done')
nif=0
for i in 0 1 2 3; do [ -d "/sys/class/net/can$i" ] && nif=$((nif + 1)); done
logw "[win $ITER] $(date +%T) usb_dev=$dev can_ifaces=$nif"
DM_BAD=0
DM_NEW=0
if sudo -n dmesg >/dev/null 2>&1; then
    DM_NEW=$(sudo -n dmesg | tail -n 400 | grep -cE 'gs_usb|usb 1-|can[0-3]' || true)
    DM_BAD=$(sudo -n dmesg | tail -n 400 | grep -cEi 'Oops|BUG:|WARNING:|Call Trace|RCU stall' || true)
fi

if [ -z "$dev" ] || [ "$nif" -lt 4 ]; then
    logw "[win $ITER] [FAIL] 设备/接口不在线，跳过本窗口"
    echo "$(date +%s),$ITER,0,$nif,0,0,0,0,0,0,0,$DM_BAD" >> "$STAB_CSV"
    exit 1
fi

for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null || true; done
$GS_SUDO ip link set can0 type can bitrate 500000 fd off loopback off listen-only off one-shot off >/dev/null 2>&1
$GS_SUDO ip link set can0 up >/dev/null 2>&1
$GS_SUDO ip link set can1 type can bitrate 500000 fd off loopback off listen-only off one-shot off >/dev/null 2>&1
$GS_SUDO ip link set can1 up >/dev/null 2>&1
sleep 0.2
tx0=$(stat_of can0 tx_packets); rx0=$(stat_of can1 rx_packets)
te0=$(stat_of can0 tx_errors);  re0=$(stat_of can1 rx_errors)
timeout "$STAB_SECS" $GS_SUDO cangen can0 -g 1 -L 8 >/dev/null 2>&1 || true
tx1=$(stat_of can0 tx_packets); rx1=$(stat_of can1 rx_packets)
te1=$(stat_of can0 tx_errors);  re1=$(stat_of can1 rx_errors)
TX=$((tx1 - tx0)); RX=$((rx1 - rx0))
TE=$((te1 - te0)); RE=$((re1 - re0))
LOSS=$(python3 -c "print(round((1-$RX/$TX)*100,2) if $TX>0 else 100.0)")
B0=$(ip -details link show can0 2>/dev/null | grep -oE 'berr-counter tx [0-9]+ rx [0-9]+' | head -1)
B1=$(ip -details link show can1 2>/dev/null | grep -oE 'berr-counter tx [0-9]+ rx [0-9]+' | head -1)
logw "[win $ITER] stab: tx=$TX rx=$RX loss=$LOSS% tx_err=$TE rx_err=$RE | can0 $B0 | can1 $B1"
echo "$(date +%s),$ITER,1,$nif,$TX,$RX,$LOSS,$TE,$RE,$DM_NEW,$DM_BAD,0" >> "$STAB_CSV"

logw "[win $ITER] ts_drift $TS_SECS s ..."
python3 "$(dirname "$0")/bench_ts_drift.py" --secs "$TS_SECS" --no-config --append "$TS_CSV" 2>&1 | tee -a "$WLOG"

for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null || true; done
logw "[win $ITER] done $(date +%T)"
