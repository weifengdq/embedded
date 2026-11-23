#!/usr/bin/env bash
# 启动主机侧 cannelloni：vcan0 <--UDP--> 板端 CAN1 网关（192.168.2.100:4000）
#
# 参数（以 build/cannelloni -h 实测为准，v2.1.2）：
#   -I vcan0             本机 CAN 接口
#   -R 192.168.2.100     远端（板）IP
#   -r 4000              远端 UDP 端口（= 板端 app_can_gw 的监听端口）
#   -l 4000              本机 UDP 监听端口（板端会向这个源端口回包）
#   -t 1000              攒包超时 us —— **非常重要**：默认 100000us(100ms) 会把时延抬高两个数量级
#   -4                   IPv4
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BOARD_IP="${BOARD_IP:-192.168.2.100}"
IFACE="${IFACE:-vcan0}"
LOCAL_PORT="${LOCAL_PORT:-4000}"
REMOTE_PORT="${REMOTE_PORT:-4000}"
BUF_US="${BUF_US:-1000}"
BIN="${CANNELLONI_BIN:-$here/build/cannelloni}"

[ -x "$BIN" ] || { echo "ERROR: 找不到 $BIN，先跑 ./build_cannelloni.sh" >&2; exit 2; }
ip link show "$IFACE" >/dev/null 2>&1 || { echo "ERROR: $IFACE 不存在，先 sudo ./vcan_setup.sh" >&2; exit 2; }

echo "[tunnel] $IFACE <-> UDP $BOARD_IP:$REMOTE_PORT (local :$LOCAL_PORT, buffer ${BUF_US}us)"
exec "$BIN" -I "$IFACE" -R "$BOARD_IP" -r "$REMOTE_PORT" -l "$LOCAL_PORT" -4 -t "$BUF_US" "$@"
