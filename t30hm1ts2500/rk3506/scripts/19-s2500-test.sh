#!/bin/sh
# S2500 (T30HM1TS2500) 板端自测 —— 驱动加载 / 寄存器 / 计数器 / PHY 环回 /
# PLCA 开关 / SQI 诊断。busybox sh, 用法: 传到板子后 sh /tmp/s2500-test.sh
# 前提: /tmp/s2500.ko 已上传 (scripts/18-install-s2500.sh deploy <board-ip>)
PATH=/sbin:/bin:/usr/sbin:/usr/bin
NET=/sys/class/net/t1s0
KO=/tmp/s2500.ko
[ -f "$KO" ] || { echo "缺少 $KO"; exit 1; }

echo "===== 1. 加载驱动 ====="
# 板载内核里的 lan8650 驱动会先绑定 spi0.0, 必须先解绑 (它不校验芯片 ID)
if [ -d /sys/bus/spi/drivers/lan8650 ]; then
    echo spi0.0 > /sys/bus/spi/drivers/lan8650/unbind 2>/dev/null || true
fi
rmmod s2500 2>/dev/null
insmod "$KO" plca_en=1 plca_id=5 plca_ncnt=8 plca_totmr=32 spi_hz=25000000 || {
    echo "insmod 失败"; exit 1; }
sleep 0.5

# 新网卡一定是 eth0 之外的那个 (eth0 = 板上 GMAC)
IF=$(ls /sys/class/net | grep -E '^eth[0-9]+$' | grep -v '^eth0$' | head -1)
if [ -n "$IF" ] && [ "$IF" != "t1s0" ]; then
    ip link set "$IF" name t1s0 2>/dev/null || true
fi
ip addr add 192.168.10.15/24 dev t1s0 2>/dev/null || true
ip link set t1s0 up
sleep 1

echo "----- dmesg -----"
dmesg | grep -i s2500 | tail -8
echo "----- 链路 -----"
ip -br addr show t1s0
cat $NET/operstate; cat $NET/carrier 2>/dev/null; echo

echo "===== 2. 寄存器 dump ====="
cat $NET/s2500_regs

echo "===== 3. 计数器 (测试前) ====="
cat $NET/s2500_counters

echo "===== 4. 环回自检 (PCS loopback, 保持链路) ====="
# mode 1 = PCSCTL.LOOP: 链路不掉, 用来确认 TX 通路真的把帧送进了 MAC-PHY;
# mode 2 = PHY_CTRL.LOOPBACK: 会把 PHY 拉出 10BASE-T1S 链路的正常工作状态,
#          实测链路会掉(carrier=0), 不适合在线自检。
TX0=$(sed -n 's/.*ndev_tx_packets=\([0-9]*\).*/\1/p' $NET/s2500_counters)
echo 1 > $NET/s2500_loopback
sleep 0.3
ping -I t1s0 -c 5 -W 1 192.168.10.99 2>&1 | tail -3
sleep 0.3
TX1=$(sed -n 's/.*ndev_tx_packets=\([0-9]*\).*/\1/p' $NET/s2500_counters)
echo 0 > $NET/s2500_loopback
echo "TX 帧数: $TX0 -> $TX1 (应 >= +5: ARP + 5 个 ICMP, 实测 +9), carrier=$(cat $NET/carrier)"
echo "注意: S2500 的 PCS/PHY 环回只把数据绕回 PCS, MAC 收不到, 因此"
echo "      不能用环回验证 RX 通路; RX 请用两节点 ping/iperf 验证。"
echo "----- 计数器 (环回后) -----"
cat $NET/s2500_counters

echo "===== 5. PLCA 开关 ====="
cat $NET/plca_status
echo 0 > $NET/plca_enable; sleep 0.3
echo "关闭后: $(cat $NET/plca_status)"
echo 1 > $NET/plca_enable; echo 5 > $NET/plca_id
echo 8 > $NET/plca_ncnt; echo 32 > $NET/plca_totmr
sleep 0.5
echo "打开后 (follower id=5): $(cat $NET/plca_status)"
grep -E 'PLCA' $NET/s2500_regs

echo "===== 6. SQI / HDD 线缆诊断 ====="
cat $NET/s2500_sqi
echo 1 > $NET/s2500_hdd
sleep 1
cat $NET/s2500_hdd

echo "===== 7. 连通性 ====="
ping -c 3 -W 2 192.168.10.10 2>&1 | tail -3

echo "===== 8. 计数器 (测试后) ====="
cat $NET/s2500_counters
echo "===== done ====="
