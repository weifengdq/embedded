#!/usr/bin/env bash
# run_ptp_master.sh - 让主机成为 IEEE 1588 主钟（grandmaster），板端 ptp_slave 作为从钟同步上来
#
# 依赖: linuxptp (ptp4l/pmc)，需要 root（打开网卡的硬件时间戳）
# 用法:
#   ./run_ptp_master.sh start            # 后台启动，日志 /tmp/ptp4l-master.log
#   ./run_ptp_master.sh start -f         # 前台运行（Ctrl-C 结束，ptp4l 会打印统计）
#   ./run_ptp_master.sh stop             # 停止（发 SIGINT，ptp4l 会打印最终统计）
#   ./run_ptp_master.sh status           # 看进程 + 最近日志
#   ./run_ptp_master.sh pmc              # 用 pmc 查询当前数据集
#
# 环境变量: IFACE(默认 enp6s0) LOG DOMAIN(默认 0) MECH(E2E|P2P，默认 E2E)
set -euo pipefail
IFACE=${IFACE:-enp6s0}
LOG=${LOG:-/tmp/ptp4l-master.log}
DOMAIN=${DOMAIN:-0}
MECH=${MECH:-E2E}
if [ "${MECH}" = "P2P" ]; then DM="-P"; else DM="-E"; fi
CMD=(ptp4l -i "${IFACE}" -m "${DM}" -H -D 0)

case "${1:-status}" in
  start)
    if pgrep -x ptp4l >/dev/null 2>&1; then echo "ptp4l already running (pid $(pgrep -x ptp4l | head -1))"; exit 0; fi
    if [ "${2:-}" = "-f" ]; then
      echo "foreground: sudo ${CMD[*]}"
      exec sudo "${CMD[@]}"
    fi
    sudo sh -c "nohup ${CMD[*]} > ${LOG} 2>&1 &"
    sleep 2
    echo "started as master on ${IFACE} (mech=${MECH}), log=${LOG}"
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
    sudo pmc -u -b 0 'GET CURRENT_DATA_SET' 'GET PARENT_DATA_SET' 'GET TIME_STATUS_NP'
    ;;
  *)
    echo "usage: $0 {start [-f]|stop|status|pmc}"; exit 2;;
esac
