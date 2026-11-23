#!/usr/bin/env bash
# test_modes.sh —— 测试项 7：模式（listen-only / loopback / one-shot）
#
# 拓扑：CAN0 <-> CAN1 物理互连。
#   listen-only：can0 只收不发；can1 正常发 -> can0 能收到（同时 can1 会因无 ACK 报错，属正常）
#   loopback   ：can0 自己发的帧回到自己的接收队列（本地回环）
#   one-shot   ：无 ACK 时单帧只尝试一次（不重传），因此 8 帧后 TEC≈64（ERROR-WARNING），
#                而不是像重传那样立刻 BUS-OFF（32 次/帧）
set -u
. "$(dirname "$0")/common.sh"
gs_begin "modes"

gs_require_ifaces 2 || { gs_end; exit $?; }
RATE=${BITRATE:-500000}
ART=$GS_ARTIFACT_DIR/modes-$GS_TS
mkdir -p "$ART"

CAP_PID=""
cap_start() { : > "$2"; ( timeout 4 $GS_SUDO candump "can$1" > "$2" 2>&1 ) & CAP_PID=$!; sleep 0.5; }
cap_stop()  { [ -n "$CAP_PID" ] && wait "$CAP_PID" 2>/dev/null; CAP_PID=""; }

teardown() { for i in 0 1 2 3; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done; }

# ---------------- listen-only ----------------
gs_step "1. listen-only：can0 设 listen-only，can1 正常发"
$GS_SUDO ip link set can0 down 2>/dev/null || true
$GS_SUDO ip link set can0 type can bitrate "$RATE" listen-only on loopback off one-shot off fd off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can0 up 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can1 down 2>/dev/null || true
$GS_SUDO ip link set can1 type can bitrate "$RATE" fd off loopback off listen-only off one-shot off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can1 up 2>&1 | tee -a "$GS_LOG"
ip -details link show can0 | tee -a "$GS_LOG"
if ip -details link show can0 | grep -qi 'listen-only'; then
    gs_pass "can0 以 listen-only 模式起来（ip -details 可见）"
else
    gs_fail "can0 未显示 listen-only"
fi
cap_start 0 "$ART/listenonly.log"
gs_cansend can1 555#1122334455667788
sleep 1.0
cap_stop
if grep -Eq '555 *\[8\] *11 22 33 44 55 66 77 88' "$ART/listenonly.log"; then
    gs_pass "listen-only 的 can0 收到了 can1 的帧（只能收）"
else
    gs_fail "listen-only 的 can0 没收到帧"
    head -3 "$ART/listenonly.log" | tee -a "$GS_LOG"
fi
gs_log "can1 状态（listen-only 对端不 ACK 时会报错，属预期）: $(gs_can_state can1)"
gs_check_kernel || { teardown; gs_end; exit 9; }

# ---------------- loopback ----------------
gs_step "2. loopback：can0 打开 loopback，自己发自己收"
$GS_SUDO ip link set can0 down 2>/dev/null || true
$GS_SUDO ip link set can0 type can bitrate "$RATE" loopback on listen-only off one-shot off fd off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can0 up 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can1 down 2>/dev/null || true
ip -details link show can0 | tee -a "$GS_LOG"
if ip -details link show can0 | grep -qi 'loopback'; then
    gs_pass "can0 以 loopback 模式起来"
else
    gs_fail "can0 未显示 loopback"
fi
cap_start 0 "$ART/loopback.log"
gs_cansend can0 666#DEADBEEF
sleep 1.0
cap_stop
if grep -Eq '666 *\[4\] *DE AD BE EF' "$ART/loopback.log"; then
    gs_pass "loopback 生效：can0 收到自己发的 0x666"
else
    gs_fail "loopback 未生效（can0 未收到自己的帧）"
    head -3 "$ART/loopback.log" | tee -a "$GS_LOG"
fi
gs_check_kernel || { teardown; gs_end; exit 9; }

# ---------------- one-shot ----------------
gs_step "3. one-shot：无 ACK 时单帧只发一次（不重传）"
$GS_SUDO ip link set can0 down 2>/dev/null || true
$GS_SUDO ip link set can0 type can bitrate "$RATE" one-shot on listen-only off loopback off fd off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can0 up 2>&1 | tee -a "$GS_LOG"
$GS_SUDO # 注意：本固件 MODE RESET 后 MCAN 仍会 ACK，ip link set can1 down 不产生错误；
# 用"速率错配"制造无 ACK（can1 改 250k，can0 仍 500k）
$GS_SUDO ip link set can1 down 2>/dev/null || true
$GS_SUDO ip link set can1 type can bitrate 250000 fd off loopback off listen-only off one-shot off 2>&1 | tee -a "$GS_LOG"
$GS_SUDO ip link set can1 up 2>&1 | tee -a "$GS_LOG"
ip -details link show can0 | tee -a "$GS_LOG"
if ip -details link show can0 | grep -qi 'one-shot'; then
    gs_pass "can0 以 one-shot 模式起来"
else
    gs_fail "can0 未显示 one-shot"
fi
TEC0=$(ip -details link show can0 | grep -oE 'berr-counter tx [0-9]+' | awk '{print $3}')
for n in $(seq 1 8); do gs_cansend can0 "70$n#AA" >/dev/null 2>&1 || true; done
sleep 0.5
STATE=$(gs_can_state can0)
TEC1=$(ip -details link show can0 | grep -oE 'berr-counter tx [0-9]+' | awk '{print $3}')
gs_log "8 帧后: $STATE   TEC ${TEC0:-0} -> ${TEC1:-0}"
if printf '%s' "$STATE" | grep -qi 'BUS-OFF'; then
    gs_fail "one-shot 下 8 帧即 BUS-OFF —— 说明仍在重传（不应发生）"
elif printf '%s' "$STATE" | grep -qiE 'ERROR-WARNING|ERROR-ACTIVE'; then
    if [ "${TEC1:-0}" -gt "${TEC0:-0}" ]; then
        gs_pass "one-shot：TEC ${TEC0:-0}->${TEC1}（每帧只尝试一次，未重传到 BUS-OFF），state=$(printf '%s' "$STATE" | grep -oE 'ERROR-[A-Z]+' | head -1)"
    else
        gs_fail "one-shot：TEC 未增长（${TEC0:-?}->${TEC1:-?}），可能仍未映射 disable_auto_retransmission"
    fi
else
    gs_skip "one-shot 行为待确认（state=$STATE）"
fi
gs_check_kernel || { teardown; gs_end; exit 9; }

gs_step "4. 收尾：恢复默认模式"
teardown
gs_end
