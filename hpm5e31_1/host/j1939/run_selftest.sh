#!/usr/bin/env bash
# 建立 vcan0（如果不存在）+ 跑主机侧 J1939 TP 自测
set -euo pipefail
PY="${PY:-$HOME/pyenv/bin/python}"
[ -x "$PY" ] || PY=python3
IFACE="${1:-vcan0}"
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  sudo modprobe vcan
  sudo ip link add dev "$IFACE" type vcan
  sudo ip link set up "$IFACE"
fi
echo "=== j1939_tp_test.py selftest ($IFACE) ==="
"$PY" "$(dirname "$0")/j1939_tp_test.py" --mode selftest -i "$IFACE"
