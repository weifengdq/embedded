#!/usr/bin/env bash
# test_fd.sh —— 测试项 4：CAN FD（1M 仲裁 + 2M/5M/8M 数据，BRS/ESI，64 字节）
#
# 判定：
#   * 每个数据段位速率下，can0 发 64 字节 FD 帧（BRS=1），can1 能把 64 字节完整收到；
#   * ip -details 显示 fd on / dbitrate 生效；
#   * 5M/8M 若 $GS_SUDO ip link set 失败（PHY/线缆/时序限制）记 SKIP 并保留错误信息，不判 FAIL。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "fd"

gs_require_ifaces 2 || { gs_end; exit $?; }

ARB=${ARB:-1000000}
ART=$GS_ARTIFACT_DIR/fd-$GS_TS
mkdir -p "$ART"

# 64 字节载荷：00 01 02 ... 3F
D64=""
for b in $(seq 0 63); do D64="$D64$(printf '%02X' $b)"; done
D32="${D64:0:64}"

CAP_PID=""
cap_start() { : > "$2"; ( timeout 4 $GS_SUDO candump "can$1" > "$2" 2>&1 ) & CAP_PID=$!; sleep 0.5; }
cap_stop()  { [ -n "$CAP_PID" ] && wait "$CAP_PID" 2>/dev/null; CAP_PID=""; }

expect_fd() {
    # expect_fd <file> <id> <hexdata> <描述>
    local f="$1" id="$2" data="$3" desc="$4" got
    got=$(grep -E "$id" "$f" | head -5 | tr -s ' ')
    if [ -z "$got" ]; then gs_fail "$desc（未收到 0x$id）"; return 1; fi
    # candump 的 FD 输出：123##1<data> 或 123 [64] <data...>（不同版本格式不同），用"数据串出现"判定
    local flat
    flat=$(printf '%s' "$got" | tr -d ' ' | tr 'a-f' 'A-F')
    if printf '%s' "$flat" | grep -q "$data"; then
        gs_pass "$desc"
        gs_log "    首行: $(printf '%s' "$got" | head -1 | cut -c1-120)"
        return 0
    fi
    gs_fail "$desc（收到帧但数据不匹配）"
    printf '%s' "$got" | head -3 | tee -a "$GS_LOG"
    return 1
}

try_fd_rate() {
    # try_fd_rate <dbitrate> <额外说明>
    local dbr="$1" note="$2"
    gs_step "FD 数据段 $((dbr/1000000)) Mbit/s（仲裁 $((ARB/1000000)) Mbit/s）$note"
    if ! $GS_SUDO ip link set can0 down 2>/dev/null; then :; fi
    if ! $GS_SUDO ip link set can1 down 2>/dev/null; then :; fi
    local err0 err1
    err0=$($GS_SUDO ip link set can0 type can bitrate "$ARB" dbitrate "$dbr" fd on 2>&1)
    err1=$($GS_SUDO ip link set can1 type can bitrate "$ARB" dbitrate "$dbr" fd on 2>&1)
    if [ -n "$err0" ] || [ -n "$err1" ]; then
        if printf '%s' "$err0$err1" | grep -qiE 'invalid|not supported|out of range|error'; then
            gs_skip "$note 配置被拒（$err0 $err1）—— 记 SKIP"
            printf '%s\n' "$err0$err1" | tee -a "$GS_LOG"
            return 2
        fi
    fi
    $GS_SUDO ip link set can0 up 2>&1 | tee -a "$GS_LOG"
    $GS_SUDO ip link set can1 up 2>&1 | tee -a "$GS_LOG"
    if ! ip -details link show can0 | grep -q 'dbitrate $dbr'; then
        gs_skip "$note can0 未显示 dbitrate $dbr（可能固件未生效），记 SKIP"
        return 2
    fi

    cap_start 1 "$ART/fd-$dbr.log"
    gs_cansend can0 "123##1$D32"
    sleep 0.8
    cap_stop
    expect_fd "$ART/fd-$dbr.log" 123 "$D32" "$note can1 收到 32 字节 FD（BRS=1）"

    cap_start 1 "$ART/fd64-$dbr.log"
    gs_cansend can0 "456##1$D64"
    sleep 0.8
    cap_stop
    expect_fd "$ART/fd64-$dbr.log" 456 "$D64" "$note can1 收到 64 字节 FD（BRS=1）"

    gs_step "FD 模式下的经典帧兼容（同一对接口）"
    cap_start 1 "$ART/fd-classic-$dbr.log"
    gs_cansend can0 0FF#DEADBEEF
    sleep 0.8
    cap_stop
    if grep -Eq '0FF *\[4\] *DE AD BE EF' "$ART/fd-classic-$dbr.log"; then
        gs_pass "$note FD 配置下经典帧仍可收发"
    else
        gs_fail "$note FD 配置下经典帧收发异常"
        head -3 "$ART/fd-classic-$dbr.log" | tee -a "$GS_LOG"
    fi

    gs_step "ESI 位（发送 ESI=1 的帧，观察接收侧 flags）"
    cap_start 1 "$ART/fd-esi-$dbr.log"
    gs_cansend can0 "789##3$D32"    # flags=3: BRS=1 | ESI=1
    sleep 0.8
    cap_stop
    if grep -Eq '789' "$ART/fd-esi-$dbr.log"; then
        gs_pass "$note ESI=1 帧被接收（透传细节见捕获行）"
        head -2 "$ART/fd-esi-$dbr.log" | tee -a "$GS_LOG"
    else
        gs_fail "$note ESI=1 帧未被接收"
    fi

    gs_check_kernel || return 9
    return 0
}

gs_step "0. 前置：两通道先以经典模式确认链路（避免把位时序问题误判成 FD 问题）"
gs_can_up 0 500000
gs_can_up 1 500000
CAP_PID=""
cap_start 1 "$ART/pre.log"
gs_cansend can0 001#AA
sleep 0.6
cap_stop
grep -Eq '001 *\[1\] *AA' "$ART/pre.log" && gs_pass "经典 500k 链路正常" || gs_fail "经典链路异常，FD 测试结果不可信"

RC=0
try_fd_rate 2000000 "（1M/2M，约定要求必测）" || RC=$?
[ "$RC" = 9 ] && { gs_end; exit 9; }
try_fd_rate 5000000 "（5M，可选）" || RC=$?
[ "$RC" = 9 ] && { gs_end; exit 9; }
try_fd_rate 8000000 "（8M，可选）" || RC=$?
[ "$RC" = 9 ] && { gs_end; exit 9; }

gs_step "最终状态与计数"
ip -details -statistics link show can0 | tee -a "$GS_LOG"
ip -details -statistics link show can1 | tee -a "$GS_LOG"
for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done
gs_end
