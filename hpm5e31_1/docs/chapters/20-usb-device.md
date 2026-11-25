# 第 20 章 USB 设备（CDC/HID）

## 20.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/usb/usb_cdc_acm_echo/`、`samples/usb/usb_hid_keyboard/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（主机侧枚举 + CDC 回显 3/3 一致，均有日志证据） |

## 20.2 本章导览

- 学到什么：HPM5E31 的 USB0 device 控制器结构、USB 枚举在主机侧长什么样、CherryUSB 的移植要点、
  以及"最小系统板没有 VBUS 检测引脚"这个会让设备**根本不枚举**的易错点。
- 前置章节：第 02 章（烧写）、第 19 章（DMA，USB 端点也用总线搬运）。
- 配套例程：`samples/usb/usb_cdc_acm_echo/`。
- 实测结论：设备枚举为 `idVendor=34b7 / Product "HPMicro CDC DEMO"`（high-speed），
  主机内核绑定 `cdc_acm 1-8:1.0: ttyACM2`；**回显 3/3 一致（PASS）**，
  证据：`logs/20261030-121242-usb_cdc_acm_echo-host.log`（主机侧）+ 同名板端日志（设备侧 actual out/in len:14）。

## 20.3 概述与典型应用

HPM5E31 片内集成 **1 个 USB 2.0 OTG 控制器（USB0）**，可作为设备（device）使用，
支持 8 个双向端点（8 IN + 8 OUT，RM §54 p.942），内置 PHY；板上 USB0 的 D+/D- 引到连接器接主机。
设备模式常用速度：全速（12 Mbps）/高速（480 Mbps，需主机与 PHY 支持）。

典型应用：

| 场景 | 类 | 说明 |
|---|---|---|
| 调试串口 / 日志 | CDC ACM | 本仓库例程，主机侧出现 /dev/ttyACMx |
| 键盘/鼠标/自定义按键 | HID | 免驱，报文小 |
| 固件升级 | DFU | 配合 BootROM/自定义 bootloader |
| 数据采集上传 | CDC/自定义 bulk | 需要吞吐测试（见第 11 节） |

表 20-1 USB0 device 关键参数（来源：RM §54 p.942、板级 `board.c`）：

| 参数 | 值 | 出处 |
|---|---|---|
| 控制器 | USB0（device/OTG） | RM §54 |
| 端点 | 8 IN + 8 OUT，双向 | RM §54 p.942 |
| 端点 0 最大包 | 64 B（控制） | USB 规范 |
| Bulk 端点最大包 | 512 B（HS）/ 64 B（FS） | 驱动 `USB_BULK_EP_MPS_*` |
| 板级基址宏 | `CONFIG_HPM_USBD_BASE = 0xF142_0000` | `usb_config.h` + 实测 `usbinfo` |
| 中断号 | `CONFIG_HPM_USBD_IRQn = 60` | 实测 |
| VBUS 检测 | **无专用引脚**，需内部 VBUS 指示 | `board.c` 注释 + 实测 |

## 20.4 工作原理

### 20.4.1 枚举流程

![图 20-1 USB CDC ACM 枚举流程与主机侧观测点](figures/20/fig-20-1-usb-enum-flow.png)

如图 20-1 所示，设备从上电到能收发数据要经过：总线复位 → SET_ADDRESS → 读设备/配置描述符 →
SET_CONFIGURATION → CDC 类请求（SET_LINE_CODING）→ SET_CONTROL_LINE_STATE（DTR=1）→ bulk 收发。
本仓库在主机 `journalctl -k` 里实测到的关键行（原文见日志）：

```text
usb 1-8: new high-speed USB device number 22 using xhci_hcd
usb 1-8: New USB device found, idVendor=34b7, idProduct=ffff, bcdDevice= 1.00
usb 1-8: Product: HPMicro CDC DEMO
usb 1-8: Manufacturer: HPMicro
usb 1-8: SerialNumber: 2024051702
cdc_acm 1-8:1.0: ttyACM2: USB ACM device
```

这段日志同时证明了三件事：设备被识别为 **high-speed**、描述符字符串正确（说明描述符表没问题）、
主机内核加载了 `cdc_acm` 驱动并创建了 `ttyACM2`。

随后主机侧用 pyserial 打开 `/dev/ttyACM2`、写入 `hello-hpm5e31 + 换行`，3 次全部原样回读
（主机日志 `logs/20261030-121242-usb_cdc_acm_echo-host.log`）：

```text
attempt 1: tx=b'hello-hpm5e31\n' rx=b'hello-hpm5e31\n'
attempt 2: tx=b'hello-hpm5e31\n' rx=b'hello-hpm5e31\n'
attempt 3: tx=b'hello-hpm5e31\n' rx=b'hello-hpm5e31\n'
RESULT: PASS (3 次回显一致)
```

板端串口同样记录到 3 次进出各 14 字节（`logs/20261030-121242-usb_cdc_acm_echo.log`）：
actual out len:14 / actual in len:14 各 3 次。两端日志互相印证：**CDC ACM 数据通路工作正常**。

### 20.4.2 控制器与端点队列

![图 20-2 CDC ACM 端点与缓冲布局](figures/20/fig-20-2-cdc-endpoints.png)

USB 控制器用 **端点队列（dqh）+ 传输描述符（dtd）** 描述传输（RM §54 p.943-949）：

1. 软件在 2 KB 对齐的内存里准备 `dqhlist`，地址写入 `ENDPTLISTADDR`；
2. 每个端点两个 dqh（IN/OUT），每个 64 B；
3. 把 dtd 挂到 dqh 的 Next 指针，再置 `ENDPTPRIME` 启动该端点传输；
4. 完成/错误通过 `USBSTS` 与端点中断上报。

CherryUSB 把这些细节封装成 `usbd_ep_start_read()` / `usbd_ep_start_write()`。

### 20.4.3 CherryUSB 移植要点

- 设备栈源码在 `hpm_sdk/middleware/cherryusb`，板级配置在
  `samples/cherryusb/config/usb_config.h`（通过 CMake 的 `set(CONFIG_CHERRYUSB 1)` 等开关启用）；
- 端点缓冲必须放 **non-cacheable** 区（`USB_NOCACHE_RAM_SECTION`），否则 DMA 与 CPU 读到不同数据；
- 缓冲区要按 `CONFIG_USB_ALIGN_SIZE` 对齐；
- 高速 bulk IN 传输长度为 512 的整数倍时，需要补 **ZLP**（零长度包），否则主机会一直等（例程
  `usbd_cdc_acm_bulk_in()` 里已处理）。

## 20.5 关键寄存器与驱动 API 对照

芯片级参数见表 20-1，寄存器与 CherryUSB API 的对应关系见表 20-2；本节只挑与吞吐/枚举相关的点。

表 20-2 USB 寄存器/API 对照（寄存器见 RM §54，API 见 CherryUSB 与 `hpm_usb_drv.h`）：

| 功能 | 寄存器/结构 | 关键位域 | API | 例程位置 |
|---|---|---|---|---|
| 控制器运行/停止 | `USBCMD` | `RS` | `usbd_initialize()` | `main.c` |
| 状态/中断 | `USBSTS` | `UI` / `UEI` | 中断回调 `usbd_event_handler()` | `cdc_acm.c` |
| 端点列表基址 | `ENDPTLISTADDR` | 2 KB 对齐地址 | 栈内部管理 | — |
| 端点使能/复位 | `ENDPTCTRL` | `RXE` / `TXE` | `usbd_ep_open()` | `cdc_acm.c` |
| 端点 prime | `ENDPTPRIME` | 每端点一位 | `usbd_ep_start_read()/write()` | `cdc_acm.c` |
| 端点 NAK | `ENDPTNAK` | 每端点一位 | 栈内部 | — |
| PHY 控制 | `PORTSC1` | `PHCD` / `PP` | `usb_phy_using_internal_vbus()` | `main.c` |
| 描述符注册 | — | — | `usbd_desc_register()` | `cdc_acm.c` |
| 类初始化 | — | — | `usbd_cdc_acm_init_intf()` | `cdc_acm.c` |
| 板级初始化 | — | — | `board_init_usb()` | `main.c` |

## 20.6 例程逐条讲解

### 20.6.1 例程：`samples/usb/usb_cdc_acm_echo/`

| 项 | 内容 |
|---|---|
| 路径 | `samples/usb/usb_cdc_acm_echo/` |
| 覆盖特性 | CherryUSB device 栈、CDC ACM 类、bulk 回显、ZLP、DTR 检测、内部 VBUS |
| 关键配置 | VID=0x34b7 PID=0xffff，EP1 IN/OUT 0x81/0x01，EP2 INT 0x83，512 B MPS |
| 实测现象 | 主机枚举成功（journalctl 见 2.1），ttyACM2 创建，**回显 3/3 一致** |
| 实测日志 | `logs/20261030-121242-usb_cdc_acm_echo.log`（设备端）、`logs/20261030-121242-usb_cdc_acm_echo-host.log`（主机侧证据，含回显 PASS） |

关键代码（来自 `samples/usb/usb_cdc_acm_echo/src/main.c`）：

```c
int main(void)
{
    bsp_init();                                   /* 控制台 = UART0 (/dev/ttyACM0) */
    board_init_usb((USB_Type *) CONFIG_HPM_USBD_BASE);
    /* 关键: 最小系统板没有 VBUS 检测引脚, 不打开内部 VBUS 就不会上拉 D+, 主机看不到设备 */
    usb_phy_using_internal_vbus((USB_Type *) CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);
    cdc_acm_init(0, CONFIG_HPM_USBD_BASE);        /* 注册描述符/接口/端点并 initialize */
    while (1) {
        bsp_shell_poll();
    }
}
```

命令：

```text
usbinfo               # 基址/VID/PID/DTR/端点 MPS/回显计数
usbsend [n]           # 从设备向主机发 n 字节测试数据 (需要主机已打开端口, DTR=1)
```

### 20.6.2 例程 B：`samples/usb/usb_hid_keyboard/`（HID 键盘）

| 项 | 内容 |
|---|---|
| 路径 | `samples/usb/usb_hid_keyboard/` |
| 覆盖特性 | HID 类设备、8 字节键盘报告、中断 IN 端点、主机 input/hidraw 绑定 |
| 关键配置 | 63 字节报告描述符、EP 0x81 中断 IN（间隔 10 ms）、每秒自动发一次 'a' 按下+释放 |
| 实测现象 | 主机枚举 PASS：接口类 0x03、内核 hid-generic Keyboard 绑定、input15 + /dev/hidraw1 |
| 实测日志 | `logs/20261030-123550-usb_hid_keyboard.log`（设备端）、`logs/20261030-123550-usb_hid_keyboard-host.log`（主机侧证据） |

表 20-6 HID 主机侧实测证据（journalctl + sysfs）：

| 项 | 实测值 |
|---|---|
| idVendor / idProduct | 0x34b7 / 0xffff |
| Product | HPMicro HID DEMO |
| 速度 | high-speed（480 Mbps） |
| bInterfaceClass | 0x03（HID） |
| 内核绑定 | hid-generic 0003:34B7:FFFF.0002: input,hidraw1: USB HID v1.11 Keyboard |
| input 设备 | input15，name=HPMicro HPMicro HID DEMO |
| hidraw | /dev/hidraw1（root:root 0600） |

易错点：HID 中断 IN 传输需要主机读取报告，否则 usbd_ep_start_write() 会一直 BUSY；
本示例最初在发送函数里裸 while 忙等导致主循环卡死（串口停打），已加约 200 ms 超时返回 -2。

## 20.7 上板实测数据

表 20-3 主机侧枚举实测（Ubuntu 24.04 / xhci_hcd / 内核日志时间 2026-10-30 11:41:52）：

| 项 | 实测值 |
|---|---|
| 设备节点 | `usb 1-8`（Bus 1, Port 8） |
| 枚举速度 | high-speed（480 Mbps） |
| idVendor / idProduct | 0x34b7 / 0xffff |
| Manufacturer | HPMicro |
| Product | HPMicro CDC DEMO |
| SerialNumber | 2024051702 |
| 内核驱动绑定 | `cdc_acm 1-8:1.0: ttyACM2: USB ACM device` |
| 宿主串口节点 | 首次枚举创建 `/dev/ttyACM2`（ACM0 被 CH343 占用，ACM1 是别的设备） |
| 拔插行为 | 同一端口重复烧写会先 disconnect 再重新枚举（dmesg 可见） |
| 干净流程复测 | 2026-10-30 12:13:03 重新烧写后一次枚举成功（device number 25），无需断电重插 |

设备端 `usbinfo` 输出（`logs/20261030-113931-usb_cdc_acm_echo.log`）：

```text
USB0 device @0xf1420000, IRQn=60
  class     : CDC ACM (CherryUSB), VID=0x34b7 PID=0xffff
  DTR       : 0 (host opened the virtual COM port)
  ep mps    : in=512 out=512
  echo tick : 0
```

上表中 DTR: 0 表示主机还没有打开这个虚拟串口；echo tick: 0 表示设备尚未回显过任何数据包，
两者都是枚举成功但主机未开始通信的正常状态，主机打开设备后 DTR 会变 1、回显计数开始增长。

表 20-4 回显验证状态：

| 步骤 | 结果 | 证据 |
|---|---|---|
| 枚举 | PASS | journalctl（表 20-3） |
| ttyACM 创建 | PASS | 内核日志 + /dev/ttyACM2 |
| 主机打开端口（DTR=1） | **PASS** | pyserial 打开后设备端 DTR 置位，可正常收发 |
| 回显一致性 | **PASS（3/3）** | 主机 rx 与 tx 完全一致；板端 actual out/in len:14 各 3 次 |
| 是否需要断电重插 | **不需要** | 本次烧写后一次枚举成功；此前一次失败为烧写后主机端口未重新枚举的偶发现象 |

### 20.7.1 CDC bulk 吞吐实测（2026-11-02）

主机 pyserial 打开 CDC 节点（VID 0x34b7 自动识别为 `/dev/ttyACM2`），连续写大块并计时；
设备端同时抓串口日志看 `echo_tick`。实测结果见表 20-7，共存稳定性见表 20-8。

表 20-7 CDC bulk 写入吞吐（主机侧计时，单次写满即停表）

| 块大小 | 写入量 | 写入耗时 | 吞吐 |
|---|---|---|---|
| 512 B | 1 MB | 1.537 s | 0.68 MB/s |
| 2048 B | 1 MB | 1.219 s | **0.86 MB/s** |
| 16384 B | 1 MB | >8 s（超时中断） | 停顿，未完成 |

结论：**主机 → 设备方向 0.68~0.86 MB/s（512 B / 2 KB 块，严格回环口径）**，与块大小基本无关（16 KB 块停顿、未完成）；R5 曾写 1.78 MB/s，但该口径日志未归档、不可复现，本轮以归档日志 `logs/20261102-164926-usb_cdc-throughput-host.log` 为准。瓶颈在设备固件处理速度（本例程每包做
DTR 状态打印 + 512 B 切片回显），不是 USB HS 链路（理论上限 480 Mbit/s = 60 MB/s）。要测链路极限，
必须去掉逐包打印并做双缓冲/批量提交。

**"先写满再读"会丢数据**：R5 的同一测试里 256 KB/1 MB 写完后回读只收到远小于写入量的数据（具体字节数日志未归档，本轮不再引用；复跑命令见本节末），
12.2 s 内不再有数据（主机读超时）。设备端日志 `logs/20261102-123845-usb_cdc_r5b-device.log` 显示
`dtr=1`、`echo_tick` 在按 512/1024/3072 递增——设备在回显，是"写满再读"这种用法让 IN 端点先被写满、
固件随后不再消费 OUT 队列，形成死锁。**这个例程的 echo 只适合交互式小包**；做吞吐测试要么读写并发、
要么只统计单向。

### 20.7.2 与 CH343 同时工作的稳定性（2026-11-02）

表 20-8 与 CH343 共存 10 次开关端口实测

同一台主机上 CH343（`/dev/ttyACM0`，控制台）与 CDC（`/dev/ttyACM2`）并存时，做 10 次
"打开-写 1 KB-读回-关闭"循环：

| 项 | 结果 |
|---|---|
| 循环次数 | 10 |
| 成功 | **10/10**（每次 1 KB 'PING' 往返一致） |
| 节点号 | 始终 `ttyACM2`，没有漂移 |
| CH343 控制台 | 全程存在（`/dev/ttyACM0` 未消失） |

物理拔插模拟（`unbind`/`bind` 而不是拔线）脚本已写好（`samples/usb/usb_cdc_acm_echo/host_replug20.sh`），
本轮时间盒内只跑了软件级 10 次开关端口，**物理插拔 20 次未做**（见第 11 节）。

## 20.8 性能与调优

- USB HS bulk 端点的理论单包 512 B，一帧最多 13 包（微帧），理论 ~60 MB/s 量级；
  实测主机→设备 **0.68~0.86 MB/s**（512 B/2 KB 块，严格逐块回环口径；16 KB 块会停顿），瓶颈在固件逐包处理与回显背压而不是 USB 链路（见 5.1）；
- 提高吞吐的常见手段：增大端点缓冲与双缓冲（本仓库例程用 `read_buffer[2][512]` 双缓冲）、
  批量提交（不要一包一次中断）、把缓冲放 non-cacheable 且 4 B 对齐；
- CDC ACM 的线速（波特率）只是"类请求参数"，bulk 传输不受它限制——所以用 CDC 做高速数据管道时，
  主机端要把波特率当作无意义参数。

## 20.9 易错点与注意事项

1. **现象**：设备端初始化成功、打印 "waiting for host enumeration"，主机 `lsusb` 看不到任何 HPMicro 设备。
   **原因**：本板没有 VBUS 检测引脚，PHY 认为"没有 VBUS"，因此不拉 D+，主机端根本不知道有设备接入
   （SDK `board.c` 的注释也提示 LQFP100 封装应调用 `usb_phy_using_internal_vbus()`）。
   **解决**：`board_init_usb()` 之后调用 `usb_phy_using_internal_vbus()`（本仓库例程已加，加上后立刻枚举成功）。
2. **现象**：把 CDC 端口当成 `/dev/ttyACM0` 打开失败或读到乱码。
   **原因**：ACM0 是板载 CH343 调试串口（921600 8N1），USB CDC 是另一个节点。
   **解决**：用 `/sys/bus/usb/devices/*/idVendor` 找到 34b7 对应的 tty（脚本见 `logs/20261030-114253-usb_cdc_acm_host.log` 的采集方式）。
3. **现象**：传输偶发数据错乱/丢失。
   **原因**：端点缓冲放在 cacheable 区，DMA 与 CPU 看到不同副本。
   **解决**：`USB_NOCACHE_RAM_SECTION` + 对齐；必要时手动 flush/invalidate。
4. **现象**：主机读不到最后一段数据，一直等。
   **原因**：发送长度恰好是 512 的整数倍且没有 ZLP。
   **解决**：在 IN 完成回调里补 `usbd_ep_start_write(..., NULL, 0)`。
5. **现象**：每次 J-Link 重新烧写后主机端口号变化（ACM2 → ACM3）。
   **原因**：重新枚举是新设备，内核按顺序分配。
   **解决**：脚本里动态查找 34b7 的 tty，不要硬编码节点号。

## 20.10 优劣评价与同类对比

表 20-5 USB device 能力对比（HPM 数据来自 RM/实测，其余为公开资料）：

| 项 | HPM5E31 USB0（实测/RM） | STM32H7 USB OTG | TC397 USB |
|---|---|---|---|
| 速度 | HS/FS device | HS/FS OTG | HS/FS |
| 端点数 | 8 IN + 8 OUT | 6~9 双向 | 多端点 |
| 片内 PHY | 有 | 有（HS） | 有 |
| VBUS 检测 | 板上无，需内部 VBUS | 有 VBUS 引脚 | 视封装 |
| 软件栈 | CherryUSB（SDK 集成） | STM32 USB Device Library | 厂商栈 |
| 实测枚举 | PASS | — | — |

优势：CherryUSB 集成度高（CDC/HID/MSC/DFU 例程齐全）、片内 PHY 省外围；
劣势：板级 VBUS 检测缺失导致"默认不枚举"，文档不显眼（本仓库实测遇到）。

## 20.11 参考来源

**上游链接（2026-11-02 核对）**

- CherryUSB（device 协议栈，本仓用的就是它）：<https://github.com/cherry-embedded/CherryUSB>
- USB 2.0 规范（bulk/端点/remote wakeup 定义）：<https://www.usb.org/document-library/usb-20-specification>
- HPM SDK 源码（USB0 驱动）：<https://github.com/hpmicro/hpm_sdk>


1. RM §54「通用串行总线 USB」（p.942 端点数量；p.943-949 队列/描述符）。
2. `hpm_sdk/samples/cherryusb/device/cdc_acm/cdc_acm_vcom/src/cdc_acm.c`（CDC ACM 参考实现）。
3. `samples/cherryusb/config/usb_config.h`（VID/PID、缓冲对齐、非 cache 段定义）。
4. `boards/hpm5e31_1/board.c`（`board_init_usb()` 与内部 VBUS 注释）。
5. `hpm_sdk/drivers/inc/hpm_usb_drv.h:592`（`usb_phy_using_internal_vbus()`）。
6. 例程：`samples/usb/usb_cdc_acm_echo/`。
7. 实测日志：`logs/20261030-121242-usb_cdc_acm_echo.log`（设备端）、`logs/20261030-121242-usb_cdc_acm_echo-host.log`（主机侧，回显 PASS）。
8. HID 例程日志：`logs/20261030-123550-usb_hid_keyboard.log`、`logs/20261030-123550-usb_hid_keyboard-host.log`。

## 20.12 扩展阅读

- [CherryUSB 官方仓库](https://github.com/cherry-embedded/CherryUSB)
- [HPM SDK CherryUSB 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/cherryusb)
- 本仓库第 02 章（烧写）、第 22 章（低功耗模式下 USB 唤醒）。

## 20.13 待确认项与风险

> 已逐条复核（2026-11-02）：本节每条都有实测数据、定位结论，或"已知限制 + 原因 + 验证方法"；不再保留裸"待确认/未做"条目。

| 项 | 状态 | 验证方法 |
|---|---|---|
| CDC 回显一致性（设备→主机→设备） | **已实测（PASS 3/3）** | 主机 pyserial 写 hello-hpm5e31 原样回读，见 logs/20261030-121242-usb_cdc_acm_echo-host.log |
| CDC bulk 实测吞吐（MB/s） | **已实测（2026-11-02 复测，日志已归档）**：主机→设备 0.68 MB/s（512 B 块）/ 0.86 MB/s（2 KB 块），各写 1 MB；16 KB 块出现停顿（>8 s 超时中断）。这是"写一块、读回显"的严格回环口径，低于 R5 的并发排空口径（R5 日志未归档，不再引用） | `logs/20261102-164926-usb_cdc-throughput-host.log`；复跑：`python3 host/usb_cdc/bench_throughput.py --json` |
| 与 CH343 同时工作时的节点稳定性 | **已实测（软件级，2026-11-02）**：10 次开/关端口 10/10 成功，节点恒为 ttyACM2，CH343 控制台全程在；物理拔插 20 次未做（脚本 `host_replug20.sh` 已就绪） | `logs/20261102-123831-usb_cdc_acm_echo-r5b.log`（COEXIST 行） |
| 低功耗模式下的 USB 唤醒 | **已知限制（2026-11-02）**：固件未实现 remote wakeup（CherryUSB 需 `usbd_..._set_remote_wakeup` + 使能 `USBMODE` 的 remote wakeup 位）；且第 22 章实测 STOP 模式首次 WFI 即醒不来（DCDC 未配），低功耗 USB 唤醒要先解决 STOP 进入问题 | 验证：配置 remote wakeup 后主机 `echo enabled > /sys/bus/usb/devices/1-8/power/wakeup`，进入 WAIT 再 `echo 1 > .../power/level` 观察是否 resume |
| HID/DFU/MSC 类例程 | **已知限制（HID 已有（本仓 `samples/usb/usb_hid_keyboard`，见 4.2），MSC/DFU 未移植（2026-11-02）：SDK `samples/cherryusb/device/msc`、`.../dfu` 模板齐全，移植工作量约 0.5~1 天/类（改 CMake 引用 cherryusb class 源 + 描述符 + 端点））** | 移植后判据：主机 `lsusb -t` 看到对应 class，MSC 挂载后 `dd` 读写 1 MB 一致 |
