#!/usr/bin/env bash
# run_e2e_test.sh - 一键端到端测试：主机 ptp4l 主钟 + 板端 ptp_slave 从钟 + 自动统计
#
# 前提：板子空闲（独占锁）、板端固件已编译（本脚本会调 scripts/hwrun.sh，内部含编译）
# 用法:
#   sudo ./run_e2e_test.sh                    # 默认 40 s 采样，E2E
#   sudo MECH=P2P SECS=40 ./run_e2e_test.sh
# 产物: 板端串口日志 logs/<ts>-ptp_slave.log；主机日志 /tmp/ptp4l-master.log
set -euo pipefail
REPO=${REPO:-hpm5e31_1}
IFACE=${IFACE:-enp6s0}
MECH=${MECH:-E2E}
SECS=${SECS:-45}
BENCH=${BENCH:-35}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG=${LOG:-/tmp/ptp4l-master.log}

echo "=== 1) 启动主机 PTP 主钟 (${MECH}) ==="
IFACE="${IFACE}" LOG="${LOG}" MECH="${MECH}" "${HERE}/run_ptp_master.sh" start

echo "=== 2) 板端 ptp_slave 上板（${SECS}s 抓串口，板内采样 ${BENCH}s） ==="
cd "${REPO}"
bash scripts/hwrun.sh samples/app/ptp_slave --secs "${SECS}" \
     --send "sleep 1500;ptpbench ${BENCH};ptp" --lock-wait 1800 --no-build

echo "=== 3) 停止主机主钟（SIGINT 触发 ptp4l 打印统计）==="
sudo pkill -INT -x ptp4l 2>/dev/null || true
sleep 1

BOARD_LOG=$(ls -t logs/*-ptp_slave.log | head -1)
echo "=== 4) 统计 ==="
echo "[board] ${BOARD_LOG}"
python3 "${HERE}/ptp_stats.py" --log "${BOARD_LOG}" || true
echo
echo "[host ptp4l] ${LOG}"
python3 "${HERE}/ptp_stats.py" --log "${LOG}" --kind host || true
