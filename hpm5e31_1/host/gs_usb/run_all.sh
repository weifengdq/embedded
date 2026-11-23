#!/usr/bin/env bash
# run_all.sh —— gs_usb 测试矩阵总入口（安全串行，遇内核异常立即中止）
#
# 顺序按开发约定 §7.4：单帧 -> FD -> 时间戳 -> 4 通道 -> 压力 -> 模式 -> 错误 -> 终端电阻 -> 兼容
#
# 用法：
#   bash host/gs_usb/run_all.sh                       # 全套（约 3~4 分钟）
#   bash host/gs_usb/run_all.sh --fast                # 只跑 enum/classic/fd/termination（约 60 s）
#   bash host/gs_usb/run_all.sh --safe                # 只跑 enum/classic/4ch/compat/stress（不碰 FD/错误注入）
#   bash host/gs_usb/run_all.sh --only "classic fd"   # 只跑指定项
#
# 每个子测试打印一行 GS_RESULT: <PASS|FAIL>;<pass>;<fail>;<log路径>，本脚本汇总成表。
# 退出码：0=全 PASS；1=有 FAIL；9=检测到内核异常（已中止）
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
LOG_DIR=${GS_LOG_DIR:-$REPO/logs}
TS=$(date +%Y%m%d-%H%M%S)
SUMMARY="$LOG_DIR/$TS-gs-usb-matrix.log"
mkdir -p "$LOG_DIR"
: > "$SUMMARY"

FAST=0
SAFE=0
ONLY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --fast) FAST=1; shift;;
        --safe) SAFE=1; shift;;
        --only) ONLY="$2"; shift 2;;
        -h|--help) sed -n '2,16p' "$0"; exit 0;;
        *) echo "未知参数: $1"; exit 2;;
    esac
done

NAMES=()
RESULTS=()
FAILED=0
ABORTED=0

run_one() {
    # run_one <name> <timeout_s> <命令...>
    local name="$1" tmo="$2"; shift 2
    if [ -n "$ONLY" ] && ! printf '%s' "$ONLY" | tr ' ' '\n' | grep -qx "$name"; then
        return 0
    fi
    echo ""
    echo "=============================================================="
    echo "### $name   ($*)"
    echo "=============================================================="
    local out rc line
    out=$(timeout "$tmo" "$@" 2>&1)
    rc=$?
    printf '%s\n' "$out" | tail -30
    printf '%s\n' "$out" >> "$SUMMARY"
    line=$(printf '%s\n' "$out" | grep -oE 'GS_RESULT: [A-Z]+;[0-9]+;[0-9]+;.*' | tail -1)
    if [ -z "$line" ]; then
        line="GS_RESULT: FAIL;0;0;(无结果行, rc=$rc)"
    fi
    NAMES+=("$name")
    RESULTS+=("$line")
    case "$line" in
        "GS_RESULT: PASS"*) : ;;
        *) FAILED=$((FAILED+1)) ;;
    esac
    [ "$rc" = 9 ] && ABORTED=1
    echo "$line"
}

if [ "$SAFE" = 1 ]; then
    # 安全子集：不跑 FD（基线固件会在 FD 首帧后掉线）、不跑 bus-off/模式注入、不跑时间戳（基线不支持）
    run_one enum    120 bash "$HERE/test_enum.sh"
    [ "$ABORTED" = 1 ] || run_one classic 120 bash "$HERE/test_classic.sh"
    [ "$ABORTED" = 1 ] || run_one 4ch     120 bash "$HERE/test_4ch.sh"
    [ "$ABORTED" = 1 ] || run_one stress  180 bash "$HERE/test_stress.sh"
    [ "$ABORTED" = 1 ] || run_one compat  180 bash "$HERE/test_compat.sh"
else
    run_one enum        120 bash "$HERE/test_enum.sh"
    [ "$ABORTED" = 1 ] || run_one classic 120 bash "$HERE/test_classic.sh"
    [ "$ABORTED" = 1 ] || run_one fd      180 bash "$HERE/test_fd.sh"
    if [ "$FAST" = 0 ]; then
        [ "$ABORTED" = 1 ] || run_one timestamp 180 bash "$HERE/test_timestamp.sh"
        [ "$ABORTED" = 1 ] || run_one 4ch       120 bash "$HERE/test_4ch.sh"
        [ "$ABORTED" = 1 ] || run_one stress    180 bash "$HERE/test_stress.sh"
        [ "$ABORTED" = 1 ] || run_one modes     180 bash "$HERE/test_modes.sh"
        [ "$ABORTED" = 1 ] || run_one error     180 bash "$HERE/test_error.sh"
    fi
    [ "$ABORTED" = 1 ] || run_one termination 90 bash "$HERE/test_termination.sh"
    [ "$ABORTED" = 1 ] || run_one compat     180 bash "$HERE/test_compat.sh"
fi

echo ""
echo "=============================================================="
echo "### 测试矩阵汇总（日志: $SUMMARY）"
echo "=============================================================="
echo "| 测试项 | 结果 | 日志 |"
echo "|---|---|---|"
i=0
while [ "$i" -lt "${#NAMES[@]}" ]; do
    r=${RESULTS[$i]}
    verdict=$(printf '%s' "$r" | sed -E 's/GS_RESULT: ([A-Z]+);.*/\1/')
    logp=$(printf '%s' "$r" | sed -E 's/GS_RESULT: [A-Z]+;[0-9]+;[0-9]+;(.*)/\1/')
    echo "| ${NAMES[$i]} | $verdict | $logp |"
    i=$((i+1))
done
echo ""
if [ "$ABORTED" = 1 ]; then
    echo "结论: 存在内核异常（已中止），详见 $SUMMARY 与 /tmp/gs_usb_artifacts/dmesg-full-*.log"
    exit 9
fi
if [ "$FAILED" -gt 0 ]; then
    echo "结论: 有 $FAILED 项 FAIL"
    exit 1
fi
echo "结论: 全部 PASS"
exit 0
