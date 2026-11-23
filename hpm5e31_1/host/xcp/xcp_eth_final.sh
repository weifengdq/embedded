#!/bin/bash
# =============================================================================
# xcp_eth_final.sh —— XCP on UDP（Ethernet）最终证据跑
#
# 同一固件内依次跑 rate（1 kHz/4 ODT）/ stress（10 kHz/8 ODT）/ 30 s 长测，并在
# 前后各做一次 ping 对照（区分板端丢包与链路丢包）。
#
# 用法：bash host/xcp/xcp_eth_final.sh
# 依赖：pyxcp；板端 samples/app/xcp_eth 固件已烧写；主机网口已配 192.168.2.1/24
# 输出：out/xcp_eth_final/{final_rate,final_stress,final_long30}.txt + CSV
# =============================================================================
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
[ -n "${PY:-}" ] || PY=python3
A2L=samples/app/xcp_eth/xcp_eth.a2l
[ -n "${T:-}" ] || T=out/xcp_eth_final
[ -n "${BOARD_IP:-}" ] || BOARD_IP=192.168.2.100
mkdir -p "$T" out

echo "=== [0] ping before DAQ（链路丢包对照）==="
ping -c 10 -i 0.2 -W 1 "$BOARD_IP" | tail -2
echo "=== [1] rate：EVT0 1 kHz / 4 ODT / 6 s ==="
$PY host/xcp/xcp_daq.py --a2l $A2L --interface eth --plan rate --secs 6 --csv out/xcp_eth_rate.csv > "$T/final_rate.txt" 2>&1
echo "rate rc=$?"
echo "=== [2] stress：EVT1 10 kHz / 8 ODT / 6 s ==="
$PY host/xcp/xcp_daq.py --a2l $A2L --interface eth --plan stress --secs 6 --csv out/xcp_eth_stress.csv > "$T/final_stress.txt" 2>&1
echo "stress rc=$?"
echo "=== [3] long30：EVT0 1 kHz / 4 ODT / 30 s ==="
$PY host/xcp/xcp_daq.py --a2l $A2L --interface eth --secs 30 --csv out/xcp_eth_long30.csv > "$T/final_long30.txt" 2>&1
echo "long rc=$?"
echo "=== [4] ping after DAQ ==="
ping -c 10 -i 0.2 -W 1 "$BOARD_IP" | tail -2
echo "=== 摘要 ==="
grep -hE "DAQ (PASS|FAIL)|板端统计|事件率|消息率|数据报率|丢包率" "$T"/final_rate.txt "$T"/final_stress.txt "$T"/final_long30.txt
echo "ALL-DONE"
