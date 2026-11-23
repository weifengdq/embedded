#!/usr/bin/env bash
# host/mqtt/bench_evidence.sh - 连续跑两次板->主机 QoS1 吞吐并把 host 侧输出归档到 logs/
# 目的：让 "1000 条 / 55 ms / 18179 msg/s" 这类主机侧计时能追到 logs（U-38-12）。
# 第一次常因 lwIP 发送队列起步（TCP_SND_QUEUELEN=16）只发出几十到二百条；第二次通常 1000/1000。
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$REPO/logs/${TS}-host-mqtt-bench.log"
{
  echo "# host/mqtt bench evidence  ${TS}"
  echo "# broker: 192.168.2.1:1883   board: 192.168.2.100"
  echo "# run1 (cold):"
  bash "$HERE/bench.sh" 1000 1 1
  echo "# run2 (warm):"
  bash "$HERE/bench.sh" 1000 1 1
} 2>&1 | tee "$LOG"
echo "[bench_evidence] archived -> $LOG"
