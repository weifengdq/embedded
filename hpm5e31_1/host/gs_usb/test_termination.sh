#!/usr/bin/env bash
# test_termination.sh —— 测试项 9：终端电阻（约定：不实现，主机请求应返回 EOPNOTSUPP）
#
# 板上是手动拨码开关，固件不 advertise GS_CAN_FEATURE_TERMINATION，因此：
#   $GS_SUDO ip link set canX termination 120   -> 内核返回 -EOPNOTSUPP（"Operation not supported"）
#   ip -details link show canX         -> 不应出现 termination 属性
#
# 判定：退出码非 0 且输出含 not supported/not implemented；同时 ip -details 不含 termination。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "termination"

gs_require_ifaces 1 || { gs_end; exit $?; }
ART=$GS_ARTIFACT_DIR/termination-$GS_TS
mkdir -p "$ART"

gs_step "1. 先以 500k 起来（无关 bitrate，只保证接口可用）"
gs_can_up 0 500000
gs_log "can0: $(gs_can_state 0)"
gs_cmd ip -details link show can0
if ip -details link show can0 | grep -qi 'termination'; then
    gs_log "注意：ip -details 里出现了 termination 属性，先记录下来"
fi

gs_step "2. 请求 120 欧终端电阻（期望失败）"
set +e
OUT=$($GS_SUDO ip link set can0 termination 120 2>&1)
RC=$?
set -e
gs_log "命令: $GS_SUDO ip link set can0 termination 120"
gs_log "退出码: $RC"
gs_log "输出  : $OUT"
printf '%s\n' "$OUT" > "$ART/termination.txt"
if [ "$RC" -ne 0 ]; then
    gs_pass "请求被拒绝（退出码 $RC）"
else
    gs_fail "请求竟然成功了（固件可能错误地 advertise 了 TERMINATION 特性）"
fi
if printf '%s' "$OUT" | grep -qiE 'not supported|not implemented|EOPNOTSUPP|Operation not supported'; then
    gs_pass "错误类型正确（不支持 / EOPNOTSUPP）"
else
    gs_log "提示: 输出里没有典型的 not supported 文案（不同 iproute2 版本文案不同），退出码为准"
fi

gs_step "3. 再试 type can 形式（另一条内核路径）"
set +e
OUT2=$($GS_SUDO ip link set can0 type can bitrate 500000 termination 120 2>&1)
RC2=$?
set -e
gs_log "命令: $GS_SUDO ip link set can0 type can bitrate 500000 termination 120"
gs_log "退出码: $RC2 / 输出: $OUT2"
if [ "$RC2" -ne 0 ]; then gs_pass "type can 形式同样被拒绝"; else gs_fail "type can 形式竟然接受 termination"; fi

gs_step "4. ip -details 不应出现 termination 属性"
DET=$(ip -details link show can0 2>&1)
gs_log "$DET"
if printf '%s' "$DET" | grep -qi 'termination'; then
    gs_fail "ip -details 里出现了 termination（说明内核认为设备支持）"
else
    gs_pass "ip -details 无 termination 属性（与不 advertise 一致）"
fi

gs_step "5. 内核安全检查"
gs_check_kernel || { gs_end; exit 9; }
$GS_SUDO ip link set can0 down 2>/dev/null || true
gs_end
