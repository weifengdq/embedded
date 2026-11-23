#!/usr/bin/env bash
# teardown.sh —— 收尾：关接口、保存 dmesg 尾部、可选卸载 gs_usb
#
# 用法：bash teardown.sh [--unload]
#   --unload  卸载 gs_usb 模块（仅在确认没有其它 CAN 用途时使用）
set -u
. "$(dirname "$0")/common.sh"
GS_CHECK_ALIVE=0    # 收尾阶段设备可能已被卸载，不做存活检查
gs_begin "teardown"

UNLOAD=0
[ "${1:-}" = "--unload" ] && UNLOAD=1

gs_step "1. 关闭 can 接口"
for i in 0 1 2 3; do
    [ -d "/sys/class/net/can$i" ] || continue
    $GS_SUDO ip link set "can$i" down 2>/dev/null && gs_pass "can$i down" || gs_log "can$i down 失败（可能已 down）"
done

gs_step "1.5 恢复 USB autosuspend"
if [ -f "$GS_ARTIFACT_DIR/usb-power-orig.txt" ]; then
    read -r PORT ORIG < "$GS_ARTIFACT_DIR/usb-power-orig.txt" || true
    if [ -n "${PORT:-}" ] && [ -n "${ORIG:-}" ] && [ "${ORIG:-}" != "unknown" ]; then
        $GS_SUDO sh -c "echo $ORIG > /sys/bus/usb/devices/$PORT/power/control" 2>/dev/null && \
            gs_pass "端口 $PORT power/control 恢复为 $ORIG" || gs_log "端口恢复失败（设备可能已不在）"
    fi
fi
$GS_SUDO sh -c 'echo 2 > /sys/module/usbcore/parameters/autosuspend' 2>/dev/null && \
    gs_pass "usbcore autosuspend 恢复默认 2" || gs_log "autosuspend 恢复失败（可忽略）"

gs_step "2. 保存 dmesg 尾部（USB/CAN 相关 200 行）"
d=$(gs_dmesg_cmd || true)
if [ -n "$d" ]; then
    OUT="$GS_ARTIFACT_DIR/dmesg-tail-$GS_TS.log"
    $d | grep -iE 'gs_usb|can[0-9]|usb|oops|WARN' | tail -200 > "$OUT"
    gs_pass "dmesg 尾部已保存: $OUT"
fi

gs_step "3. 内核异常检查"
gs_check_kernel || true

if [ "$UNLOAD" = 1 ]; then
    gs_step "4. 卸载 gs_usb"
    if modprobe -r gs_usb 2>/dev/null || sudo -n modprobe -r gs_usb 2>/dev/null; then
        gs_pass "gs_usb 已卸载"
    else
        gs_fail "卸载失败（有接口在用？先 $GS_SUDO ip link set canX down）"
    fi
fi

gs_end
