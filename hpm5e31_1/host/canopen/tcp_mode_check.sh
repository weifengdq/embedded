#!/usr/bin/env bash
# 离线验证 cannelloni 2.1.2 的 TCP 传输模式（-C s 服务端 / -C c 客户端）
# 不依赖板卡：vcan0 <-> vcan1，两个 cannelloni 实例经 TCP 互传 CAN 帧。
# 用途：U-33-11 说明"TCP 模式可替代 UDP fire-and-forget"的可行性证据；
#       端到端 TCP 还要求板端 app_can_gw 增加 TCP 监听（当前仅 UDP，见第 33 章 U-33-11）。
set -uo pipefail
BR="host/can_bridge"
BIN="$BR/build/cannelloni"
sudo ip link add dev vcan1 type vcan 2>/dev/null || true
sudo ip link set up dev vcan0
sudo ip link set up dev vcan1
ip -br link show vcan0; ip -br link show vcan1
pkill -f "cannelloni" 2>/dev/null; sleep 0.3
"$BIN" -I vcan0 -C s -l 25000 -L 127.0.0.1 -t 1000 > /tmp/tcp_srv.log 2>&1 &
SRV=$!
sleep 0.6
"$BIN" -I vcan1 -C c -R 127.0.0.1 -r 25000 -t 1000 > /tmp/tcp_cli.log 2>&1 &
CLI=$!
sleep 0.6
( timeout 3 candump vcan1 > /tmp/tcp_cap.txt 2>&1 & )
sleep 0.3
for i in 1 2 3 4 5; do cansend vcan0 300#0${i}000000A55AC33C; sleep 0.05; done
sleep 3
kill $SRV $CLI 2>/dev/null
echo "=== TCP 模式：vcan0 -> cannelloni(TCP) -> vcan1 ==="
cat /tmp/tcp_cap.txt
N=$(grep -c "300" /tmp/tcp_cap.txt || true)
echo "收到 $N/5 帧"
[ "$N" = "5" ] && echo "TCP_MODE: PASS" || echo "TCP_MODE: FAIL"
