#!/usr/bin/env bash
# test_compat.sh —— 测试项 10：与常用 SocketCAN 工具/库的兼容性
#
# 覆盖：python-can 4.x 收发、candump、cansend、cangen、canplayer 回放。
# 这是"标准 gs_usb 适配器"的直接证据：这些工具都只依赖 SocketCAN，不认识 gs_usb 私有协议。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "compat"

gs_require_ifaces 2 || { gs_end; exit $?; }
RATE=${BITRATE:-500000}
ART=$GS_ARTIFACT_DIR/compat-$GS_TS
mkdir -p "$ART"
PY=${PYTHON:-$HOME/pyenv/bin/python}
if ! "$PY" -c 'import can' 2>/dev/null; then
    gs_skip "pyenv 里没有 python-can，跳过 python-can 用例"
    PY=""
fi

CAP_PID=""
cap_start() { : > "$2"; ( timeout 5 $GS_SUDO candump "can$1" > "$2" 2>&1 ) & CAP_PID=$!; sleep 0.4; }
cap_stop()  { [ -n "$CAP_PID" ] && wait "$CAP_PID" 2>/dev/null; CAP_PID=""; }

gs_step "1. 起两个通道"
gs_can_up 0 "$RATE"
gs_can_up 1 "$RATE"

# ---------------- candump / cansend ----------------
gs_step "2. candump + cansend（can-utils 基础链路）"
cap_start 1 "$ART/cansend.log"
gs_cansend can0 111#0102030405060708
sleep 0.8
cap_stop
grep -Eq '111 *\[8\] *01 02 03 04 05 06 07 08' "$ART/cansend.log" && gs_pass "cansend -> candump 通" || { gs_fail "cansend/candump 不通"; head -3 "$ART/cansend.log" | tee -a "$GS_LOG"; }

# ---------------- python-can ----------------
gs_step "3. python-can 4.x 收发"
if [ -n "$PY" ]; then
    cat > "$ART/pycan_test.py" <<'PYEOF'
import sys, time
import can
tx = can.Bus(interface="socketcan", channel="can0")
rx = can.Bus(interface="socketcan", channel="can1")
msg = can.Message(arbitration_id=0x222, data=[0xDE, 0xAD, 0xBE, 0xEF], is_extended_id=False)
tx.send(msg)
got = None
deadline = time.time() + 2.0
while time.time() < deadline:
    m = rx.recv(timeout=0.5)
    if m is not None and m.arbitration_id == 0x222:
        got = m
        break
tx.shutdown(); rx.shutdown()
if got is None:
    print("FAIL: python-can 没收到 0x222")
    sys.exit(1)
print(f"OK: python-can 收到 id=0x{got.arbitration_id:03X} data={got.data.hex().upper()} dlc={got.dlc}")
PYEOF
    if $GS_SUDO "$PY" "$ART/pycan_test.py" 2>&1 | tee -a "$GS_LOG"; then
        gs_pass "python-can 收发通（socketcan backend）"
    else
        gs_fail "python-can 收发失败"
    fi
fi

# ---------------- cangen ----------------
gs_step "4. cangen 突发（2 s，约 1000 帧）"
cap_start 1 "$ART/cangen-rx.log"
timeout 3 $GS_SUDO cangen can0 -g 1 -n 1000 -I r -D r > "$ART/cangen.log" 2>&1 || true
sleep 0.5
cap_stop
CGN_RX=$(grep -cE '^ *can1 ' "$ART/cangen-rx.log" 2>/dev/null || echo 0)
gs_log "cangen 期间 can1 捕获到 $CGN_RX 帧"
[ "$CGN_RX" -gt 100 ] && gs_pass "cangen 吞吐正常（$CGN_RX 帧）" || gs_fail "cangen 帧太少（$CGN_RX）"

# ---------------- canplayer ----------------
gs_step "5. canplayer 回放（先用 $GS_SUDO candump -L 录一段，再回放）"
cap_start 1 "$ART/player-rx.log"
timeout 2 $GS_SUDO candump -L can0 > "$ART/record.log" 2>&1 &
REC_PID=$!
sleep 0.3
gs_cansend can1 333#C0FFEE
sleep 0.6
kill "$REC_PID" 2>/dev/null || true
wait "$REC_PID" 2>/dev/null || true
REC_N=$(grep -cE '^\(|^ *can' "$ART/record.log" 2>/dev/null || echo 0)
gs_log "录制到 $REC_N 行:"
head -3 "$ART/record.log" | tee -a "$GS_LOG"
if [ "$REC_N" -ge 1 ]; then
    if timeout 5 $GS_SUDO canplayer -I "$ART/record.log" 2>&1 | tee -a "$GS_LOG"; then
        sleep 0.5
        cap_stop
        if grep -Eq '333' "$ART/player-rx.log"; then
            gs_pass "canplayer 回放成功（can1 收到回放帧 0x333）"
        else
            gs_fail "canplayer 回放了但 can1 没收到 0x333"
            head -3 "$ART/player-rx.log" | tee -a "$GS_LOG"
        fi
    else
        gs_fail "canplayer 执行失败"
        cap_stop
    fi
else
    cap_stop
    gs_skip "$GS_SUDO candump -L 没录到帧，canplayer 用例跳过"
fi

gs_step "6. 内核安全检查"
gs_check_kernel || { gs_end; exit 9; }
for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done
gs_end
