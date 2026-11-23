#!/usr/bin/env bash
# test_4ch.sh —— 测试项 3：4 通道并发（互不串扰）
#
# 拓扑（约定 §5）：CAN0 <-> CAN1 互连，CAN2 <-> CAN3 互连（各自 120Ω）。
# 所以 can0 发的帧只应出现在 can1，can2 发的只应出现在 can3；两组之间不应串。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "4ch"

gs_require_ifaces 4 || { gs_end; exit $?; }

ART=$GS_ARTIFACT_DIR/4ch-$GS_TS
mkdir -p "$ART"
RATE=${BITRATE:-500000}

CAP_PID=""
cap_start() { : > "$2"; ( timeout 5 $GS_SUDO candump "can$1" > "$2" 2>&1 ) & CAP_PID=$!; sleep 0.5; }
cap_stop()  { [ -n "$CAP_PID" ] && wait "$CAP_PID" 2>/dev/null; CAP_PID=""; }

gs_step "1. 4 个通道全部以 $RATE bit/s 起来"
for i in 0 1 2 3; do gs_can_up "$i" "$RATE"; done
for i in 0 1 2 3; do gs_log "can$i: $(gs_can_state $i)"; done
gs_check_kernel || { gs_end; exit 9; }

gs_step "2. 每通道同时发不同 ID（can0 发 0x100、can2 发 0x200）"
cap_start 1 "$ART/rx-can1.log"
CAP2=""
( timeout 5 $GS_SUDO candump can3 > "$ART/rx-can3.log" 2>&1 ) & CAP2=$!
sleep 0.5
gs_cansend can0 100#A0A1A2A3
gs_cansend can2 200#B0B1B2B3
sleep 1.2
wait "$CAP_PID" 2>/dev/null; CAP_PID=""
wait "$CAP2" 2>/dev/null

gs_step "3. 判据 A：can1 只应看到 0x100，can3 只应看到 0x200"
if grep -Eq '100 *\[4\] *A0 A1 A2 A3' "$ART/rx-can1.log"; then gs_pass "can1 收到 can0 的 0x100"; else gs_fail "can1 未收到 0x100"; head -3 "$ART/rx-can1.log" | tee -a "$GS_LOG"; fi
if grep -Eq '200 *\[4\] *B0 B1 B2 B3' "$ART/rx-can3.log"; then gs_pass "can3 收到 can2 的 0x200"; else gs_fail "can3 未收到 0x200"; head -3 "$ART/rx-can3.log" | tee -a "$GS_LOG"; fi

gs_step "4. 判据 B：无串扰（can1 不应出现 0x200，can3 不应出现 0x100）"
if grep -Eq '(^| )200 ' "$ART/rx-can1.log"; then gs_fail "串扰：can1 收到了 can2 的 0x200"; else gs_pass "can1 无 can2 的帧（无串扰）"; fi
if grep -Eq '(^| )100 ' "$ART/rx-can3.log"; then gs_fail "串扰：can3 收到了 can0 的 0x100"; else gs_pass "can3 无 can0 的帧（无串扰）"; fi

gs_step "5. 4 通道各自都能发送（TX 计数增加）"
for i in 0 1 2 3; do
    gs_cansend "can$i" "7${i}0#11"
done
sleep 0.5
for i in 0 1 2 3; do
    tx=$(ip -s link show "can$i" | awk '/TX:/{getline; print $2; exit}')
    gs_log "can$i TX 包数=$tx"
    [ "${tx:-0}" -ge 1 ] && gs_pass "can$i 可发送（TX=$tx）" || gs_fail "can$i 未发出帧"
done

gs_step "6. 4 通道错误计数（应全为 0/ERROR-ACTIVE）"
for i in 0 1 2 3; do gs_log "can$i: $(gs_can_state $i)"; done
ip -details -statistics link show can0 | tee -a "$GS_LOG"
ip -details -statistics link show can3 | tee -a "$GS_LOG"

gs_check_kernel || { gs_end; exit 9; }
for i in 0 1 2 3; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done
gs_end
