#!/usr/bin/env bash
# run_retry.sh —— 按需重试的测试驱动（固件调试期会短暂断连，允许"重枚举后重试一次"）
#
# 用法：
#   bash host/gs_usb/run_retry.sh enum classic            # 跑指定项，失败且设备掉线时重试一次
#   bash host/gs_usb/run_retry.sh --no-retry enum         # 不重试
#
# 行为：
#   1) 每项执行前确认设备在（sysfs 1209:2323），不在则最多等 30 s 重枚举；
#   2) 测试失败且"设备已消失"时：等设备回来（最多 30 s）后重跑一次，日志里记 RETRY；
#   3) 每次重试都保存当时的 dmesg 尾部，便于分析掉线原因；
#   4) 汇总成 Markdown 表（含 RETRY 标记），退出码 0/1/9 同 run_all.sh。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LOG_DIR=${GS_LOG_DIR:-$(cd "$HERE/../.." && pwd)/logs}
TS=$(date +%Y%m%d-%H%M%S)
SUMMARY="$LOG_DIR/$TS-gs-usb-retry.log"
RETRY=1
ARGS=()
for a in "$@"; do
    case "$a" in
        --no-retry) RETRY=0;;
        *) ARGS+=("$a");;
    esac
done
[ ${#ARGS[@]} -gt 0 ] || { echo "用法: bash host/gs_usb/run_retry.sh [--no-retry] <test> [test...]"; exit 2; }
mkdir -p "$LOG_DIR"; : > "$SUMMARY"

present() {
    local d v p
    for d in /sys/bus/usb/devices/*/; do
        [ -f "$d/idVendor" ] || continue
        v=$(cat "$d/idVendor" 2>/dev/null); p=$(cat "$d/idProduct" 2>/dev/null)
        [ "$v" = "1209" ] && [ "$p" = "2323" ] && return 0
    done
    return 1
}
wait_dev() {
    local tmo=${1:-30} i
    for i in $(seq 1 $((tmo*5))); do present && return 0; sleep 0.2; done
    return 1
}
tail_dmesg() {
    sudo -n dmesg 2>/dev/null | grep -iE 'gs_usb|usb 1-|can[0-9]|oops|WARN' | tail -20
}

NAMES=() VERDICTS=() LOGS=() NOTES=()

run_item() {
    local name="$1"
    local runner="$HERE/test_$name.sh"
    local attempt=1 note="" out rc line dev_lost=0
    if [ ! -f "$runner" ]; then
        echo "跳过 $name（没有 $runner）"; return 0
    fi
    if ! present; then
        echo "[$name] 设备不在，等最多 30 s 重枚举…"
        wait_dev 30 || { NAMES+=("$name"); VERDICTS+=("FAIL"); LOGS+=("-"); NOTES+=("设备不在，无法开始"); return 0; }
    fi
    while [ "$attempt" -le 2 ]; do
        echo ""
        echo "======== $name (attempt $attempt) ========"
        out=$(timeout 240 bash "$runner" 2>&1); rc=$?
        printf '%s\n' "$out" | tail -26
        printf '%s\n' "$out" >> "$SUMMARY"
        line=$(printf '%s\n' "$out" | grep -oE 'GS_RESULT: [A-Z]+;[0-9]+;[0-9]+;.*' | tail -1)
        [ -z "$line" ] && line="GS_RESULT: FAIL;0;0;(无结果行, rc=$rc)"
        if printf '%s' "$line" | grep -q 'GS_RESULT: PASS'; then break; fi
        if ! present; then
            dev_lost=1
            echo "[$name] 设备已掉线（attempt $attempt）——保存 dmesg"
            tail_dmesg | tee -a "$SUMMARY"
            sudo -n dmesg 2>/dev/null | tail -300 > "$LOG_DIR/$TS-gs-usb-$name-drop$attempt.log"
            if [ "$RETRY" = 1 ] && [ "$attempt" -lt 2 ]; then
                echo "[$name] 等设备重枚举后重试一次…"
                if wait_dev 30; then attempt=$((attempt+1)); note="RETRY(掉线后重跑)"; continue; fi
            fi
            break
        fi
        break
    done
    NAMES+=("$name"); VERDICTS+=("$(printf '%s' "$line" | sed -E 's/GS_RESULT: ([A-Z]+);.*/\1/')")
    LOGS+=("$(printf '%s' "$line" | sed -E 's/GS_RESULT: [A-Z]+;[0-9]+;[0-9]+;(.*)/\1/')")
    NOTES+=("${note:-};rc=$rc;dev_lost=$dev_lost")
    echo "$line"
}

for n in "${ARGS[@]}"; do run_item "$n"; done

echo ""
echo "| 测试项 | 结果 | 日志 | 备注 |"
echo "|---|---|---|---|"
i=0
while [ "$i" -lt "${#NAMES[@]}" ]; do
    echo "| ${NAMES[$i]} | ${VERDICTS[$i]} | ${LOGS[$i]} | ${NOTES[$i]} |"
    i=$((i+1))
done
echo ""
echo "汇总日志: $SUMMARY"
FAILN=0
for v in "${VERDICTS[@]}"; do [ "$v" = "PASS" ] || FAILN=$((FAILN+1)); done
[ "$FAILN" -eq 0 ] && { echo "结论: 全部 PASS"; exit 0; }
echo "结论: $FAILN 项未通过"; exit 1
