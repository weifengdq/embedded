#!/usr/bin/env bash
# setup.sh —— gs_usb 测试环境预检 + 设备枚举（一次跑完，不改系统配置）
#
# 做的事：
#   1) 检查内核版本 / gs_usb.ko / can-utils / python-can / iproute2 / ethtool；
#   2) 检查 USB 上是否有 1209:2323（candleLight）；
#   3) gs_usb 模块未加载则 modprobe（失败不致命，打印原因）；
#   4) 等 can0..can3 出现（默认最多等 10 s），打印 dmesg 里 gs_usb 的枚举日志；
#   5) 生成环境快照，供文档引用。
#
# 不需要 root；只有 modprobe 与 dmesg 可能需要（脚本用 sudo -n 兜底，失败即降级为提示）。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "setup"

gs_step "1. 内核与模块"
gs_cmd uname -srm
gs_log "内核版本: $(uname -r)"
if uname -r | grep -q '^7\.0\.'; then gs_pass "内核 7.0.x"; else gs_fail "内核非 7.0.x（约定要求 7.0.x）"; fi
if [ -f "/lib/modules/$(uname -r)/kernel/drivers/net/can/usb/gs_usb.ko.zst" ] || \
   [ -f "/lib/modules/$(uname -r)/kernel/drivers/net/can/usb/gs_usb.ko" ]; then
    gs_pass "gs_usb.ko 存在"
else
    gs_fail "找不到 gs_usb.ko（内核未编译该驱动）"
fi

gs_step "2. 工具链"
gs_cmd command -v candump cansend cangen canplayer canfdtest ip ethtool python3
for t in candump cansend cangen canplayer ip ethtool; do
    command -v "$t" >/dev/null && gs_pass "命令 $t: $(command -v $t)" || gs_fail "缺少 $t"
done
gs_cmd ip -V
gs_cmd ethtool --version
if $HOME/pyenv/bin/python -c 'import can;print("python-can", can.__version__)' 2>/dev/null; then
    gs_pass "python-can 可用"
else
    gs_skip "pyenv 里没有 python-can（不影响 shell 测试）"
fi
if ip link help can 2>&1 | grep -q 'termination'; then
    gs_pass "iproute2 支持 termination 属性（可测'应不支持'）"
else
    gs_fail "当前 iproute2 不认识 termination 关键字，test_termination 无法执行"
fi

gs_step "3. USB 设备（VID:PID 1209:2323）"
gs_cmd lsusb
# 以 sysfs 为准：lsusb 在 AppArmor 下可能读不到部分 uevent 而误报
if gs_wait_usb 5; then
    DEV=$(gs_usb_present)
    gs_pass "找到 gs_usb/candleLight 设备 1209:2323（sysfs: $DEV）"
else
    gs_fail "未找到 1209:2323 —— gs_usb 固件未运行或未插 USB（板上 Pin67/68）"
    gs_log "提示：烧写 samples/app/gs_usb_candle 固件后用 hwrun 保持运行，再跑本脚本"
    gs_log "     （sysfs 扫描 /sys/bus/usb/devices/*/idVendor=1209 & idProduct=2323）"
fi

gs_step "3.5 USB autosuspend（必需！否则空闲 ~85 s 被主机挂起->detach，会误判成设备掉线）"
GS_SUDO sh -c 'echo -1 > /sys/module/usbcore/parameters/autosuspend' 2>/dev/null && \
    gs_pass "usbcore autosuspend=-1（禁用全局自动挂起）" || gs_fail "无法设置 /sys/module/usbcore/parameters/autosuspend"
AUTO=$(cat /sys/module/usbcore/parameters/autosuspend 2>/dev/null)
gs_log "usbcore/parameters/autosuspend = ${AUTO:-?}（-1 = 禁用）"
PORT=$(gs_usb_present)
if [ -n "$PORT" ]; then
    ORIG=$(cat "/sys/bus/usb/devices/$PORT/power/control" 2>/dev/null || echo unknown)
    echo "$PORT $ORIG" > "$GS_ARTIFACT_DIR/usb-power-orig.txt"
    if $GS_SUDO sh -c "echo on > /sys/bus/usb/devices/$PORT/power/control" 2>/dev/null; then
        gs_pass "端口 $PORT power/control=on（原值 $ORIG 已记录）"
    else
        gs_fail "无法设置 /sys/bus/usb/devices/$PORT/power/control"
    fi
    gs_log "当前: $PORT power/control=$(cat /sys/bus/usb/devices/$PORT/power/control 2>/dev/null)"
    PARENT=$(echo "$PORT" | sed -E 's/-[0-9]+$//')
    if [ -n "$PARENT" ] && [ -d "/sys/bus/usb/devices/$PARENT" ]; then
        $GS_SUDO sh -c "echo on > /sys/bus/usb/devices/$PARENT/power/control" 2>/dev/null && \
            gs_log "父 hub $PARENT power/control=on（best-effort）"
    fi
else
    gs_fail "设备不在，无法设置端口 power/control（先让固件跑起来）"
fi

gs_step "4. gs_usb 模块"
if lsmod | grep -q '^gs_usb'; then
    gs_pass "gs_usb 已加载"
else
    gs_log "gs_usb 未加载，尝试 modprobe（可能需要 sudo）"
    if modprobe gs_usb 2>/dev/null || sudo -n modprobe gs_usb 2>/dev/null; then
        gs_pass "modprobe gs_usb 成功"
    else
        gs_skip "modprobe 失败（插板后 udev 通常会自动加载）"
    fi
fi
gs_cmd lsmod | grep -E 'gs_usb|can_dev|can'

gs_step "5. 等待 can0..can3 出现（最多 10 s）"
for i in $(seq 1 20); do
    n=$(gs_count_ifaces)
    [ "$n" -ge 4 ] && break
    sleep 0.5
done
n=$(gs_count_ifaces)
gs_log "当前 can 接口数: $n -> $(gs_find_ifaces)"
if [ "$n" -ge 4 ]; then
    gs_pass "4 个接口就绪"
else
    gs_fail "接口不足 4 个（固件 icount/端点配置？）"
fi
gs_cmd ip -br link show type can
gs_cmd lsusb -t
gs_log "== dmesg 中 gs_usb 相关日志（最近 60 行）=="
d=$(gs_dmesg_cmd || true)
if [ -n "$d" ]; then $d | grep -iE 'gs_usb|can[0-9]|candle' | tail -60 | tee -a "$GS_LOG"; fi

gs_step "6. 每个接口的驱动与时间戳能力"
for i in 0 1 2 3; do
    [ -d "/sys/class/net/can$i" ] || continue
    gs_log "--- can$i ---"
    gs_cmd ethtool -i "can$i"
    gs_cmd ethtool -T "can$i"
    gs_cmd ip -details link show "can$i"
done

gs_step "7. 提示"
gs_log "后续："
gs_log "  bash host/gs_usb/test_enum.sh        # 枚举细节与 4 通道"
gs_log "  bash host/gs_usb/run_all.sh          # 全矩阵（安全串行，遇内核异常即停）"

gs_end
