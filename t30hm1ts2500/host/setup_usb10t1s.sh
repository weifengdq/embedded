#!/usr/bin/env bash
# 配置 Ubuntu 侧 USB-10T1S (EVB-LAN8670-USB, K2L 10BASE-T1S) 为 PLCA 协调者
# 用法: sudo ./setup_usb10t1s.sh [接口名]
set -euo pipefail

USB_MAC="00:1e:c0:d1:c3:37"
IFACE="${1:-}"

if [[ -z "$IFACE" ]]; then
    for d in /sys/class/net/*; do
        [[ -r "$d/address" ]] || continue
        if [[ "$(cat "$d/address")" == "$USB_MAC" ]]; then
            IFACE="$(basename "$d")"
            break
        fi
    done
fi
if [[ -z "$IFACE" ]]; then
    echo "未找到 USB-10T1S (MAC $USB_MAC) 网卡" >&2
    echo "可用接口:" >&2
    ip -br link >&2
    exit 1
fi

echo "==> 接口: $IFACE ($(cat /sys/class/net/$IFACE/address))"
ip link set "$IFACE" up
ip addr replace 192.168.10.10/24 dev "$IFACE"

echo "==> 关闭 IPv6 (避免 RS/RA 噪声)"
sysctl -qw net.ipv6.conf."$IFACE".disable_ipv6=1 || true

echo "==> 配置 PLCA 协调者 (ID=0, NCNT=8, TOTMR=32)"
ethtool --set-plca-cfg "$IFACE" enable on node-id 0 node-cnt 8 to-tmr 32 || {
    echo "PLCA 配置失败：请确认 ethtool >= 6.9 且 PHY 驱动支持 PLCA" >&2
}

ethtool --get-plca-cfg "$IFACE"
ethtool --get-plca-status "$IFACE"
ip -br addr show "$IFACE"
echo "==> 完成。设备应为 192.168.10.10/24，PLCA 协调者。"
