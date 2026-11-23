#!/usr/bin/env bash
# 用法: ./run_monitor.sh [vcan0] [秒数]
set -euo pipefail
PY="${PY:-$HOME/pyenv/bin/python}"
[ -x "$PY" ] || PY=python3
IFACE="${1:-vcan0}"
SECS="${2:-5}"
exec "$PY" "$(dirname "$0")/j1939_monitor.py" -i "$IFACE" --duration "$SECS"
