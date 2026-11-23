#!/usr/bin/env bash
# host/mqtt/bench.sh - 板->主机 QoS1 吞吐（用 C 客户端 mosquitto_sub 计数，避开 paho 单线程瓶颈）
#
# 用法： bash host/mqtt/bench.sh [n] [qos]
#  1) 先起 mosquitto_sub 订阅 hpm5e31/bench（-C n，收满 n 条退出）
#  2) 再向 hpm5e31/cmd/bench 发 "n,qos"，让板端连发 n 条
#  3) 统计：收到条数、丢包、总耗时、速率（板端自报的 bench/done 也会从 broker 日志看）
#
# 为什么不用 python paho：paho 单线程回调处理 1000 条瞬时小报文时自身会成为瓶颈
# （第 38 章实测：paho 订阅端 e2e 时延被自身处理拖到 1.4 s，甚至触发 QoS0 丢包），
# 而 mosquitto_sub 是 C 实现，能把板端真实吞吐测出来。
set -uo pipefail
HOST=192.168.2.1
N="${1:-1000}"
QOS="${2:-1}"
SUBQ="${3:-$QOS}"   # 订阅 QoS：与发布 QoS 解耦，便于对比 broker->订阅者 的确认开销
TMP="$(mktemp /tmp/mqtt-bench.XXXXXX)"

mosquitto_sub -h "$HOST" -p 1883 -t hpm5e31/bench -q "$SUBQ" -C "$N" -W 20 >"$TMP" 2>/dev/null &
SPID=$!
sleep 0.4
T0=$(date +%s.%N)
mosquitto_pub -h "$HOST" -p 1883 -t hpm5e31/cmd/bench -m "$N,$QOS" -q 1
wait "$SPID" 2>/dev/null
T1=$(date +%s.%N)
RECV=$(wc -l <"$TMP")
LOSS=$((N - RECV))
DT=$(awk -v a="$T0" -v b="$T1" 'BEGIN{printf "%.3f", b-a}')
RATE=$(awk -v n="$RECV" -v a="$T0" -v b="$T1" 'BEGIN{ if (b>a) printf "%.1f", n/(b-a); else print "0" }')
echo "[bench] n=$N pub_qos=$QOS sub_qos=$SUBQ recv=$RECV loss=$LOSS elapsed=${DT}s rate=${RATE} msg/s"
rm -f "$TMP"
[ "$LOSS" -le $((N / 100)) ] && echo "[bench] => PASS" || { echo "[bench] => FAIL"; exit 1; }