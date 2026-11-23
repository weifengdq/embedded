#!/usr/bin/env bash
# host/mqtt/run_suite.sh - 一次持锁窗口内跑完主机侧 MQTT 联调（配合 scripts/hwrun.sh --post）
#
# 设计目的：固件烧写 + 串口抓取 + 上位机测试绑在同一把整机锁里，避免"日志名是 A、
# 内容是 B"；同时按阶段（stage）切分，方便复跑单组数据。
#
# 用法：
#   bash host/mqtt/run_suite.sh basic              # 订阅校验 + 命令回执 + 回声时延
#   bash host/mqtt/run_suite.sh stress             # 1000 条 QoS1/QoS0 吞吐 + 逐条 RTT
#   bash host/mqtt/run_suite.sh lwt                # kill(abort) 遗嘱 + 自动重连
#   bash host/mqtt/run_suite.sh keepalive          # keepalive=10s 保活 35s（PINGREQ/PINGRESP）
#   bash host/mqtt/run_suite.sh reboot             # 软复位 + 遗嘱超时（keepalive=10s）
#   bash host/mqtt/run_suite.sh all                # basic + stress + lwt
#
# 环境变量（可选）：
#   SUITE_SERIAL=1        后台抓串口（默认关；打开时窗口会等到 SUITE_SERIAL_SECS 结束）
#   SUITE_SERIAL_SECS=75  串口抓取时长
#   SUITE_PCAP=1          用 tcpdump 抓 enp6s0:1883（需要免密 sudo；产物 .pcap + .txt）
#
# 每个阶段结束都会归档：
#   logs/<ts>-host-mqtt-broker-<stage>.log   broker 侧日志切片
#   logs/<ts>-host-mqtt-serial-<stage>.log   板端串口（SUITE_SERIAL=1）
#   logs/<ts>-host-mqtt-<stage>.pcap/.txt    MQTT 线上报文（SUITE_PCAP=1）
#   logs/<ts>-host-mqtt-sub-check.json 等    各测试脚本的 JSON 报告
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
STAGE="${1:-basic}"
OUTDIR="$REPO/logs"
TS="$(date +%Y%m%d-%H%M%S)"
BLOG=$HOME/tmp2/mosq_test.log
PCAP="$OUTDIR/${TS}-host-mqtt-${STAGE}.pcap"
MARK=0
[ -f "$BLOG" ] && MARK="$(wc -l < "$BLOG")"
RC=0
SER_PID=""

slice_broker() {
  local dst="$OUTDIR/${TS}-host-mqtt-broker-${STAGE}.log"
  if [ -f "$BLOG" ]; then
    tail -n +"$((MARK + 1))" "$BLOG" > "$dst"
    echo "[suite] broker log slice -> $dst ($(wc -l < "$dst") 行)"
  fi
}

start_serial() {
  [ "${SUITE_SERIAL:-0}" = "1" ] || return 0
  local f="$OUTDIR/${TS}-host-mqtt-serial-${STAGE}.log"
  python3 "$REPO/scripts/serial_cap.py" --port /dev/ttyACM0 --baud 921600 \
      --secs "${SUITE_SERIAL_SECS:-60}" --out "$f" &
  SER_PID=$!
  echo "[suite] 串口后台抓取 -> $f (pid=$SER_PID)"
}

stop_serial() {
  # serial_cap.py 只在退出时落盘，这里等它自然结束（SUITE_SERIAL_SECS 秒）
  if [ -n "$SER_PID" ]; then wait "$SER_PID" 2>/dev/null || true; fi
}

start_pcap() {
  [ "${SUITE_PCAP:-0}" = "1" ] || return 0
  sudo -n tcpdump -i any -s 0 -U -w "$PCAP" 'tcp port 1883' >/dev/null 2>&1 &
  sleep 0.5
  echo "[suite] tcpdump 抓 any:1883（含 lo，方便看 broker->订阅者这一跳）-> $PCAP"
}

stop_pcap() {
  [ "${SUITE_PCAP:-0}" = "1" ] || return 0
  sudo -n pkill -f "tcpdump -i any" 2>/dev/null || true
  sleep 0.5
  sudo -n chown z:z "$PCAP" 2>/dev/null || true
  if [ -s "$PCAP" ]; then
    tcpdump -r "$PCAP" -tttt -nn > "${PCAP%.pcap}.txt" 2>/dev/null || true
    echo "[suite] pcap 文本 -> ${PCAP%.pcap}.txt ($(wc -l < "${PCAP%.pcap}.txt") 行)"
  else
    echo "[suite] pcap 为空（tcpdump 是否启动失败？）"
  fi
}

cleanup() {
  stop_serial
  stop_pcap
  slice_broker
}
trap cleanup EXIT

step() {
  echo ""
  echo "===== $* ====="
  "$@" || RC=1
}

cd "$REPO" || exit 2
start_serial
start_pcap

case "$STAGE" in
  basic)
    step python3 host/mqtt/sub_check.py --secs 10
    step python3 host/mqtt/pub_cmd.py --cmd led --arg on
    step python3 host/mqtt/pub_cmd.py --cmd period --arg 500
    step python3 host/mqtt/pub_cmd.py --cmd cansend --arg 0x123,11,22,33,44
    step python3 host/mqtt/pub_cmd.py --cmd echo --echo-n 20
    step python3 host/mqtt/pub_cmd.py --cmd period --arg 1000
    ;;
  stress)
    # burst：不限速，暴露"上位机 paho 自身瓶颈"（第 38 章坑 6）
    step python3 host/mqtt/stress.py --n 1000 --qos 1 --mode both
    # steady：限速 200 msg/s，测板端稳态往返时延
    step python3 host/mqtt/stress.py --n 1000 --qos 1 --mode throughput --rate 200
    step python3 host/mqtt/stress.py --n 1000 --qos 0 --mode throughput --rate 200
    # 板->主机吞吐：C 客户端计数
    step bash host/mqtt/bench.sh 1000 1
    ;;
  lwt)
    step python3 host/mqtt/lwt_check.py --mode kill
    ;;
  bench)
    step bash host/mqtt/bench.sh 1000 1
    ;;
  lwtall)
    step python3 host/mqtt/lwt_check.py --mode kill
    step python3 host/mqtt/lwt_check.py --mode keepalive --secs 35 --broker-log "$BLOG"
    step python3 host/mqtt/lwt_check.py --mode reboot --secs 40 --broker-log "$BLOG"
    ;;
  keepalive)
    step python3 host/mqtt/lwt_check.py --mode keepalive --secs 35 --broker-log "$BLOG"
    ;;
  reboot)
    step python3 host/mqtt/lwt_check.py --mode reboot --secs 40 --broker-log "$BLOG"
    ;;
  keepalive_reboot)
    step python3 host/mqtt/lwt_check.py --mode keepalive --secs 35 --broker-log "$BLOG"
    step python3 host/mqtt/lwt_check.py --mode reboot --secs 40 --broker-log "$BLOG"
    ;;
  all)
    step python3 host/mqtt/sub_check.py --secs 10
    step python3 host/mqtt/pub_cmd.py --cmd led --arg on
    step python3 host/mqtt/pub_cmd.py --cmd period --arg 500
    step python3 host/mqtt/pub_cmd.py --cmd cansend --arg 0x123,11,22,33,44
    step python3 host/mqtt/pub_cmd.py --cmd echo --echo-n 20
    step python3 host/mqtt/pub_cmd.py --cmd period --arg 1000
    step python3 host/mqtt/stress.py --n 1000 --qos 1 --mode both
    step python3 host/mqtt/lwt_check.py --mode kill
    ;;
  *)
    echo "usage: $0 basic|stress|lwt|keepalive|reboot|all"; exit 1 ;;
esac

echo ""
echo "[suite] stage=$STAGE rc=$RC"
exit "$RC"