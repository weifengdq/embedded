# usb_hid_keyboard —— USB0 device（HID 键盘）+ 主机侧 input/hidraw 验证

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 用 CherryUSB 的 HID 类实现一个标准 **USB HID 键盘**（8 字节报告：修饰键+保留+6 键码）；
- 设备每秒自动发送一次 'a' 键的**按下+释放**报告，主机内核绑定为键盘输入设备；
- 命令 `hidinfo`（VID/PID/是否已配置/发送计数）、`hidkey [n]`（手动发 n 次 'a'）；
- 与 `samples/usb/usb_cdc_acm_echo/` 一起，覆盖 USB device 的两种主流类（CDC / HID）。

## 硬件要求 / 接线

| 项 | 说明 |
|---|---|
| USB0 | 板上 USB0 接主机（与 CDC 例程同一根线） |
| 关键配置 | 必须调用 `usb_phy_using_internal_vbus()`，否则不枚举（板上无 VBUS 检测引脚） |
| 主机侧节点 | HID **不会**出现 ttyACM；而是 input 设备 + `/dev/hidrawN` |
| 控制台 | UART0 / `/dev/ttyACM0`（921600 8N1） |

## 编译与运行

```bash
bash scripts/build.sh usb/usb_hid_keyboard
bash scripts/hwrun.sh usb/usb_hid_keyboard --secs 20 --no-build
# 主机侧同时观察:
#   journalctl -k --since "-3 min" | grep -iE "hid-generic|input:"
#   for d in /sys/bus/usb/devices/*/; do cat $d/idVendor $d/product 2>/dev/null; done
```

## 命令

| 命令 | 说明 |
|---|---|
| hidinfo | 打印基址/IRQ、VID/PID、是否已配置、HID 发送计数 |
| hidkey [n] | 发送 'a' 的按下+释放 n 次（1~100） |

## 实测现象

### 主机侧（journalctl + sysfs，证据日志 `logs/20261030-123550-usb_hid_keyboard-host.log`）

```text
idVendor       34b7
idProduct      ffff
manufacturer   HPMicro
product        HPMicro HID DEMO
speed          480                     # high-speed

BInterfaceClass = 03                   # 03 = HID

内核日志:
usb 1-8: New USB device found, idVendor=34b7, idProduct=ffff, bcdDevice= 0.02
usb 1-8: Product: HPMicro HID DEMO
usb 1-8: Manufacturer: HPMicro
input: HPMicro HPMicro HID DEMO as /devices/.../0003:34B7:FFFF.0002/input/input15
hid-generic 0003:34B7:FFFF.0002: input,hidraw1: USB HID v1.11 Keyboard [HPMicro HPMicro HID DEMO]

input 设备: /sys/class/input/input15/name = HPMicro HPMicro HID DEMO
hidraw    : /dev/hidraw1 (crw------- root:root)
```

三条关键证据：① 接口类 = 0x03（HID）；② 内核 `hid-generic` 把它绑定为 **Keyboard**；
③ 主机创建了 input15 与 `/dev/hidraw1`。**枚举/类绑定 PASS**。

### 设备侧

设备端启动日志 `logs/20261030-123550-usb_hid_keyboard.log`：

```text
=== usb_hid_keyboard: USB0 HID keyboard device ===
hid_keyboard_init done, waiting for host enumeration...
shell: hidinfo | hidkey [n]
```

## 关键点 / 易错点

1. **HID IN 传输必须有主机读**：主机不读中断端点时 `usbd_ep_start_write()` 会一直处于
   BUSY（本示例最初在 `hid_keyboard_send_key()` 里裸 `while` 忙等，导致主循环卡死、
   串口不再打印）。已加 ~200 ms 超时返回 -2，主循环可继续。
2. **枚举不稳定**：和 CDC 例程一样，J-Link 重新烧写后偶发"主机端口没有重新枚举"，
   需要再烧一次或让主机重新扫描；排查方法见 `logs/20261030-123550-usb_hid_keyboard-host.log`。
3. **hidraw 权限**：`/dev/hidraw1` 是 root:root 0600，普通用户不能直接读；
   要抓按键事件用 `evtest /dev/input/eventX`（需要 input 组权限）或 root。
4. **HID 报告描述符长度必须与接口描述符一致**（本例 63 字节），否则主机会枚举失败。
5. 板级必须 `usb_phy_using_internal_vbus()`（同 CDC 例程），不加则完全看不到设备。

## 待确认项

| 项 | 状态 | 验证方法 |
|---|---|---|
| 主机侧按键事件（EV_KEY）实际到达 | 待确认（无 input 组权限） | `evtest /dev/input/eventX` 或 `cat /dev/hidraw1` 观察 'a' 报告 |
| 设备端 hid_tx 计数 | 待确认（本次串口窗口未捕获到） | 重新烧写后 `hidinfo` 查看 configured=1 且 hid_tx 增长 |
| 鼠标/自定义 HID | 未做 | 复用 SDK `samples/cherryusb/device/hid/mouse` |
