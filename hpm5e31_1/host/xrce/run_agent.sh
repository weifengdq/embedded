#!/usr/bin/env bash
# run_agent.sh —— 启动 Micro-XRCE-DDS Agent（UDP4，默认 2018）
#
# 用法：
#   bash host/xrce/run_agent.sh                      # 前台，udp4 -p 2018
#   PORT=2019 VERBOSE=4 bash host/xrce/run_agent.sh
#   bash host/xrce/run_agent.sh -d                   # 后台，日志 /tmp/xrce_agent.log
#
# 依赖：先跑 build_agent.sh；ROS 2 环境（Agent 用 Fast DDS 3.6.2 做 ROS 2 侧发现）
set -euo pipefail

WORK="${XRCE_BUILD_DIR:-${TMPDIR:-/tmp}/xrce_agent_build}"
AGENT="${XRCE_AGENT_BIN:-${WORK}/agent_install/bin/MicroXRCEAgent}"
PORT="${PORT:-2018}"
VERBOSE="${VERBOSE:-4}"
TRANSPORT="${TRANSPORT:-udp4}"
LOG="${XRCE_AGENT_LOG:-/tmp/xrce_agent.log}"
DAEMON=0
[ "${1:-}" = "-d" ] && DAEMON=1

[ -x "$AGENT" ] || { echo "ERROR: 找不到 $AGENT，先跑 bash host/xrce/build_agent.sh"; exit 3; }
# shellcheck disable=SC1091
source "${ROS_DIR:-/opt/ros/lyrical}/setup.bash"
export LD_LIBRARY_PATH="${WORK}/agent_install/lib:${LD_LIBRARY_PATH:-}"

run() { exec "$AGENT" "$TRANSPORT" -p "$PORT" -v "$VERBOSE"; }

if [ "$DAEMON" = 1 ]; then
  pkill -f "MicroXRCEAgent .* -p $PORT" 2>/dev/null || true
  sleep 0.3
  nohup "$AGENT" "$TRANSPORT" -p "$PORT" -v "$VERBOSE" > "$LOG" 2>&1 &
  sleep 1
  ss -lunp 2>/dev/null | grep -F ":$PORT" && echo "AGENT_RUNNING port=$PORT log=$LOG"
else
  echo "[run_agent] $TRANSPORT -p $PORT -v $VERBOSE（Ctrl-C 结束）"
  run
fi
