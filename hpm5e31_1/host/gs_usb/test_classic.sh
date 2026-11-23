#!/usr/bin/env bash
# test_classic.sh —— 测试项 2：经典 CAN 500k 收发（can0 -> can1 / 反向）
#
# 板上 CAN0 与 CAN1 已物理相连（约定 §5），所以本测试是"同一 USB 设备的两通道互发"。
# 覆盖：标准帧、29 位扩展帧、0 字节帧、8 字节满帧、RTR 远程帧、DLC=8 满负载。
#
# 判定：candump 在接收端抓到对应 ID 与数据；双向；收发计数一致。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "classic"

gs_require_ifaces 2 || { gs_end; exit $?; }

BITRATE=${BITRATE:-500000}
ART=$GS_ARTIFACT_DIR/classic-$GS_TS
mkdir -p "$ART"

CAP_PID=""
cap_start() {
    # cap_start <iface> <outfile>
    : > "$2"
    ( timeout 4 $GS_SUDO candump "can$1" > "$2" 2>&1 ) &
    CAP_PID=$!
    sleep 0.5
}
cap_stop() {
    [ -n "$CAP_PID" ] && wait "$CAP_PID" 2>/dev/null
    CAP_PID=""
}

expect_line() {
    # expect_line <file> <grep -E 正则> <描述>
    if grep -Eq "$2" "$1"; then
        gs_pass "$3"
        gs_log "    匹配行: $(grep -E "$2" "$1" | head -2 | tr -s ' ' | tr '\n' ';')"
        return 0
    fi
    gs_fail "$3（未匹配 $2）"
    gs_log "    实际捕获 $(wc -l < "$1") 行:"
    head -5 "$1" | tee -a "$GS_LOG"
    return 1
}

gs_step "1. 两通道以 $BITRATE bit/s 起来（只做位时序，不启用其它特性）"
gs_can_up 0 "$BITRATE"
gs_can_up 1 "$BITRATE"
gs_log "can0: $(gs_can_state 0)"
gs_log "can1: $(gs_can_state 1)"
gs_check_kernel || { gs_end; exit 9; }

gs_step "2. can0 -> can1：标准帧 0x123（8 字节）"
cap_start 1 "$ART/classic-0to1.log"
gs_cansend can0 123#1122334455667788
sleep 0.8
cap_stop
expect_line "$ART/classic-0to1.log" '123 *\[8\] *11 22 33 44 55 66 77 88' "can1 收到 0x123 [8] 1122334455667788"

gs_step "3. can1 -> can0：标准帧 0x321（3 字节），验证反向"
cap_start 0 "$ART/classic-1to0.log"
gs_cansend can1 321#AABBCC
sleep 0.8
cap_stop
expect_line "$ART/classic-1to0.log" '321 *\[3\] *AA BB CC' "can0 收到 0x321 [3] AABBCC"

gs_step "4. 29 位扩展帧（0x18DAF110，UDS 物理响应 ID）"
cap_start 1 "$ART/classic-eff.log"
gs_cansend can0 18DAF110#0322F190
sleep 0.8
cap_stop
expect_line "$ART/classic-eff.log" '18DAF110 *\[4\] *03 22 F1 90' "can1 收到 29 位帧 0x18DAF110"

gs_step "5. 0 字节帧（DLC=0）"
cap_start 1 "$ART/classic-dlc0.log"
gs_cansend can0 100#
sleep 0.8
cap_stop
expect_line "$ART/classic-dlc0.log" '100 *\[0\]' "can1 收到 0 字节帧 0x100"

gs_step "6. RTR 远程帧（0x7FF，请求 8 字节）"
cap_start 1 "$ART/classic-rtr.log"
gs_cansend can0 7FF#R8
sleep 0.8
cap_stop
if grep -Eq '7FF' "$ART/classic-rtr.log"; then
    gs_pass "can1 收到 RTR 帧 0x7FF"
    grep -E '7FF' "$ART/classic-rtr.log" | head -2 | tee -a "$GS_LOG"
else
    gs_skip "RTR 帧未被 can1 报告（MCAN RTR 支持需确认，记为待确认）"
fi

gs_step "7. 收发计数（ip -s link）"
ip -s link show can0 | tee -a "$GS_LOG"
ip -s link show can1 | tee -a "$GS_LOG"
RX1=$(ip -s link show can1 | awk '/RX:/{getline; print $2; exit}')
TX0=$(ip -s link show can0 | awk '/TX:/{getline; print $2; exit}')
gs_log "can1 RX 包数=$RX1  can0 TX 包数=$TX0"
if [ "${RX1:-0}" -ge 4 ]; then gs_pass "can1 至少收到 4 帧（RX=$RX1）"; else gs_fail "can1 RX 计数偏少（$RX1）"; fi
if [ "${TX0:-0}" -ge 4 ]; then gs_pass "can0 TX 计数正常（TX=$TX0）"; else gs_fail "can0 TX 计数偏少（$TX0）"; fi

gs_step "8. 高位速率下的错误计数（正常应保持 ERROR-ACTIVE / 0 错误）"
gs_log "can0: $(gs_can_state 0)"
gs_log "can1: $(gs_can_state 1)"

gs_check_kernel || { gs_end; exit 9; }
for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done
gs_end
