#!/usr/bin/env bash
# run_client.sh -- 跑自写 SOME/IP Python 客户端（板端需在跑对应例程）
# 用法: run_client.sh [discover|core|rpc|event|tp|all] [额外参数...]
#   板端 someip_rpc   -> core（discover + rpc + event）
#   板端 someip_event -> event
#   板端 someip_tp    -> tp --size 4096
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${PYTHON:-$HOME/pyenv/bin/python}"
MODE="${1:-core}"; shift || true
BOARD="${BOARD_IP:-192.168.2.100}"
LOCAL="${LOCAL_IP:-192.168.2.1}"
ARGS=()
case "${MODE}" in
  discover) ARGS=(discover --timeout 3 --watch 5);;
  core)     ARGS=(core --secs 6 --count 50 --timeout 3 --watch 4);;
  rpc)      ARGS=(rpc --count 50);;
  event)    ARGS=(event --secs 6 --ttl 3);;
  tp)       ARGS=(tp --size 4096);;
  all)      ARGS=(all --secs 6 --count 50 --size 4096 --timeout 3 --watch 4);;
  *)        echo "usage: $0 [discover|core|rpc|event|tp|all]"; exit 2;;
esac
echo "[run_client] mode=${MODE} board=${BOARD} local=${LOCAL}"
repo="$(cd "${here}/../.." && pwd)"
mkdir -p "${repo}/logs"
LOG="${repo}/logs/$(date +%Y%m%d-%H%M%S)-host-someip-${MODE}.log"
echo "[run_client] host log: ${LOG}"
set -o pipefail
timeout 70 "${PY}" "${here}/someip_client.py" --board "${BOARD}" --local "${LOCAL}" "${ARGS[@]}" "$@" 2>&1 | tee "${LOG}"
exit "${PIPESTATUS[0]}"
