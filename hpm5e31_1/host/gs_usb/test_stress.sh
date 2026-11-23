#!/usr/bin/env bash
# test_stress.sh —— 测试项 6：高负载（cangen 高压 + ip -s link 计数 + dmesg 安全检查）
#
# 做法：can0 用 cangen 以最小间隔发 10 s（-g 0，能压多快压多快），can1 接收；
# 统计：发送/接收帧数、帧率、ip -s link 的 dropped/errors、can 层错误计数、dmesg 有无 WARN。
#
# 注意：这是本矩阵里最容易触发内核/固件背压 bug 的用例，脚本在测试前后各做一次
# dmesg 安全检查，一旦命中 oops/WARN 立即停止（退出码 9），不再继续其它测试。
set -u
. "$(dirname "$0")/common.sh"
gs_begin "stress"

gs_require_ifaces 2 || { gs_end; exit $?; }

RATE=${BITRATE:-500000}
DURATION=${DURATION:-10}
ART=$GS_ARTIFACT_DIR/stress-$GS_TS
mkdir -p "$ART"

rx_packets() { ip -s link show "$1" | awk '/RX:/{getline; print $2; exit}'; }
tx_packets() { ip -s link show "$1" | awk '/TX:/{getline; print $2; exit}'; }
rx_dropped() { ip -s link show "$1" | awk '/RX:/{getline; getline; print $4; exit}'; }
tx_dropped() { ip -s link show "$1" | awk '/TX:/{getline; getline; print $4; exit}'; }

gs_step "1. 起两个通道（$RATE bit/s）"
gs_can_up 0 "$RATE"
gs_can_up 1 "$RATE"
gs_check_kernel || { gs_end; exit 9; }

gs_step "2. 记录基线计数"
B_TX0=$(tx_packets can0); B_RX1=$(rx_packets can1)
B_RXD1=$(rx_dropped can1); B_TXD0=$(tx_dropped can0)
gs_log "基线: can0 TX=$B_TX0 can1 RX=$B_RX1 can1 RX_drop=$B_RXD1 can0 TX_drop=$B_TXD0"

gs_step "3. cangen 满速发 $DURATION s（-g 0，随机数据，标准帧）"
T0=$(date +%s%N)
timeout "$((DURATION+5))" $GS_SUDO cangen can0 -g 0 -n 2000000 -I R -L R -D R -v > "$ART/cangen.log" 2>&1 &
CG=$!
NRX_PID=""
( timeout "$((DURATION+5))" $GS_SUDO candump -n 5000000 can1 > "$ART/candump.log" 2>&1 ) & NRX_PID=$!
sleep "$DURATION"
kill "$CG" 2>/dev/null || true
sleep 0.5
kill "$NRX_PID" 2>/dev/null || true
wait "$CG" 2>/dev/null || true
wait "$NRX_PID" 2>/dev/null || true
T1=$(date +%s%N)
ELAPSED_MS=$(( (T1-T0)/1000000 ))
gs_log "实际压测时长: $ELAPSED_MS ms"

gs_step "4. 计数与丢包"
A_TX0=$(tx_packets can0); A_RX1=$(rx_packets can1)
A_RXD1=$(rx_dropped can1); A_TXD0=$(tx_dropped can0)
D_TX0=$((A_TX0-B_TX0)); D_RX1=$((A_RX1-B_RX1))
D_RXD1=$((A_RXD1-B_RXD1)); D_TXD0=$((A_TXD0-B_TXD0))
FPS=$(awk -v n="$D_TX0" -v ms="$ELAPSED_MS" 'BEGIN{ if(ms>0) printf "%.0f", n*1000/ms; else print 0 }')
LOSS=$(awk -v tx="$D_TX0" -v rx="$D_RX1" 'BEGIN{ if(tx>0) printf "%.3f", (tx-rx)*100/tx; else print 0 }')
gs_log "can0 TX 增加 $D_TX0 帧，can1 RX 增加 $D_RX1 帧"
gs_log "发送帧率 $FPS 帧/s（$D_TX0 帧 / $ELAPSED_MS ms）"
gs_log "按 TX/RX 计数差估算丢帧: $LOSS %"
gs_log "can0 TX_dropped=$D_TXD0  can1 RX_dropped=$D_RXD1"
[ "$D_TX0" -gt 0 ] && gs_pass "can0 发出 $D_TX0 帧（$FPS 帧/s）" || gs_fail "can0 没有发出帧"
[ "$D_RX1" -gt 0 ] && gs_pass "can1 收到 $D_RX1 帧" || gs_fail "can1 没有收到帧"
if awk -v l="$LOSS" 'BEGIN{exit !(l+0 <= 1.0)}'; then
    gs_pass "丢帧率 $LOSS % <= 1%"
else
    gs_fail "丢帧率 $LOSS % 偏高"
fi

gs_step "5. 错误计数与状态（高负载后应无 bus-off/被动错误）"
for i in 0 1; do
    gs_log "can$i: $(gs_can_state $i)"
    ip -details -statistics link show "can$i" | tee -a "$GS_LOG"
done
if ip -details link show can0 | grep -q 'ERROR-ACTIVE'; then gs_pass "can0 仍为 ERROR-ACTIVE"; else gs_fail "can0 状态异常: $(gs_can_state can0)"; fi

gs_step "6. 内核安全检查（高负载最容易暴露 USB 背压/内存问题）"
gs_check_kernel || { gs_end; exit 9; }

for i in 0 1; do $GS_SUDO ip link set "can$i" down 2>/dev/null; done
gs_end
