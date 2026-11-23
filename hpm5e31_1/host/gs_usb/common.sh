#!/usr/bin/env bash
# common.sh —— host/gs_usb 测试脚本公共库
#
# 提供：日志落盘（logs/<时间戳>-gs-usb-<test>.log）、dmesg 基线 + 内核 oops/WARN 检测、
#       接口发现、SocketCAN 起停、PASS/FAIL 计数与退出码。
#
# 用法（每个 test_*.sh 开头）：
#   . "$(dirname "$0")/common.sh"
#   gs_begin "enum"                # 初始化日志、检查环境、记录 dmesg 基线
#   gs_step "1. 枚举 4 个接口"
#   gs_pass "can0..can3 都出现"
#   gs_end                        # 汇总 + 退出码（0=PASS, 1=FAIL, 9=检测到内核异常）
#
# 安全设计（对应开发约定 §7）：
#   * 每次 gs_begin 记录 dmesg 行号基线；每个 gs_check_kernel 只扫描新增行；
#   * 命中 oops/BUG/WARNING/panic/Call Trace/RCU stall 时，立即打印"STOP"、把完整
#     dmesg 存到 $GS_ARTIFACT_DIR/dmesg-full-<test>-<ts>.log，并以 9 退出，
#     让 run_all.sh 中止后续测试（不许继续发帧）。
set -u

GS_LIB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
GS_REPO_ROOT=$(cd "$GS_LIB_DIR/../.." && pwd)
GS_LOG_DIR=${GS_LOG_DIR:-$GS_REPO_ROOT/logs}
GS_ARTIFACT_DIR=${GS_ARTIFACT_DIR:-/tmp/gs_usb_artifacts}
GS_TS=$(date +%Y%m%d-%H%M%S)
GS_TEST=${GS_TEST_NAME:-misc}
GS_LOG=$GS_LOG_DIR/$GS_TS-gs-usb-$GS_TEST.log
GS_DMESG_MARK=$GS_ARTIFACT_DIR/dmesg-mark-$GS_TEST
GS_PASS=0
GS_FAIL=0
GS_KERNEL_BAD=0
GS_IFACES=()

mkdir -p "$GS_LOG_DIR" "$GS_ARTIFACT_DIR"

# ---------- 输出与日志 ----------
gs_log() { printf '%s\n' "$*" | tee -a "$GS_LOG"; }
gs_title() {
    gs_log ""
    gs_log "=================================================================="
    gs_log "$*"
    gs_log "=================================================================="
}
gs_step() { gs_log ""; gs_log "---- $*"; }
gs_cmd() {
    # 执行命令并把命令与输出都写进日志（不中断脚本）
    [ $# -eq 0 ] && return 0
    gs_log "> $*"
    "$@" 2>&1 | tee -a "$GS_LOG"
}
gs_pass() { GS_PASS=$((GS_PASS+1)); gs_log "  [PASS] $*"; }
gs_fail() { GS_FAIL=$((GS_FAIL+1)); gs_log "  [FAIL] $*"; }
gs_skip() { gs_log "  [SKIP] $*"; }

# ---------- 权限（CAN 配置要 CAP_NET_ADMIN，CAN_RAW socket 要 CAP_NET_RAW） ----------
# 本机 z 用户无 cap_net_admin，can-utils 也没有 setcap，因此：
#   * ip link set  -> 需要 root
#   * candump/cansend/cangen/canplayer、python-can、原始 CAN_RAW socket -> 需要 root
# 统一通过 GS_SUDO 前缀（root 下为空，普通用户下为 "sudo -n"）。
gs_detect_sudo() {
    if [ "$(id -u)" = "0" ]; then echo ""; return; fi
    if sudo -n true 2>/dev/null; then echo "sudo -n"; return; fi
    echo ""
}
GS_SUDO=$(gs_detect_sudo)
gs_require_privileges() {
    if [ -z "$GS_SUDO" ] && [ "$(id -u)" != "0" ]; then
        gs_fail "需要 root 或免密 sudo（CAP_NET_ADMIN/CAP_NET_RAW）—— 普通用户无法配置 CAN 接口/开 CAN_RAW socket"
        return 1
    fi
    gs_pass "权限 OK（$( [ "$(id -u)" = "0" ] && echo root || echo "sudo -n" )）"
    return 0
}

# ---------- 环境 ----------
gs_require_cmds() {
    local missing=()
    local c
    for c in "$@"; do
        command -v "$c" >/dev/null 2>&1 || missing+=("$c")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        gs_fail "缺少命令: ${missing[*]}（安装：sudo apt install can-utils iproute2 ethtool）"
        return 1
    fi
    gs_pass "依赖命令齐全: $*"
}

gs_dmesg_readable() {
    if dmesg >/dev/null 2>&1; then echo dmesg; return 0; fi
    if sudo -n dmesg >/dev/null 2>&1; then echo "sudo -n dmesg"; return 0; fi
    echo ""
    return 1
}

gs_dmesg_cmd() {
    local d
    d=$(gs_dmesg_readable) || true
    if [ -z "$d" ]; then
        gs_fail "dmesg 不可读（kernel.dmesg_restrict=1 且无免密 sudo）——安全检查无法执行，拒绝继续"
        return 1
    fi
    echo "$d"
}

gs_dmesg_baseline() {
    # 用"时间戳"而不是"行号"做基线：dmesg 环形缓冲会淘汰旧行，行号会倒退
    local d
    d=$(gs_dmesg_cmd) || return 1
    date +%s > "$GS_DMESG_MARK"
    gs_log "dmesg 基线时间戳: $(cat "$GS_DMESG_MARK")（之后用 --since 只扫描新增行）"
    return 0
}

gs_check_kernel() {
    local d n bad
    d=$(gs_dmesg_cmd) || return 9
    n=$(cat "$GS_DMESG_MARK" 2>/dev/null || echo 0)
    # 优先用 dmesg --since（util-linux >= 2.34）；老版本退回"最后 400 行"
    new_lines=$($d --since "@$n" 2>/dev/null || true)
    if [ -z "$new_lines" ] && ! $d --since "@$n" >/dev/null 2>&1; then
        new_lines=$($d | tail -n 400)
    fi
    bad=$(printf '%s\n' "$new_lines" | grep -Ei 'Oops|BUG:|WARNING:|kernel panic|Call Trace|RCU stall|general protection|KASAN' || true)
    if [ -n "$bad" ]; then
        GS_KERNEL_BAD=1
        gs_log "  [STOP] 检测到内核异常（oops/WARN/...），立即停止后续测试！"
        $d | tee "$GS_ARTIFACT_DIR/dmesg-full-$GS_TEST-$GS_TS.log" >/dev/null
        gs_log "  完整 dmesg 已保存: $GS_ARTIFACT_DIR/dmesg-full-$GS_TEST-$GS_TS.log"
        printf '%s\n' "$bad" | tee -a "$GS_LOG"
        return 9
    fi
    gs_log "  dmesg 安全检查: 无 oops/WARN（新增 $(printf '%s\n' "$new_lines" | grep -c . || true) 行）"
    return 0
}

# ---------- gs_usb 设备与接口 ----------
gs_count_ifaces() {
    local i=0 n=0
    for i in 0 1 2 3; do
        [ -d "/sys/class/net/can$i" ] && n=$((n+1))
    done
    echo "$n"
}

gs_find_ifaces() {
    GS_IFACES=()
    local i
    for i in 0 1 2 3; do
        [ -d "/sys/class/net/can$i" ] && GS_IFACES+=("$i")
    done
    echo "${GS_IFACES[*]:-}"
}

gs_require_ifaces() {
    local want=${1:-4} got
    got=$(gs_count_ifaces)
    if [ "$got" -lt "$want" ]; then
        gs_fail "只找到 $got 个 can 接口（期望 $want）——gs_usb 固件是否在跑？用 lsusb 看 1209:2323"
        return 1
    fi
    gs_pass "发现 $got 个接口: $(gs_find_ifaces)"
    return 0
}

gs_is_gs_usb() {
    local idx="$1" driver
    driver=$(basename "$(readlink -f /sys/class/net/can$idx/device/driver 2>/dev/null)" 2>/dev/null)
    [ "$driver" = "gs_usb" ] && return 0
    return 1
}

gs_iface_details() {
    ip -details -statistics link show "can$1" 2>&1
}

# ---------- USB 设备存在性（sysfs，绕过 lsusb 的 apparmor 限制） ----------
gs_usb_present() {
    local d v p
    for d in /sys/bus/usb/devices/*/; do
        [ -f "$d/idVendor" ] || continue
        v=$(cat "$d/idVendor" 2>/dev/null)
        p=$(cat "$d/idProduct" 2>/dev/null)
        if [ "$v" = "1209" ] && [ "$p" = "2323" ]; then
            basename "$d"
            return 0
        fi
    done
    return 1
}

gs_wait_usb() {
    local tmo=${1:-10} i
    for i in $(seq 1 $((tmo * 5))); do
        if gs_usb_present >/dev/null; then return 0; fi
        sleep 0.2
    done
    return 1
}

gs_dmesg_tail_save() {
    local tag=${1:-tail} d out
    d=$(gs_dmesg_cmd || true)
    [ -z "$d" ] && return 1
    out="$GS_ARTIFACT_DIR/dmesg-$tag-$GS_TEST-$GS_TS.log"
    $d | tail -n 300 > "$out"
    gs_log "  dmesg 尾部已保存: $out"
    return 0
}

gs_assert_alive() {
    local dev
    if ! dev=$(gs_usb_present); then
        gs_fail "测试后 USB 设备 1209:2323 已消失（掉线/崩溃）！"
        gs_dmesg_tail_save "alive"
        return 1
    fi
    gs_pass "测试后设备仍在线（sysfs: $dev）"
    return 0
}

# ---------- SocketCAN 起停 ----------
gs_can_down() {
    $GS_SUDO ip link set "can$1" down 2>/dev/null || true
}

gs_can_up() {
    # gs_can_up <idx> <bitrate> [--dbitrate N] [--fd on|off] [额外 ip 属性...]
    local idx="$1" rate="$2"; shift 2
    local dbitrate="" fd=""
    local extra=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --dbitrate) dbitrate="$2"; shift 2;;
            --fd) fd="$2"; shift 2;;
            *) extra+=("$1"); shift;;
        esac
    done
    gs_can_down "$idx"
    local args=(type can bitrate "$rate")
    [ -n "$dbitrate" ] && args+=(dbitrate "$dbitrate")
    # 关键：不显式关掉 FD 时，接口会沿用上一次配置的 <FD> 模式（别人做 FD 测试后遗留），
    # 导致经典帧收发异常。所以默认显式 fd off。
    if [ -n "$fd" ]; then args+=(fd "$fd"); else args+=(fd off); fi
    # 关键：loopback / listen-only / one-shot 也会像 FD 一样"粘住"（前一轮测试遗留），
    # 任一残留都会让正常收发异常（例如 <LOOPBACK> 时帧不上总线）。这里统一复位。
    args+=(loopback off listen-only off one-shot off)
    [ ${#extra[@]} -gt 0 ] && args+=("${extra[@]}")
    # 注意：先设属性、再 up（"ip link set DEV up type can ..." 顺序在某些版本会报 EPERM）
    gs_cmd $GS_SUDO ip link set "can$idx" "${args[@]}"
    gs_cmd $GS_SUDO ip link set "can$idx" up
}

gs_can_state() {
    ip -details link show "can$1" 2>/dev/null | grep -oE 'state [A-Z-]+|berr-counter tx [0-9]+ rx [0-9]+' | tr '\n' ' '
    echo
}

# ---------- 帧收发 ----------
gs_cansend() { gs_cmd $GS_SUDO cansend "$1" "$2"; }

gs_capture_frames() {
    # gs_capture_frames <iface> <seconds> <outfile>
    timeout "$2" $GS_SUDO candump -T 1000 "$1" > "$3" 2>&1
    return 0
}

# ---------- 生命周期 ----------
gs_begin() {
    GS_TEST="${1:-misc}"
    GS_LOG="$GS_LOG_DIR/$GS_TS-gs-usb-$GS_TEST.log"
    : > "$GS_LOG"
    gs_title "gs_usb 测试: $GS_TEST（$GS_TS）"
    gs_log "主机: $(uname -srm)  脚本: $(basename "$0")"
    gs_log "日志: $GS_LOG"
    gs_log "工件: $GS_ARTIFACT_DIR"
    if uname -r | grep -q '^7\.0\.'; then
        gs_log "内核 7.0.x —— 与开发约定 §1 一致"
    else
        gs_log "警告: 内核 $(uname -r) 非 7.0.x，gs_usb 协议行为可能不同"
    fi
    GS_CHECK_ALIVE=${GS_CHECK_ALIVE:-1}
    gs_require_privileges || GS_FAIL=$((GS_FAIL+1))
    gs_dmesg_baseline || true
    gs_require_cmds dmesg ip ethtool cansend candump cangen canplayer || true
}

gs_end() {
    if [ "${GS_CHECK_ALIVE:-1}" = "1" ]; then gs_assert_alive || true; fi
    gs_check_kernel || true
    gs_title "汇总: $GS_TEST"
    gs_log "PASS=$GS_PASS  FAIL=$GS_FAIL  内核异常=$GS_KERNEL_BAD"
    if [ "$GS_KERNEL_BAD" -ne 0 ]; then
        gs_log "结论: FAIL（内核异常，已保存完整 dmesg）"
        echo "GS_RESULT: FAIL;0;$GS_KERNEL_BAD;$GS_LOG"
        return 9
    fi
    if [ "$GS_FAIL" -ne 0 ]; then
        gs_log "结论: FAIL"
        echo "GS_RESULT: FAIL;$GS_PASS;$GS_FAIL;$GS_LOG"
        return 1
    fi
    gs_log "结论: PASS"
    echo "GS_RESULT: PASS;$GS_PASS;0;$GS_LOG"
    return 0
}
