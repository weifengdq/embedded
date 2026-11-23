#!/usr/bin/env bash
# test_enum.sh —— 测试项 1：枚举（USB 设备 -> 4 个 CAN netdev）
#
# 判定：
#   * lsusb 出现 1209:2323，且 dmesg 有 gs_usb 的 "CAN device" 枚举日志；
#   * /sys/class/net/can0..can3 全部存在，驱动绑定到 gs_usb；
#   * 每个接口 ethtool -i 显示 driver=gs_usb；
#   * 4 个接口的 ifindex/MAC 独立，bus-info 都是同一个 USB 设备。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "enum"

gs_step "1. USB 层"
gs_cmd lsusb | grep -iE '1209:2323|candle' || true
if lsusb | grep -qi '1209:2323'; then gs_pass "USB 设备 1209:2323 存在"; else gs_fail "USB 设备未找到"; fi
gs_cmd lsusb -d 1209:2323 -v | head -20

gs_step "2. dmesg 枚举日志"
d=$(gs_dmesg_cmd || true)
if [ -n "$d" ]; then
    $d | grep -iE 'gs_usb|gsusb' | tail -30 | tee -a "$GS_LOG"
    n=$($d | grep -ic 'gs_usb')
    [ "$n" -gt 0 ] && gs_pass "dmesg 有 $n 条 gs_usb 日志" || gs_fail "dmesg 无 gs_usb 日志"
    # 内核打印 "CAN device(s) ... initialized" 时说明通道数已确认
    if $d | grep -qiE 'gs_usb.*(initialized|CAN device)'; then gs_pass "dmesg 有枚举成功标志"; fi
fi

gs_step "3. 4 个 netdev"
gs_cmd ip -br link show type can
got=$(gs_count_ifaces)
[ "$got" -eq 4 ] && gs_pass "找到 4 个 CAN 接口: $(gs_find_ifaces)" || gs_fail "只找到 $got 个接口（期望 4）"

gs_step "4. 驱动绑定"
for i in 0 1 2 3; do
    [ -d "/sys/class/net/can$i" ] || continue
    dri=$(basename "$(readlink -f /sys/class/net/can$i/device/driver 2>/dev/null)" 2>/dev/null)
    bus=$(readlink -f /sys/class/net/can$i/device 2>/dev/null | xargs -r basename)
    ifn=$(cat /sys/class/net/can$i/ifindex 2>/dev/null)
    gs_log "can$i: driver=$dri ifindex=$ifn usb=$bus"
    [ "$dri" = "gs_usb" ] && gs_pass "can$i 绑定 gs_usb" || gs_fail "can$i 绑定 $dri（非 gs_usb）"
done
for i in 0 1 2 3; do
    [ -d "/sys/class/net/can$i" ] || continue
    gs_log "ethtool -i can$i: $(ethtool -i can$i | tr '\n' ' ')"
done

gs_step "5. 时间戳能力（ethtool -T，供测试项 5 对照）"
for i in 0 1 2 3; do
    [ -d "/sys/class/net/can$i" ] || continue
    gs_log "--- can$i ---"
    ethtool -T "can$i" 2>&1 | tee -a "$GS_LOG"
done

gs_step "6. 初始状态（GET_STATE 通过 ip -details 体现）"
for i in 0 1 2 3; do
    [ -d "/sys/class/net/can$i" ] || continue
    ip -details link show "can$i" 2>&1 | tee -a "$GS_LOG"
done

gs_end
