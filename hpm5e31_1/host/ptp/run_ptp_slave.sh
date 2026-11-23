#!/usr/bin/env bash
# run_ptp_slave.sh - 让主机成为 IEEE 1588 从钟（clientOnly），用来验证板端 ptp_master 的反方向同步
#
# 用法:
#   ./run_ptp_slave.sh start      # 后台启动，日志 /tmp/ptp4l-slave.log
#   ./run_ptp_slave.sh stop
#   ./run_ptp_slave.sh status
#   ./run_ptp_slave.sh pmc
#   ./run_ptp_slave.sh stats      # 调 ptp_stats.py 统计 /tmp/ptp4l-slave.log
#
# 环境变量: IFACE(默认 enp6s0) LOG DOMAIN MECH
set -euo pipefail
IFACE=${IFACE:-enp6s0}
LOG=${LOG:-/tmp/ptp4l-slave.log}
DOMAIN=${DOMAIN:-0}
MECH=${MECH:-E2E}
if [ "${MECH}" = "P2P" ]; then DM="-P"; else DM="-E"; fi
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# -s = clientOnly：强制自己当从钟，不参与 BMCA 竞争
CMD=(ptp4l -i "${IFACE}" -m "${DM}" -H -s)

case "${1:-status}" in
  start)
    if pgrep -x ptp4l >/dev/null 2>&1; then echo "ptp4l already running (pid $(pgrep -x ptp4l | head -1))"; exit 0; fi
    sudo sh -c "nohup ${CMD[*]} > ${LOG} 2>&1 &"
    sleep 2
    echo "started as slave (clientOnly) on ${IFACE} (mech=${MECH}), log=${LOG}"
    tail -5 "${LOG}" || true
    ;;
  stop)
    sudo pkill -INT -x ptp4l 2>/dev/null || true
    sleep 1
    echo "stopped; last log:"; tail -12 "${LOG}" 2>/dev/null || true
    ;;
  status)
    pgrep -a ptp4l || echo "ptp4l not running"
    echo "--- ${LOG} (tail) ---"; tail -15 "${LOG}" 2>/dev/null || true
    ;;
  pmc)
    sudo pmc -u -b 0 'GET CURRENT_DATA_SET' 'GET PARENT_DATA_SET'
    ;;
  stats)
    python3 "${HERE}/ptp_stats.py" --log "${LOG}" --kind host
    ;;
  bench)
    # 一键：起从钟 → 等 N 秒 → 停 → 打印 host 侧 master offset 统计（供 hwrun --post 用）
    BSECS=${2:-40}
    sudo pkill -INT -x ptp4l 2>/dev/null || true
    sleep 0.5
    sudo sh -c "nohup ${CMD[*]} > ${LOG} 2>&1 &"
    echo "ptp4l slave started on ${IFACE} (mech=${MECH}), sampling ${BSECS}s ..."
    sleep "${BSECS}"
    sudo pkill -INT -x ptp4l 2>/dev/null || true
    sleep 1
    tail -6 "${LOG}" || true
    python3 "${HERE}/ptp_stats.py" --log "${LOG}" --kind host
    ;;
  *)
    echo "usage: $0 {start|stop|status|pmc|stats|bench [secs]}"; exit 2;;
esac