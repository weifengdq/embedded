#!/usr/bin/env bash
# host_replug20.sh - 软件级"拔插"20 次（USB unbind/bind），验证 CDC 节点号与 CH343 共存
#   用法: sudo bash host_replug20.sh [cycles]
#   说明: 手上没有 uhubctl/可控 HUB 电源，物理拔插用 USB 设备 unbind/bind 模拟:
#         主机重新枚举设备 -> 重新分配 /dev/ttyACMx，等价于一次重插（地址/描述符都重走一遍）
set -u
CYCLES="$1"
[ -z "$CYCLES" ] && CYCLES=20
VID=34b7
ok=0
fail=0
nodes=""
ch343_fail=0
i=1
while [ "$i" -le "$CYCLES" ]; do
  dev=$(for p in /sys/bus/usb/devices/*/idVendor; do
          v=$(cat "$p" 2>/dev/null | tr -d '\n')
          [ "$v" = "$VID" ] && basename "$(dirname "$p")"
        done | head -1)
  if [ -z "$dev" ]; then echo "cycle $i: CDC device gone"; fail=$((fail+1)); i=$((i+1)); continue; fi
  echo "$dev" | sudo -n tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1
  sleep 0.4
  echo "$dev" | sudo -n tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
  sleep 0.8
  node=""
  for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    node=$(for p in /sys/bus/usb/devices/*/idVendor; do
             v=$(cat "$p" 2>/dev/null | tr -d '\n')
             [ "$v" = "$VID" ] || continue
             d=$(dirname "$p")
             ls "$d"/*/tty/ttyACM* 2>/dev/null | head -1
           done)
    [ -n "$node" ] && break
    sleep 0.2
  done
  if [ -z "$node" ]; then
    echo "cycle $i: $dev rebound but no ttyACM node"
    fail=$((fail+1))
  else
    n=/dev/$(basename "$node")
    nodes="$nodes $(basename "$n")"
    python3 -c "
import serial,sys,time
s=serial.Serial('$n',921600,timeout=2,write_timeout=5); s.dtr=True; time.sleep(0.05)
s.reset_input_buffer(); s.write(b'REPLUG'*128); s.flush()
got=b''
t0=time.time()
while len(got)<768 and time.time()-t0<3:
    got+=s.read(768-len(got))
s.close(); sys.exit(0 if got==b'REPLUG'*128 else 1)" 2>/dev/null
    if [ $? -eq 0 ]; then
      ok=$((ok+1)); echo "cycle $i: $n OK"
    else
      fail=$((fail+1)); echo "cycle $i: $n data FAIL"
    fi
  fi
  [ -e /dev/ttyACM0 ] || ch343_fail=$((ch343_fail+1))
  i=$((i+1))
done
echo "REPLUG_TOTAL cycles=$CYCLES ok=$ok fail=$fail ch343_missing=$ch343_fail nodes=$(echo $nodes | tr ' ' '\n' | sort -u | tr '\n' ',')"
[ "$fail" -eq 0 ] && echo "RESULT=PASS" || echo "RESULT=FAIL"
