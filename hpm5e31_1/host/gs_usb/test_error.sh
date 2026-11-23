#!/usr/bin/env bash
# test_error.sh —— 测试项 8：CAN_ERR_* 错误帧上报（BERR）与 BUS-OFF 恢复
#
# 覆盖 U-40-17（CAN_ERR_* 帧上报，M3 前未实现）与 U-40-6（BUS-OFF 状态上报与恢复）。
#
# 错误注入方法（可重复、不动硬件）：
#   can0 = 500k，berr-reporting on
#   can1 = 500k，listen-only on  → restricted operation：能收不发，不回 ACK、不发错误帧
#   can0 发 1 帧后 MCAN 自动重传，每次 ACK 错误 TEC += 8：
#     96 → ERROR-WARNING，128 → ERROR-PASSIVE，256 → BUS-OFF
#
# 观察点（全部来自内核可见状态，不由固件自报）：
#   1) candump can0 收到 CAN_ERR_CRTL（can_id 0x20000004）与 CAN_ERR_BUSOFF（0x20000040）；
#   2) ip -details link show can0 出现 BUS-OFF（内核 gs_update_state 依据错误帧更新 netdev 状态）；
#   3) 恢复：can1 回到正常 500k，can0 重启 → ERROR-ACTIVE，收到 CAN_ERR_RESTARTED（0x20000100），
#      can0→can1 收发恢复正常。
#
# 注：错误帧只在 berr-reporting on 时上报（与 candleLight 语义一致）；GET_STATE 的
#     berr-counter 不受此开关影响。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "error"

gs_require_ifaces 2 || { gs_end; exit $?; }
ART=$GS_ARTIFACT_DIR/error-$GS_TS
mkdir -p "$ART"

can_state_short() {
    ip -details link show "$1" 2>/dev/null | grep -oE 'state [A-Z-]+|berr-counter tx [0-9]+ rx [0-9]+' | tr '\n' ' '
}

wait_state() {
    # wait_state <iface> <grep 正则> <超时秒>
    local i
    for i in $(seq 1 "$(($3 * 10))"); do
        if ip -details link show "$1" 2>/dev/null | grep -qE "$2"; then return 0; fi
        sleep 0.1
    done
    return 1
}

gs_step "1. can0=500k berr-reporting on；can1=500k listen-only（不回 ACK）"
$GS_SUDO ip link set can0 down 2>/dev/null || true
$GS_SUDO ip link set can1 down 2>/dev/null || true
gs_cmd $GS_SUDO ip link set can0 type can bitrate 500000 fd off loopback off listen-only off one-shot off berr-reporting on
gs_cmd $GS_SUDO ip link set can0 up
gs_cmd $GS_SUDO ip link set can1 type can bitrate 500000 fd off loopback off one-shot off listen-only on
gs_cmd $GS_SUDO ip link set can1 up
gs_log "can0: $(can_state_short can0)"
gs_log "can1: $(can_state_short can1)"

gs_step "2. 后台 candump can0 抓错误帧；can0 发 1 帧触发自动重传，等 BUS-OFF（最多 15 s）"
: > "$ART/err.dump"
( timeout 18 $GS_SUDO candump -T 2000 can0 > "$ART/err.dump" 2>&1 ) & DUMP_PID=$!
sleep 0.6
gs_cansend can0 "123#1122334455667788" >/dev/null 2>&1 || true
BOFF=0
for i in $(seq 1 150); do
    if ip -details link show can0 2>/dev/null | grep -qi 'BUS-OFF'; then BOFF=1; gs_log "第 $i 次采样（约 $((i / 10)) s）看到 BUS-OFF"; break; fi
    sleep 0.1
done
sleep 0.8
kill "$DUMP_PID" 2>/dev/null || true
wait "$DUMP_PID" 2>/dev/null || true
ip -details -statistics link show can0 | tee "$ART/busoff.txt" | grep -oE 'state [A-Z-]+|berr-counter tx [0-9]+ rx [0-9]+' | head -2 | tee -a "$GS_LOG"
gs_log "错误帧原始 dump（前 12 行）:"
head -12 "$ART/err.dump" | tee -a "$GS_LOG"

gs_step "3. 判定：BUS-OFF 状态 + CAN_ERR_BUSOFF/CRTL 帧"
if [ "$BOFF" = 1 ] && ip -details link show can0 | grep -qi 'BUS-OFF'; then
    gs_pass "ip -details 显示 BUS-OFF（内核按 CAN_ERR_BUSOFF 更新状态，U-40-6 的状态上报闭环）"
else
    gs_fail "15 s 内未出现 BUS-OFF（错误帧上报或自动重传异常）"
fi
if grep -qi '20000040' "$ART/err.dump"; then
    gs_pass "candump 收到 CAN_ERR_BUSOFF 帧（can_id 20000040，U-40-17）"
else
    gs_fail "candump 未收到 CAN_ERR_BUSOFF 帧"
fi
if grep -qi '20000004' "$ART/err.dump"; then
    gs_pass "candump 收到 CAN_ERR_CRTL 帧（warning/passive 状态迁移上报）"
    gs_log "CAN_ERR_CRTL 帧样本: $(grep -i '20000004' "$ART/err.dump" | head -4 | tr '\n' '|')"
else
    gs_fail "candump 未收到 CAN_ERR_CRTL 帧"
fi
ERR_FRAMES=$(grep -ciE '200000(04|40|02|01)' "$ART/err.dump" || true)
gs_log "dump 里 CAN_ERR_* 帧计数: $ERR_FRAMES"

gs_step "4. 恢复：can1 回正常 500k；重启 can0，检查 RESTARTED 与链路"
$GS_SUDO ip link set can1 down 2>/dev/null || true
gs_cmd $GS_SUDO ip link set can1 type can bitrate 500000 fd off loopback off listen-only off one-shot off
gs_cmd $GS_SUDO ip link set can1 up
: > "$ART/restart.dump"
: > "$ART/recover.dump"
( timeout 10 $GS_SUDO candump -T 2000 can0 > "$ART/restart.dump" 2>&1 ) & RD_PID=$!
( timeout 10 $GS_SUDO candump -T 2000 can1 > "$ART/recover.dump" 2>&1 ) & RC_PID=$!
sleep 0.4
$GS_SUDO ip link set can0 down 2>&1 | tee -a "$GS_LOG"
gs_cmd $GS_SUDO ip link set can0 type can bitrate 500000 fd off loopback off listen-only off one-shot off berr-reporting on
gs_cmd $GS_SUDO ip link set can0 up
if wait_state can0 'ERROR-ACTIVE' 10; then
    gs_pass "重启后 can0 恢复 ERROR-ACTIVE"
else
    gs_fail "重启后 can0 未恢复: $(can_state_short can0)"
fi
sleep 0.5
gs_cansend can0 "321#AABB" >/dev/null 2>&1 || true
sleep 1.0
kill "$RD_PID" "$RC_PID" 2>/dev/null || true
wait "$RD_PID" 2>/dev/null || true
wait "$RC_PID" 2>/dev/null || true
if grep -qi '20000100' "$ART/restart.dump"; then
    gs_pass "can0 收到 CAN_ERR_RESTARTED 帧（can_id 20000100）"
else
    gs_log "  [INFO] 未捕获 RESTARTED 帧（重启太快或 dump 未覆盖）；不影响链路判定"
fi
if grep -Eq '321 *\[2\] *AA BB' "$ART/recover.dump"; then
    gs_pass "恢复后 can0 -> can1 收发生效"
else
    gs_fail "恢复后收发生效失败"
    head -3 "$ART/recover.dump" | tee -a "$GS_LOG"
fi
gs_log "can0: $(can_state_short can0) / can1: $(can_state_short can1)"

gs_step "5. 内核安全检查"
gs_check_kernel || { gs_end; exit 9; }

for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done
gs_end
