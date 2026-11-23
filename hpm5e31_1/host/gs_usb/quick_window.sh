#!/usr/bin/env bash
# quick_window.sh —— 90 秒短窗口：只做「枚举 + 经典帧」两件事，给"板卡紧张/固件还在调"时用
#
# 用法：bash host/gs_usb/quick_window.sh [--secs 90]
# 特点：
#   * 全程 ≤90 s（默认 75 s 预算，留余量）；
#   * 只跑 test_enum.sh + test_classic.sh（不发 FD、不注入错误、不改任何持久配置）；
#   * 每步后做「设备是否还在」检查，掉线立即停并保存 dmesg；
#   * 最后输出一段可直接贴进日报的摘要（接口数/驱动/经典帧计数/日志路径）。
set -u
. "$(dirname "$0")/common.sh"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUDGET=${1:-75}
[ "${1:-}" = "--secs" ] && BUDGET=${2:-75}
TS=$(date +%Y%m%d-%H%M%S)
OUT="$GS_LOG_DIR/$TS-gs-usb-window.log"

echo "== gs_usb 90s 短窗口 $(date '+%F %T')（预算 $BUDGET s）==" | tee -a "$OUT"

# --- 0) 设备在不在 ---
if ! DEV=$(gs_usb_present); then
    echo "FAIL: 设备 1209:2323 不在（请确认固件在跑 / USB 插好）" | tee -a "$OUT"
    exit 2
fi
echo "设备: sysfs $DEV" | tee -a "$OUT"

# --- 1) 枚举 ---
echo "" | tee -a "$OUT"
echo "---- [1/2] enum ----" | tee -a "$OUT"
ENUM_LOG=$(timeout 45 bash "$HERE/test_enum.sh" 2>&1)
echo "$ENUM_LOG" | tail -20 | tee -a "$OUT"
ENUM_LINE=$(printf '%s\n' "$ENUM_LOG" | grep -oE 'GS_RESULT: [A-Z]+;[0-9]+;[0-9]+;.*' | tail -1)
echo "$ENUM_LINE" | tee -a "$OUT"
if ! gs_usb_present >/dev/null; then
    echo "FAIL: 枚举后设备掉线（固件/驱动问题），停止窗口" | tee -a "$OUT"
    gs_dmesg_tail_save "enum-lost"
    exit 9
fi

# --- 2) 经典帧 ---
echo "" | tee -a "$OUT"
echo "---- [2/2] classic ----" | tee -a "$OUT"
CLS_LOG=$(timeout 45 bash "$HERE/test_classic.sh" 2>&1)
printf '%s\n' "$CLS_LOG" | tail -24 | tee -a "$OUT"
CLS_LINE=$(printf '%s\n' "$CLS_LOG" | grep -oE 'GS_RESULT: [A-Z]+;[0-9]+;[0-9]+;.*' | tail -1)
echo "$CLS_LINE" | tee -a "$OUT"

# --- 3) 存活 + 摘要 ---
ALIVE="是"
gs_usb_present >/dev/null || ALIVE="否"
NIF=$(gs_count_ifaces)
DRV=$(basename "$(readlink -f /sys/class/net/can0/device/driver 2>/dev/null)" 2>/dev/null)
echo "" | tee -a "$OUT"
echo "== 摘要 ==" | tee -a "$OUT"
echo "| 项 | 值 |" | tee -a "$OUT"
echo "|---|---|" | tee -a "$OUT"
echo "| 接口数 | $NIF |" | tee -a "$OUT"
echo "| can0 驱动 | $DRV |" | tee -a "$OUT"
echo "| enum | $(printf '%s' "$ENUM_LINE" | sed -E 's/GS_RESULT: ([A-Z]+);.*/\1/') |" | tee -a "$OUT"
echo "| classic | $(printf '%s' "$CLS_LINE" | sed -E 's/GS_RESULT: ([A-Z]+);.*/\1/') |" | tee -a "$OUT"
echo "| 窗口后设备在线 | $ALIVE |" | tee -a "$OUT"
echo "| 窗口日志 | $OUT |" | tee -a "$OUT"
echo "窗口结束 $(date '+%F %T')" | tee -a "$OUT"

[ "$ALIVE" = "是" ] || exit 9
case "$ENUM_LINE$CLS_LINE" in
    *FAIL*) exit 1;;
esac
exit 0
