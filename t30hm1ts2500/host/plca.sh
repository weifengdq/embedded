#!/usr/bin/env bash
# PLCA 快捷控制: ./plca.sh on|off|status|id N
set -euo pipefail
IFACE="${IFACE:-enx001ec0d1c337}"
cmd="${1:-status}"
case "$cmd" in
  on)
    ethtool --set-plca-cfg "$IFACE" enable on node-id 0 node-cnt 8 to-tmr 32
    ethtool --get-plca-cfg "$IFACE" ;;
  off)
    ethtool --set-plca-cfg "$IFACE" enable off
    ethtool --get-plca-cfg "$IFACE" ;;
  status)
    ethtool --get-plca-cfg "$IFACE"
    ethtool --get-plca-status "$IFACE" ;;
  *)
    echo "usage: $0 on|off|status" >&2; exit 1 ;;
esac
