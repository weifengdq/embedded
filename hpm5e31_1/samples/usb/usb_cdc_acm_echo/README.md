# usb_cdc_acm_echo —— USB0 device + CherryUSB CDC ACM 虚拟串口

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 用 CherryUSB device 栈实现 **CDC ACM** 虚拟串口（VCP），主机侧出现 `/dev/ttyACMx`；
- 收到主机 bulk OUT 数据后原样回写（bulk IN），含 512 字节整数倍时的 ZLP 处理；
- 命令 `usbinfo`（基址/VID/PID/DTR/端点 MPS/回显计数）、`usbsend n`（主动发 n 字节）。

## 硬件要求 / 接线

| 项 | 说明 |
|---|---|
| USB0 | 板上 USB0 通过 Pin67/68 接主机（本测试主机为 <user>@<host> 的 xHCI 端口） |
| 控制台 | 仍是 UART0（CH343 `/dev/ttyACM0`，921600 8N1） |
| USB CDC 节点 | 首次枚举为 `/dev/ttyACM2`（ACM0 被 CH343 占用） |
| 关键配置 | **必须调用 `usb_phy_using_internal_vbus()`**，否则不枚举（板上无 VBUS 检测引脚） |

## 编译与运行

```bash
bash scripts/build.sh usb/usb_cdc_acm_echo
bash scripts/hwrun.sh usb/usb_cdc_acm_echo --secs 12 --send "usbinfo"
# 主机侧同时观察:
#   journalctl -k --since "-2 min" | grep -iE "cdc_acm|34b7"
#   ls /sys/bus/usb/devices/*/idVendor  -> 出现 34b7
```

## 命令

| 命令 | 说明 |
|---|---|
| usbinfo | 打印 USB0 基址/IRQ、VID/PID、DTR、端点 MPS、回显计数 |
| usbsend [n] | 向主机发 n 字节 'A'..'Z' 循环（需要主机已打开端口） |

## 实测现象（串口输出）

设备端日志：`logs/20261030-113931-usb_cdc_acm_echo.log`
主机侧证据：`logs/20261030-114253-usb_cdc_acm_host.log`

```text
=== usb_cdc_acm_echo: USB0 CDC ACM device ===
cdc_acm_init done, waiting for host enumeration...
shell: usbinfo | usbsend [n]

letter:/$ usbinfo
USB0 device @0xf1420000, IRQn=60
  class     : CDC ACM (CherryUSB), VID=0x34b7 PID=0xffff
  DTR       : 0 (host opened the virtual COM port)
  ep mps    : in=512 out=512
  echo tick : 0 (每回显一包 +1)
```

主机 `journalctl -k` 实测（摘录）：

```text
usb 1-8: new high-speed USB device number 22 using xhci_hcd
usb 1-8: New USB device found, idVendor=34b7, idProduct=ffff
usb 1-8: Product: HPMicro CDC DEMO
usb 1-8: Manufacturer: HPMicro
usb 1-8: SerialNumber: 2024051702
cdc_acm 1-8:1.0: ttyACM2: USB ACM device
```

结论：**枚举 PASS**（high-speed、描述符字符串正确、内核绑定 cdc_acm 并创建 ttyACM2），
且 **回显 PASS（3/3 一致）** —— 主机侧日志 `logs/20261030-121242-usb_cdc_acm_echo-host.log`：

```text
attempt 1: tx=b'hello-hpm5e31\n' rx=b'hello-hpm5e31\n'
attempt 2: tx=b'hello-hpm5e31\n' rx=b'hello-hpm5e31\n'
attempt 3: tx=b'hello-hpm5e31\n' rx=b'hello-hpm5e31\n'
RESULT: PASS (3 次回显一致)
```

板端日志 `logs/20261030-121242-usb_cdc_acm_echo.log` 同时记录 3 次 `actual out len:14` / `actual in len:14`。

主机侧复现（用 pyserial，ttyACM 编号每次可能不同，先按 34b7 动态查找）：

```text
t=$(for x in /sys/class/tty/ttyACM*; do [ "$(cat $x/device/../idVendor 2>/dev/null)" = 34b7 ] && basename $x; done)
python3 -c "import serial,time; s=serial.Serial('/dev/'+'$t',115200,timeout=0.5); time.sleep(0.5); s.reset_input_buffer(); s.write(b'hello-hpm5e31\\n'); s.flush(); print('rx =', s.read(64)); s.close()"
```

注意：不要用 /dev/ttyACM0（那是 CH343 控制台），编号可能变化。

## 关键点 / 易错点

1. **不枚举的根因**：本板没有 VBUS 检测引脚，`board_init_usb()` 之后必须
   `usb_phy_using_internal_vbus()`（加之前主机完全看不到设备，加之后立刻出现 34b7）。
2. 端点缓冲必须放 non-cacheable 区（`USB_NOCACHE_RAM_SECTION`）并按 4 B 对齐。
3. 发送长度为 512 整数倍时要补 ZLP，否则主机会等（本示例已处理）。
4. ACM0 是 CH343 调试串口，别拿它当 CDC 端口；用 `idVendor==34b7` 动态找节点。

## 待确认项

| 项 | 状态 | 验证方法 |
|---|---|---|
| 回显一致性 | **已确认（PASS 3/3）** | logs/20261030-121242-usb_cdc_acm_echo-host.log |
| bulk 吞吐（MB/s） | 待确认（需实测） | 主机连续写 1 MB 计时；设备端统计 echo_tick |
