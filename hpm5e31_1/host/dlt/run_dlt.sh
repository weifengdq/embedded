#!/usr/bin/env bash
# Bench C：板端 dlt_log -> 主机 UDP 3490，收 6 s 并落盘 .dlt
set -uo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
mkdir -p /tmp/dlt
"$PY" dlt_recv.py --duration 6 --out /tmp/dlt/board.dlt
exit $?
