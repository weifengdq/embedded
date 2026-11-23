#!/usr/bin/env bash
# 一键跑全部主机侧 J1939 测试，输出到仓库 logs/
set -euo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
[ -x "$PY" ] || PY=python3
IFACE="${1:-vcan0}"
TS="$(date +%Y%m%d-%H%M%S)"
ROOT="$(cd ../.. && pwd)"
LOG="$ROOT/logs"
mkdir -p "$LOG"
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  sudo modprobe vcan
  sudo ip link add dev "$IFACE" type vcan
  sudo ip link set up "$IFACE"
fi
echo "=== tp selftest ==="
"$PY" j1939_tp_test.py --mode selftest -i "$IFACE" | tee "$LOG/$TS-host-j1939-tp.log"
echo "=== dm1 decode (vcan 注入) ==="
("$PY" dm1_decode.py -i "$IFACE" --duration 2 > "$LOG/$TS-host-j1939-dm1.log" 2>&1 &)
sleep 0.4
cansend "$IFACE" 18FECA00#0000 || true
sleep 0.2
cansend "$IFACE" 18FECA00#100024010C00 || true
sleep 2
cat "$LOG/$TS-host-j1939-dm1.log"
echo "=== kernel can-j1939 interop ==="
sudo modprobe can-j1939 || true
"$PY" hoststack_interop.py -i "$IFACE" | tee "$LOG/$TS-host-j1939-kernel.log"
echo "logs -> $LOG/$TS-host-j1939-*.log"
