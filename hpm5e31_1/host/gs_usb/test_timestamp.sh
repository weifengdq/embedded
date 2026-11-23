#!/usr/bin/env bash
# test_timestamp.sh —— 测试项 5 的 shell 包装：调 test_timestamp.py 并把结果写成统一格式
#
# 单独跑：bash host/gs_usb/test_timestamp.sh
# 参数： 同 test_timestamp.py（--count/--gap/--bitrate/--no-config）
set -u
. "$(dirname "$0")/common.sh"
gs_begin "timestamp"

PY=${PYTHON:-$HOME/pyenv/bin/python}
if [ ! -x "$PY" ]; then
    gs_fail "找不到 python: $PY（用 PYTHON=... 指定）"
    gs_end; exit $?
fi
gs_step "1. ethtool -T（能力查询）"
for i in 0 1; do
    [ -d "/sys/class/net/can$i" ] || continue
    gs_cmd ethtool -T "can$i"
done

gs_step "2. SO_TIMESTAMPING 采集"
OUT=$($GS_SUDO "$PY" "$GS_LIB_DIR/test_timestamp.py" --json "$@")
RC=$?
gs_log "$OUT"
printf '%s\n' "$OUT" > "$GS_ARTIFACT_DIR/timestamp-$GS_TS.json"

VERDICT=$(printf '%s' "$OUT" | sed -nE 's/.*"verdict": "([A-Z]+)".*/\1/p')
REASON=$(printf '%s' "$OUT" | sed -nE 's/.*"reason": "([^"]*)".*/\1/p')
HWT=$(printf '%s' "$OUT" | sed -nE 's/.*"hw_ts": ([0-9]+).*/\1/p')
RXN=$(printf '%s' "$OUT" | sed -nE 's/.*"count_recv": ([0-9]+).*/\1/p')
if [ "${VERDICT:-}" = "PASS" ]; then
    gs_pass "硬件时间戳: 收到 ${RXN:-?} 帧, 其中硬件戳 ${HWT:-?} 个"
else
    gs_fail "硬件时间戳未通过: ${REASON:-未知原因} (rc=$RC)"
fi
gs_check_kernel || { gs_end; exit 9; }
gs_end
