#!/usr/bin/env bash
# Bench B：vcan0 抓 3 s -> VSS 解码（板端需先 `gw on` + vss on）
set -uo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
REPO="$(cd ../.. && pwd)"
BR="$REPO/host/can_bridge"
bash "$BR/vcan_setup.sh" >/dev/null 2>&1 || true
pkill -f "cannelloni -I vcan0" 2>/dev/null || true
sleep 0.3
if [ -x "$BR/build/cannelloni" ]; then
  "$BR/build/cannelloni" -I vcan0 -R 192.168.2.100 -r 4000 -l 4000 -t 1000 >/tmp/vss_cannelloni.log 2>&1 &
  CPID=$!
  sleep 0.8
else
  echo "WARN: cannelloni 未构建"; CPID=0
fi
"$PY" vss_decode.py --channel vcan0 --duration 3 --json-out /tmp/vss.json
RC=$?
[ "$CPID" != "0" ] && kill "$CPID" 2>/dev/null
pkill -f "cannelloni -I vcan0" 2>/dev/null || true
exit $RC
