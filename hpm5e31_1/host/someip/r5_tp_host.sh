#!/usr/bin/env bash
# r5_tp_host.sh -- 板端 someip_tp 已烧写并运行：主机侧 5 次 4096 B TP 收发 + tcpdump + ethtool 计数
set -uo pipefail
cd hpm5e31_1
source scripts/env.sh
TS=$(date +%Y%m%d-%H%M%S); OUT=logs/${TS}-host-tp-r5.log
exec > >(tee -a "$OUT") 2>&1
echo "[tp] $(date +%T) ping:"; ping -c 2 -W 1 192.168.2.100 | tail -2
echo '--- ethtool before'; ethtool -S enp6s0 2>/dev/null | grep -E 'rx_crc_errors|rx_errors|rx_dropped' || true
sudo -n timeout 45 tcpdump -i enp6s0 -nn -s0 'udp and port 30502' -w /tmp/r5_tp3.pcap >/dev/null 2>&1 &
TD=$!; sleep 1
for i in 1 2 3 4 5; do echo "--- run $i"; timeout 15 python3 host/someip/someip_client.py --board 192.168.2.100 tp --size 4096 2>&1 | grep -aE '板->主机|主机->板|PASS|FAIL|不连续|offset=' | tail -5; done
kill -$TD 2>/dev/null; wait $TD 2>/dev/null
echo '--- ethtool after'; ethtool -S enp6s0 2>/dev/null | grep -E 'rx_crc_errors|rx_errors|rx_dropped' || true
echo '--- tcpdump 板->主机 大帧（>200B）:'; sudo -n tcpdump -nn -r /tmp/r5_tp3.pcap 2>/dev/null | grep '192.168.2.100.30501 >' | grep -v 'length 20' | head -30
cp /tmp/r5_tp3.pcap "logs/${TS}-tp-r5.pcap" 2>/dev/null || true
echo "[tp] done $(date +%T) out=$OUT"
