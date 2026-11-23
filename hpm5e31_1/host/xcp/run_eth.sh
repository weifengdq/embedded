#!/usr/bin/env bash
# run_eth.sh —— XCPonEth 一键测试（Bench C：主机 enp6s0 192.168.2.1 直连板 192.168.2.100:5555）
#
# 前置：板端已烧写 samples/app/xcp_eth 并上电起网（串口出现 "XCP-ETH INIT PASS"）。
# 用法：bash host/xcp/run_eth.sh [秒数]        # 默认 rate 档
#       XCP_PLAN=stress bash host/xcp/run_eth.sh 6
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PY=$HOME/pyenv/bin/python
BOARD_IP="${BOARD_IP:-192.168.2.100}"
SECS="${1:-8}"
PLAN="${XCP_PLAN:-rate}"
cd "$REPO"

echo "===== 0) 预检：ping 板 $BOARD_IP ====="
if ! ping -c 3 -W 1 "$BOARD_IP" >/dev/null 2>&1; then
    echo "ERROR: ping 不通 $BOARD_IP（确认板已上电、烧的是 samples/app/xcp_eth、主机 enp6s0 是 192.168.2.1）"
    exit 2
fi

echo "===== 1) XCPonEth $PLAN 档，采集 $SECS s ====="
"$PY" host/xcp/xcp_daq.py --a2l samples/app/xcp_eth/xcp_eth.a2l \
      --interface eth --eth-host "$BOARD_IP" --eth-port 5555 \
      --plan "$PLAN" --secs "$SECS" --csv "out/xcp_eth_${PLAN}.csv"
