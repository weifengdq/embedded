#!/usr/bin/env bash
# t1s_host.sh -- configure the host side USB-10BASE-T1S adapter (enx001ec0d1c337)
# usage: sudo bash t1s_host.sh <csma|plca0|plca1> [node_cnt] [to_tmr] [burst_cnt] [burst_tmr]
set -u
IF=${T1S_IF:-enx001ec0d1c337}
MODE=${1:-csma}
NCNT=${2:-8}; TO=${3:-32}; BC=${4:-0}; BT=${5:-128}

ip link set "$IF" up
ip addr add 192.168.1.1/24 dev "$IF" 2>/dev/null
case "$MODE" in
  csma)
    ethtool --set-plca-cfg "$IF" enable off 2>/dev/null && echo "plca off (CSMA/CD)" ;;
  plca0)
    ethtool --set-plca-cfg "$IF" enable on node-id 0 node-cnt "$NCNT" to-tmr "$TO" burst-cnt "$BC" burst-tmr "$BT" && echo "plca on, node-id 0 (coordinator)" ;;
  plca1)
    ethtool --set-plca-cfg "$IF" enable on node-id 1 node-cnt "$NCNT" to-tmr "$TO" burst-cnt "$BC" burst-tmr "$BT" && echo "plca on, node-id 1 (follower)" ;;
  *)
    echo "usage: $0 <csma|plca0|plca1> [node_cnt] [to_tmr] [burst_cnt] [burst_tmr]"; exit 2 ;;
esac
ethtool --get-plca-cfg "$IF" 2>/dev/null || true
ethtool --get-plca-status "$IF" 2>/dev/null || true
ip -br addr show "$IF"
