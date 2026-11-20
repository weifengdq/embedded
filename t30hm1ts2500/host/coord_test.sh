#!/usr/bin/env bash
# coord_test.sh - "MCU/SOC 当 PLCA 协调者 (ID=0)" 一致性测试
#
# 思路: 把主机 (USB-10T1S) 从协调者降级为跟随者 (node-id 7),
#       让被测板成为唯一的协调者 (ID=0), 然后做双向 ping / iperf2 / 状态检查,
#       结束后恢复原状 (主机协调者 0, 被测板回到原 ID)。
#
# 用法: sudo host/coord_test.sh <stm32|hpm|s32k|tc387|rk3506> [板子IP] [原ID]
# 说明: 全网同一时刻只能有一个协调者; 本脚本用 /tmp/s2500_perf.lock 与吞吐测试互斥。
set -uo pipefail
cd "$(dirname "$0")/.."
BOARD=${1:?用法: sudo host/coord_test.sh <stm32|hpm|s32k|tc387|rk3506> [板子IP] [原ID]}
IFACE=${IFACE:-enx001ec0d1c337}
case "$BOARD" in
  stm32|h723)  IP=${2:-192.168.10.11}; ORIG=${3:-1};;
  hpm)    IP=${2:-192.168.10.12}; ORIG=${3:-2};;
  s32k)   IP=${2:-192.168.10.13}; ORIG=${3:-3};;
  tc387)  IP=${2:-192.168.10.14}; ORIG=${3:-4};;
  rk3506) IP=${2:-192.168.10.15}; ORIG=${3:-5};;
  *) echo "未知板子: $BOARD"; exit 1;;
esac
OUTDIR=${OUTDIR:-./coord_logs}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="$OUTDIR/coord_${BOARD}_${STAMP}.log"
mkdir -p "$OUTDIR"
exec > >(tee "$OUT") 2>&1

# 串行化锁: 若锁文件不可写 (例如以 root 运行时遇到别的用户创建的锁), 退化到私有锁并告警
if ! exec 9>/tmp/s2500_perf.lock 2>/dev/null; then
  echo "警告: /tmp/s2500_perf.lock 不可写, 使用私有锁 (不与他人互斥)" >&2
  exec 9>/tmp/s2500_perf.$$.lock
fi
flock 9

RK_HOST=${RK_HOST:?请先 export RK_HOST=<RK3506 eth0 的 IP>}   # 板子 eth0 走 DHCP, 用串口 ip -br addr 确认
RK_PASS=${RK_PASS:?请先 export RK_PASS=<RK3506 root 密码>}

board_sh() {   # 板端 shell 执行一条命令; $2 = 超时秒数 (默认 3)
  local to=${2:-3}
  if [ "$BOARD" = rk3506 ]; then
    sshpass -p "$RK_PASS" ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 root@$RK_HOST "$1"
  else
    ./tools/s25.py --auto "$BOARD" -t "$to" --idle "${3:-0.25}" -c "$1"
  fi
}

board_plca() { # $1 = PLCA node id
  if [ "$BOARD" = rk3506 ]; then
    # RK3506 的 S2500 不是 MDIO PHY, 驱动用 sysfs 控制面 (ethtool --set-plca-cfg 不支持)
    sshpass -p "$RK_PASS" ssh -o StrictHostKeyChecking=no root@$RK_HOST \
      "S=/sys/class/net/t1s0; echo 0 > \$S/plca_enable; echo $1 > \$S/plca_id; echo 8 > \$S/plca_ncnt; echo 32 > \$S/plca_totmr; echo 1 > \$S/plca_enable; cat \$S/plca_status"
  else
    ./tools/s25.py --auto "$BOARD" -t 3 -c "s2500 plca id $1 ncnt 8 totmr 32 en 1"
  fi
}

host_plca() {  # $1 = PLCA node id (0 = 协调者)
  # 必须先 disable 再 enable: 跟随者->协调者直接改 node-id 时驱动可能不生效
  sudo ethtool --set-plca-cfg "$IFACE" enable off 2>/dev/null
  sudo ethtool --set-plca-cfg "$IFACE" enable on node-id "$1" node-cnt 8 to-tmr 32
  sudo ethtool --get-plca-cfg "$IFACE" | sed -n 2p
}

restore() {
  echo
  echo "== 恢复原状 =="
  host_plca 0
  board_plca "$ORIG" >/dev/null 2>&1
  sleep 1
  echo "-- 主机 PLCA --"; sudo ethtool --get-plca-cfg "$IFACE"
  echo "-- 主机 -> $BOARD ($IP) --"; ping -c 3 -W 1 -i 0.2 "$IP" | tail -2
}

echo "=========================================================="
echo " PLCA 协调者 (ID=0) 测试: $BOARD  $STAMP"
echo " 板子 IP=$IP  原 ID=$ORIG  主机接口=$IFACE"
echo " 日志: $OUT"
echo "=========================================================="

echo
echo "== 0. 前置检查 =="
ip -br addr show "$IFACE"
sudo ethtool --get-plca-cfg "$IFACE"
ping -c 3 -W 1 -i 0.2 "$IP" | tail -2 || { echo "板子不在线, 退出"; exit 1; }

echo
echo "== 1. 主机降为跟随者 (node-id 7) =="
host_plca 7
sudo ethtool --get-plca-cfg "$IFACE"
sleep 1
echo "-- 主机当前 PLCA 状态 --"; sudo ethtool --get-plca-status "$IFACE"

echo
echo "== 2. 被测板成为协调者 (ID=0) =="
board_plca 0
sleep 2
echo "-- 板端状态 --"
if [ "$BOARD" = rk3506 ]; then
  board_sh "cat /sys/class/net/t1s0/plca_status; cat /sys/class/net/t1s0/plca_id; ip -s link show t1s0 | head -5"
else
  board_sh "s2500 status"
fi

echo
echo "== 3. 主机 -> 板子 ping (20 包) =="
ping -c 20 -i 0.02 -W 1 "$IP" | tail -3

echo
echo "== 4. 板子 -> 主机 ping (20 包) =="
if [ "$BOARD" = rk3506 ]; then
  board_sh "ping -c 20 -i 0.02 -W 1 192.168.10.10 | tail -3"
else
  board_sh "ping 192.168.10.10 20"
fi

echo
echo "== 5. TCP 吞吐 (iperf2, 板端为服务端) =="
if [ "$BOARD" = rk3506 ]; then
  board_sh "pkill iperf 2>/dev/null; iperf -s -D 2>/dev/null; sleep 0.5; echo server-started"
else
  board_sh "iperf -s"
fi
sleep 1
echo "-- 下行 (主机 -> 板) --"
iperf -c "$IP" -t 8 -i 0 -f m 2>&1 | tail -3
echo "-- 上行 (板 -> 主机) --"
if [ "$BOARD" = rk3506 ]; then
  iperf -c "$IP" -t 8 -i 0 -f m -R 2>&1 | tail -3
else
  # lwiperf 的 -R (反向) 不支持, 改用 板端作客户端 + 主机作服务端
  pkill -f "iperf -s" 2>/dev/null
  iperf -s -f m >/tmp/iperf_srv_$$.log 2>&1 &
  SRV=$!
  sleep 1
  board_sh "iperf -c 192.168.10.10 -t 8" 25 12
  kill $SRV 2>/dev/null
  tail -4 /tmp/iperf_srv_$$.log
  rm -f /tmp/iperf_srv_$$.log
fi

echo
echo "== 6. 计数器 (板端) =="
if [ "$BOARD" = rk3506 ]; then
  board_sh "cat /sys/class/net/t1s0/s2500_counters 2>/dev/null | head -20"
else
  board_sh "s2500 counters"
fi

restore
echo
echo "== 完成: $OUT =="
