#!/usr/bin/env bash
# run_client.sh -- 跑 DoIP 上位机（板端需在跑 samples/app/doip_server）
# 用法: ./run_client.sh [discover|announce|entity|route|uds|bulk|err|all] [额外参数...]
#   all 建议板端先打开周期声明与短 general 定时器：
#     bash scripts/hwrun.sh app/doip_server --secs 40 \
#          --send "doip timer period 1000;doip timer general 3000" \
#          --post "timeout 120 bash host/doip/run_client.sh all" --lock-wait 1800
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${PYTHON:-$HOME/pyenv/bin/python}"
[ -x "${PY}" ] || PY=python3
MODE="${1:-all}"; shift || true
BOARD="${BOARD_IP:-192.168.2.100}"
LOCAL="${LOCAL_IP:-192.168.2.1}"
repo="$(cd "${here}/../.." && pwd)"
mkdir -p "${repo}/logs"
LOG="${repo}/logs/$(date +%Y%m%d-%H%M%S)-host-doip-${MODE}.log"
echo "[run_client] mode=${MODE} board=${BOARD} local=${LOCAL}"
echo "[run_client] host log: ${LOG}"
set -o pipefail
timeout 150 "${PY}" "${here}/doip_client.py" --board "${BOARD}" --local "${LOCAL}" "${MODE}" "$@" 2>&1 | tee "${LOG}"
rc="${PIPESTATUS[0]}"
echo "[run_client] rc=${rc}"
exit "${rc}"
