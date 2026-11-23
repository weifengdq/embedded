#!/usr/bin/env bash
# repro_tx_urb_leak.sh —— TX URB 泄漏复现/验证（ch42 §42.9 坑 20 的 A/B 证据脚本）
#
# 背景：修复前固件在 drain_host_tx() 里 app_can_send() 失败时丢帧且不回 echo，
#       内核 gs_usb 每个 TX URB 只能靠 echo 回收（GS_MAX_TX_URBS=10），漏满 10 个后
#       netdev TX 队列永久停（cansend 报 "No buffer space available"、tx_packets 不再涨）。
#
# 用法：
#   bash host/gs_usb/repro_tx_urb_leak.sh --expect-stall   # 烧旧固件：期望复现卡死
#   bash host/gs_usb/repro_tx_urb_leak.sh --expect-ok      # 烧修复后固件：期望不卡死
#
# 做法：1M/2M FD 64B、cangen -g 0 满速发 N 帧；再单独 cansend 一帧，看是否 ENOBUFS；
#       同时记录 tx/rx 增量、ip -s -d 的 dropped/errors、berr-counter、dmesg 新增行。
set -u
. "$(dirname "$0")/common.sh"
MODE="${1:---expect-ok}"
N="${N:-3000}"

gs_begin "tx-urb-leak"
gs_require_ifaces 2 || { gs_end; exit $?; }
stat_of() { cat "/sys/class/net/$1/statistics/$2" 2>/dev/null || echo 0; }
berr_of() { ip -details link show "$1" 2>/dev/null | grep -oE 'berr-counter tx [0-9]+ rx [0-9]+' | head -1; }
cstate_of() { ip -details link show "$1" 2>/dev/null | grep -oE 'state [A-Z-]+' | tail -1; }

gs_step "1. 配置 1M/2M FD 64B（can0 -> can1），先用小批量确认链路通"
gs_can_down 0; gs_can_down 1
$GS_SUDO ip link set can0 type can bitrate 1000000 dbitrate 2000000 fd on loopback off listen-only off one-shot off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can1 type can bitrate 1000000 dbitrate 2000000 fd on loopback off listen-only off one-shot off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can0 up; $GS_SUDO ip link set can1 up
gs_log "  can0: $(ip -details link show can0 | grep -oE 'bitrate [0-9]+|dbitrate [0-9]+|dsample-point [0-9.]+' | tr '\n' ' ')"
rm -f /tmp/repro_dump.txt
$GS_SUDO timeout 3 candump -t a -n 3 can1 > /tmp/repro_dump.txt 2>&1 &
sleep 0.3
$GS_SUDO timeout 5 cangen can0 -f -b -n 3 -L 64 >/dev/null 2>&1
sleep 0.5
gs_log "  预热收到 $(grep -c '' /tmp/repro_dump.txt) 帧（期望 3）"
if [ "$(grep -c '' /tmp/repro_dump.txt)" -lt 3 ]; then
    gs_fail "预热不足 3 帧，链路/配置有问题，A/B 结论无效"
    gs_end; exit 1
fi

gs_step "2. 满速发送 N=$N 帧（cangen -g 0 -f -b -L 64）"
tx0=$(stat_of can0 tx_packets); rx0=$(stat_of can1 rx_packets)
$GS_SUDO timeout -k 3 25 cangen can0 -f -b -g 0 -n "$N" -L 64 -p 100 -i >/dev/null 2>>"$GS_LOG"
rc=$?
sleep 1
tx1=$(stat_of can0 tx_packets); rx1=$(stat_of can1 rx_packets)
dtx=$((tx1-tx0)); drx=$((rx1-rx0))
gs_log "  cangen rc=$rc（137=被 SIGKILL，说明卡在满速发送里）tx_delta=$dtx rx_delta=$drx（期望 $N）"

gs_step "3. 关键判据：队列是否永久停 —— 再单独 cansend 一帧"
D64=$(python3 -c "print('11' * 64)")
$GS_SUDO timeout 3 cansend can0 "123##1$D64" >/tmp/repro_cansend.txt 2>&1
crc=$?
cmsg=$(tr -d '\r' < /tmp/repro_cansend.txt | head -2 | tr '\n' ' ')
gs_log "  cansend rc=$crc msg=$cmsg"
sleep 1
tx2=$(stat_of can0 tx_packets)
gs_log "  单帧后 can0 tx_packets: $tx1 -> $tx2（增长 1 = 队列活着）"
gs_log "  can0: $(cstate_of can0) $(berr_of can0) / can1: $(cstate_of can1) $(berr_of can1)"
gs_log "  can0 ip -s: $(ip -s -d link show can0 | tr -s ' ' | tr '\n' ' ')"

stalled=0
if [ "$crc" != 0 ]; then
    if printf '%s' "$cmsg" | grep -qi 'No buffer space'; then stalled=1; fi
fi
if [ "$dtx" -lt "$N" ]; then stalled=1; fi

if [ "$MODE" = "--expect-stall" ]; then
    if [ "$stalled" = 1 ]; then
        gs_pass "--expect-stall：复现成功（tx_delta=$dtx < $N，cansend rc=$crc「$cmsg」）——旧固件 TX 队列永久停"
    else
        gs_fail "--expect-stall：未复现（tx_delta=$dtx，cansend rc=$crc）——固件可能已含修复"
    fi
else
    if [ "$stalled" = 0 ] && [ "$dtx" -ge "$N" ]; then
        gs_pass "--expect-ok：$N 帧满速全部发出、cansend 正常（rc=0），TX 队列未停"
    else
        gs_fail "--expect-ok：仍卡死（tx_delta=$dtx，cansend rc=$crc「$cmsg」）"
    fi
fi
gs_end

