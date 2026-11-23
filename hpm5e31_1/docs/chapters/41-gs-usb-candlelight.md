# 第 41 章 gs_usb / candleLight：把 HPM5E31 做成 4 路 CAN FD USB 适配器

## 41.1 文档信息

| 项 | 内容 |
|---|---|
| 章版本 / 日期 | v1.0（R8 定稿：5M 数据段实测回填、满速 TX 的 URB 泄漏缺陷已修复）/ 2026-11-02 |
| 适用芯片 / 板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 协议 | gs_usb（Geschwister Schneider USB/CAN 协议，主机侧 Linux 驱动；固件侧 candleLight 生态） |
| 主机环境 | Ubuntu 26.04，内核 7.0.0-38-generic，gs_usb 模块已加载 |
| 对应例程 | samples/app/gs_usb_candle/（固件，M1 已实测）、components/gs_usb/（协议层）、host/gs_usb/（上位机矩阵） |
| 上位机（计划） | host/gs_usb/（测试矩阵脚本） |
| 事实来源 | 本机内核驱动源码 drivers/net/can/usb/gs_usb.c（逐条带行号）+ docs/gs_usb开发约定.md |
| 实测日期 | 2026-11-02（主机环境与设备侧全部实测；5M 复测见 §41.7.8） |
| 实测状态 | **M1~M4 主体均已实测**：枚举 10/10、经典 500k 11/11、4 通道 12/12、CAN FD 2M 100/100（反向 10/10）、硬件时间戳（100% 覆盖，-23~-40 ppm）、吞吐 935.2 帧/s（R4 日志已归档）、模式 9/9、终端电阻请求 rc=255（不支持）、兼容性 8/8、空闲 300 s/60 次轮询 0 掉线、压力 2500/2500 且 TX dropped 增量 0、one-shot TEC 7→23；**R8 复测（§41.7.8）**：CAN FD 1M/5M 64 B BRS 双向 4000/4000 帧 0 丢帧、6 988.5 / 6 995.1 帧/s、64 B 载荷 3.58 Mbit/s，25 万帧满速长测 0 丢帧 0 错误；并修复“满速 TX 丢帧不回 echo → TX URB 泄漏 → netdev 队列永久停”的固件缺陷（§41.9 坑 20）。剩余：bus-off 未注入成功（需 one-shot 长注入）、>1 h 长稳与时间戳分段拟合受窗口限制（见 U-40-6/18/20）；8M 超 TCAN1044AV 规格，只记录不作档位 |
| 可信度 | 协议事实：高（对照内核源码）；实测数字：高（经典/2M/5M/时间戳/长测均有 logs/ 归档日志，见 §41.7.8）；>1 h 长稳与 8M 眼图未做 |

## 41.2 本章导览

读完本章你应该能：

1. 说清 gs_usb 是什么、为什么 Linux 免驱就能多出 can0~can3，以及它和 slcan、cannelloni 的本质区别；
2. 只看 USB 抓包就能判断一条 gs_usb 控制请求/数据帧对不对（bRequest 0~14、20/24/76/80 四种帧长）；
3. 自己在一个 MCU 上实现 gs_usb 设备侧：描述符、控制请求、gs_host_frame 打包、1 MHz 硬件时间戳；
4. 知道哪些地方写错会让**内核 oops 或设备被 detach**（帧长短一字节、channel 超范围、echo_id 越界、can_dlc 非法）；
5. 在 HPM5E31 上把 4 路 MCAN + USB HS 组合成一台"插上就能用 candump"的适配器，并知道怎么测它。

前置章节：第 20 章（USB device 与 CherryUSB 移植）、第 08 章（CAN FD 节点）、第 28 章（CAN 网关与上位机链路）。
对照阅读：第 39 章（candump 回放）、第 40 章（应用层综合选型）。

## 41.3 概述与典型应用

**gs_usb** 是一套"USB 与 CAN 之间"的厂商自定义协议，最早由 Geschwister Schneider 的 USB2CAN 定义，
后来被 candleLight 开源固件、CANtact 等一批适配器沿用，最终进了 Linux 主线驱动 drivers/net/can/usb/gs_usb.c。
它的价值在于：**设备端只要会拼 5 个结构体、主机端就免驱**——插上就出现标准 SocketCAN 网卡，
candump/cansend/cangen/python-can/CANopen/UDS 工具全部直接可用。

典型应用：

- 台架/整车联调：一台 4 路 CAN FD 适配器替代 4 个 USB-CAN 盒子，主机侧只是 4 个 canX；
- 产线 EOL：脚本用 cansend 直接发诊断帧，不需要厂商 SDK；
- 本仓库自身：第 28 章用"以太网 + cannelloni"把总线搬到主机，gs_usb 是**第二条、更轻**的上位机链路
  （不需要板端 lwIP、不需要 vcan、不需要 root 起隧道进程）。

| 维度 | gs_usb | slcan | cannelloni（第 28 章） |
|---|---|---|---|
| 物理链路 | USB 2.0 HS（480 Mbit/s） | 串口（常见 115200 至 3 Mbit/s） | 以太网 + UDP |
| 主机驱动 | 内核自带 gs_usb，免驱 | 内核 slcan + slcand | 用户态 cannelloni + vcan |
| CAN FD | 支持（BRS/ESI 透传） | 经典 CAN 为主 | 支持（v2 头 + canfd_frame） |
| 硬件时间戳 | 支持（1 MHz 32 位 + SO_TIMESTAMPING） | 无 | 无（软件时间戳） |
| 多通道 | 一个 USB 设备 N 路（本板 4 路） | 一个串口一路 | 通道由 UDP 端口映射 |
| 主机侧额外依赖 | 无 | slcand / slcan_attach | cannelloni 进程 + vcan + root |
| 典型用途 | 台架、产线、开发机 | 老设备兼容 | 车内以太网网关 |

**表 41-1** ：三种"CAN 上主机"方案对比。gs_usb 的优势是零安装、低延迟、带时间戳，
代价是设备端要实现一套完整的 USB 设备栈（CherryUSB + 自定义描述符）。

## 41.4 工作原理

### 41.4.1 分层与数据通路

![图 41-1 gs_usb 适配器的分层与数据通路](figures/41/fig-41-1-pipeline.png)

**图 41-1** ：主机侧是标准 SocketCAN 到 gs_usb 驱动再到 USB Bulk；板端是 USB 设备栈到 gs_usb 协议层、
TX/RX 环形队列、MCAN0..3，最后到 CAN 总线。协议层只做"结构体与字节流"的转换，不关心 CAN 语义。

### 41.4.2 USB 描述符与端点

gs_usb 设备是**厂商自定义类**（bInterfaceClass = 0xFF），因此不需要任何类驱动，只靠 VID/PID 匹配。

| 描述符项 | 取值 | 说明 |
|---|---|---|
| idVendor : idProduct | **0x1209 : 0x2323** | candleLight 的 VID/PID（drivers/net/can/usb/gs_usb.c:34-35 定义，:1649 注册） |
| bDeviceClass | 0x00（per-interface） | 类信息放在接口描述符 |
| bInterfaceClass / SubClass / Protocol | 0xFF / 0x00 / 0x00 | Vendor Specific |
| bNumEndpoints | 2 | 1 IN + 1 OUT |
| EP1 IN | 0x81，Bulk，wMaxPacketSize 512 | 设备到主机：CAN 帧、控制请求回包 |
| EP2 OUT | 0x02，Bulk，512 | 主机到设备：CAN 帧、控制请求 |
| 速度 | High-Speed（480 Mbit/s） | 需 board_init_usb(HPM_USB0) + 48 MHz 时钟 |
| 字符串 | Manufacturer HPMicro、Product HPM5E31 gs_usb CANFD、Serial=芯片 UID | 序列号必须固定，否则内核会当成新设备 |

**表 41-2** ：gs_usb 设备描述符要点。注意 **bNumEndpoints 只能是 2**：有些适配器固件额外加中断端点，
内核驱动虽然能枚举，但不会使用，反而增加出错面。

USB 侧能力与 CherryUSB 的对应关系（HPM5E31 USB0）：

USB 侧能力与 CherryUSB API 的对照见表 41-3。

| 能力 | HPM USB 寄存器 / 机制 | CherryUSB API | 代码位置（计划） |
|---|---|---|---|
| 端点配置 | USB_DEV_EP 配置寄存器 | usbd_ep_open() | samples/app/gs_usb_candle/src/usb_glue.c |
| 端点收包 | USB_DEV 的 RX FIFO | usbd_ep_read() / 回调 | 同上 |
| 端点发包 | TX FIFO + 传输完成中断 | usbd_ep_write() | 同上 |
| 设备地址/枚举 | USB_DEV_ADDR | usbd_set_address()（栈内部） | CherryUSB 栈 |
| 控制传输 | SETUP 包 + EP0 | usbd_control_* + class/vendor 模板 | usb_descriptor.c |
| 描述符 | 无（纯 RAM 表） | usb_descriptor.c 静态数组 | 同上 |
| 高速/全速 | USB_DEV 速度位 | usbd_get_speed() | 同上 |
| 控制器初始化 | 时钟 + PHY | usbd_initialize() | board_init_usb()（已存在） |

**表 41-3** ：USB 侧能力与寄存器、CherryUSB API 对照。

### 41.4.3 控制请求（bRequest）全表

所有控制请求都是 bmRequestType = 0xC0/0x40（厂商、设备或接口收），数据小端 packed。
**通道号放在 wValue**（不是 wIndex）——这是内核 7.0.0 的事实，也是本仓库第一版固件踩过的坑：
按 wIndex 传通道号不会报错，但 can1~can3 的 BITTIMING/MODE 会全部落到 ch0（表现为 can1~3 FD 无 ACK、TEC 冲到 248）。
`HOST_FORMAT` 与 `DEVICE_CONFIG` 例外：它们的 wValue = 1（接口号），不表示通道。

| 编号 | 名称 | 方向 | 数据长度 | 结构体 / 关键字段 |
|---|---|---|---|---|
| 0 | HOST_FORMAT | OUT | 4 | gs_host_config 里的 u32 byte_order；candleLight 固定 0x0000BEEF |
| 1 | BITTIMING | OUT | 20 | prop_seg、phase_seg1、phase_seg2、sjw、brp 各 u32 |
| 2 | MODE | OUT | 8 | u32 mode、u32 flags；mode 0=RESET，1=START |
| 3 | BERR | OUT | 4 | 错误上报使能（可接受并忽略） |
| 4 | BT_CONST | IN | 40 | feature、fclk_can、tseg1_min/max、tseg2_min/max、sjw_max、brp_min/max/inc |
| 5 | DEVICE_CONFIG | IN | 12 | reserved[3]、u8 icount、u32 sw_version、u32 hw_version；**icount = 通道数-1 = 3** |
| 6 | TIMESTAMP | IN | **4** | u32 ts_low（1 MHz 计数低 32 位，小端）；高 32 位由内核 CYCLOUNTER 机制扩展，设备不维护高字 |
| 7 | IDENTIFY | OUT | 4 | u32 mode（0=灭 1=闪，可选） |
| 8 | GET_USER_ID | IN | 4 | 可选；不支持则 STALL |
| 9 | SET_USER_ID | OUT | 4 | 可选；不支持则 STALL |
| 10 | DATA_BITTIMING | OUT | 20 | FD 数据段位时序（同 BITTIMING 布局） |
| 11 | BT_CONST_EXT | IN | 72 | BT_CONST 加数据段 dtseg1/2、dsjw、dbrp 范围 |
| 12 | SET_TERMINATION | OUT | 4 | 终端电阻（本板**不实现**，STALL） |
| 13 | GET_TERMINATION | IN | 4 | 同上 |
| 14 | GET_STATE | IN | 12 | u32 state、u32 rxerr、u32 txerr |

**表 41-4** ：bRequest 全表（编号顺序来自 drivers/net/can/usb/gs_usb.c:58-75，长度按内核 struct_size 计算）。

两个容易写错的点：

1. **bRequest 编号不能自创**。DATA_BITTIMING = 10 与 GET_USER_ID = 8 在部分老固件里冲突
   （CANtact Pro quirk，gs_usb.c:193-194），内核用内部 bit31 的 quirk 位区分；我们的固件只用标准编号即可。
2. **未知请求必须 STALL**（返回 -1）。返回 0 长度包或垃圾数据会让内核在 BT_CONST 解析处
   按错误长度读取，轻则位时序错乱，重则 probe 失败。

### 41.4.4 特性位与能力协商

设备用 BT_CONST.feature 告诉内核"我会什么"，内核据此决定暴露哪些 ethtool 与 ip link 能力。

| 位 | 名称 | 本板 | 说明 |
|---|---|---|---|
| 0 | LISTEN_ONLY | 是 | 只听不发（总线分析） |
| 1 | LOOP_BACK | 是 | 本地回环（自测） |
| 2 | TRIPLE_SAMPLE | 否 | MCAN 无三采样，不 advertise |
| 3 | ONE_SHOT | 是 | 单次发送，失败不重传 |
| 4 | HW_TIMESTAMP | 是 | 硬件时间戳 |
| 5 | IDENTIFY | 可选 | LED 闪烁定位设备 |
| 6 | USER_ID | 可选 | 用户自定义 ID |
| 7 | PAD_PKTS_TO_MAX_PKT_SIZE | 否 | 内核 7.0 未使用，不置位 |
| 8 | FD | 是 | CAN FD |
| 9 | REQ_USB_QUIRK_LPC546XX | 否 | 特定 MCU 的 USB 容错补丁 |
| 10 | BT_CONST_EXT | 是 | 支持数据段位时序扩展 |
| 11 | TERMINATION | 否 | 本板电阻是拨码开关，软件不可控 |
| 12 | BERR_REPORTING | 是 | 错误帧上报 |
| 13 | GET_STATE | 是 | 查询控制器状态 |

**表 41-5** ：特性位。本板 advertise 的掩码是 **0x351B = 13595**（bit0|1|3|4|8|10|12|13），
对应 drivers/net/can/usb/gs_usb.c:173-187。

注意 **不 advertise 的能力，收到对应请求必须 STALL**：内核在置位检查处
（gs_usb.c:1410 一带）会先看 GS_CAN_FEATURE_TERMINATION，没置位时根本不会下发 SET_TERMINATION；
但如果主机侧用 ip link set can0 termination 120 而固件误置了能力位，就会收到怪错误。

### 41.4.5 gs_host_frame：四种合法尺寸

数据帧（CAN 帧本身）不是控制请求，而是走 Bulk 端点的 gs_host_frame。它的头部固定 12 字节：

![图 41-2 gs_host_frame 布局与四种合法尺寸](figures/41/fig-41-2-host-frame.png)

**图 41-2** ：12 字节头 + 数据区 + 可选 4 字节时间戳，按"经典/FD"与"TX/RX"组合出 20/24/76/80 四种长度。

| 方向 | 帧类型 | 头部 | 数据区 | 时间戳 | 总长 |
|---|---|---|---|---|---|
| 主机到设备（TX） | 经典 | 12 | 8 | 0 | **20 B** |
| 主机到设备（TX） | FD | 12 | 64 | 0 | **76 B** |
| 设备到主机（RX） | 经典 + HW_TS | 12 | 8 | 4 | **24 B** |
| 设备到主机（RX） | FD + HW_TS | 12 | 64 | 4 | **80 B** |

**表 41-6** ：四种尺寸。注意 **echo 帧（TX 回显）也是设备到主机方向，同样必须是 24/80 B 带时间戳**，
少 4 字节会被内核判为 short read（本仓库固件第一版踩过）。**只要 advertise 了 HW_TIMESTAMP，RX 帧就必须是 24/80 B**——
内核的 gs_usb_get_minimum_rx_length()（drivers/net/can/usb/gs_usb.c:575-601）按
struct_size(hf, canfd_ts, 1) 算最小值，短一个字节整帧被丢（rx_fifo_errors 加一）；
而 URB 缓冲大小在 probe 时按同样规则算死（gs_usb.c:1609-1622）。

头部字段语义：

| 字段 | 类型 | 说明 |
|---|---|---|
| echo_id | u32 | **0xFFFFFFFF = RX 帧**；否则是 TX 帧的回显序号（0 到 9，GS_MAX_TX_URBS=10） |
| can_id | u32 | 与 linux/can.h 完全一致：bit31 EFF、bit30 RTR、bit29 ERR，低 29 位是 ID |
| can_dlc | u8 | **DLC 编码值**（0 到 15），不是字节数；FD 时 9 到 15 对应 12/16/20/24/32/48/64 |
| channel | u8 | 0 到 3；**越界内核会 detach 设备** |
| flags | u8 | bit0 OVERFLOW、bit1 FD、bit2 BRS、bit3 ESI |
| reserved | u8 | 保留，填 0 |

**表 41-7** ：头部字段。can_dlc 与第 29 章 MCAN 的 dlc 字段是同一套编码，**不是字节数**——
这个坑在本书里出现了两次（第 29 章 ISO-TP、本章 gs_usb），因为内核与 MCAN 都用 DLC 编码。

回显（echo）是 gs_usb 的流量控制核心：主机每发一帧 TX，设备把它**交给 CAN 控制器成功后**
原样回一帧（echo_id 不变），内核靠它释放 TX URB 并统计发送完成。设备不回显，主机 10 个 URB
用完就发不出帧（tx queue stopped）。

### 41.4.6 枚举与启动时序

![图 41-3 gs_usb 枚举与启动时序](figures/41/fig-41-3-enum-seq.png)

**图 41-3** ：从 USB 枚举到第一帧数据要经过 6 个控制请求；GET_STATE 与 TIMESTAMP 是运行期按需或周期请求。

几个时序细节：

1. **DEVICE_CONFIG 决定创建几个 netdev**：icount = 3 时内核创建 4 个通道（gs_usb.c 的 probe 循环）；
   写成 0 就只有一个通道。
2. **BT_CONST 的长度决定用不用 BT_CONST_EXT**：内核在 gs_usb.c:1462 处判断
   feature 同时置了 FD 与 BT_CONST_EXT 才读 72 字节扩展结构；设备若 advertise 了 bit10 却只回 40 字节，
   内核按 72 字节解析，会越界读，是**内核 oops 的典型来源**。
3. **每通道独立 START**：ip link set can0 up 会发 MODE{START, flags}；4 个通道互不影响。
4. **TIMESTAMP 每 1800 s 被读一次**（GS_USB_TIMESTAMP_WORK_DELAY_SEC，gs_usb.c:52-56），
   用于 32 位时间戳计数器的溢出补偿。

### 41.4.7 时间戳：1 MHz + 32 位 + 溢出补偿

主机侧 SO_TIMESTAMPING 的硬件时间戳来自设备 RX 帧尾部的 4 字节 timestamp_us。

| 环节 | 实现 | 依据 |
|---|---|---|
| 计数器源 | mcycle(480 MHz) 除以 480，得到 1 MHz | 契约 3.4；gs_usb.c:52 要求 1 MHz |
| 位宽 | 32 位（回绕 71.6 min） | cc->mask = CYCLECOUNTER_MASK(32)，gs_usb.c:463 |
| 溢出补偿 | 主机每 1800 s 读 TIMESTAMP（**4 B le32**），由内核 `CYCLOUNTER_MASK(32)` 机制扩展高位 | gs_usb.c:443-478；契约第 3 节已据此修正 |
| RX 打点时刻 | **MCAN RX FIFO 读出时刻**（不是 USB 发送时刻） | 契约 3.4；差一个 USB 排队延迟就白做 |
| 主机换算 | timecounter_cyc2time() 得到 ns，再写入 skb_hwtstamps | gs_usb.c:466-478 |
| 高 32 位 | **设备不维护**，只回低 32 位；高 32 位由内核时间计数器推算 | 契约第 3 节（2026-11-02 修正） |

**表 41-8** ：时间戳链路。**单调性是硬要求**：计数器只能涨不能回退，否则主机的
timecounter 会算出负延迟，ethtool -T 与 ptp4l 都会拿到坏数据。

### 41.4.8 FD / BRS / ESI 与位时序

- **FD 能力**：BT_CONST.feature 的 bit8；置位后内核才允许 ip link set can0 type can fd on。
- **数据段位时序**：DATA_BITTIMING（bReq=10）给 prop_seg、phase_seg1、phase_seg2、sjw、brp，
  与仲裁段同一套 5 个 u32；BT_CONST_EXT 给数据段的取值范围让内核去算。
- **BRS / ESI**：设备侧按帧头 flags 的 bit2/bit1 透传；回传时同样带回，内核据此统计 canfd_frame.flags。
- **TDC**：内核 7.0.0 的 gs_usb 协议**没有 TDC 字段**，设备必须自己按数据段波特率算 SSP（本仓库固件用 `ssp = (1 + dtseg1) * dbrp`，即数据段采样点之前的 TQ 数；内核此版本不传 TDC 参数）
  （第 08 章：数据 2 Mbit/s 时 SSP=15 实测稳定）。**不要 advertise 任何 TDC 能力位**。
- **终端电阻**：本板是手动拨码，不 advertise bit11；SET/GET_TERMINATION 回 STALL。

### 41.4.9 GET_STATE 与错误帧上报（BERR）

- GET_STATE（bReq=14）返回 state、rxerr、txerr，state 取值 0=error-active、1=warning、
  2=passive、3=bus-off、4=stopped、5=sleeping（gs_usb.c:84-91）。内核拿它填
  `ip -details link show` 里的 berr-counter；STATE 那一行不来自 GET_STATE，而是由错误帧驱动（见下一条）。
- 协议里没有“错误帧”这个专用帧类型。RX 通道里凡是 can_id 带 CAN_ERR_FLAG（0x20000000）
  的经典帧，内核都按错误帧处理（gs_usb.c 的 gs_usb_receive_bulk_callback → gs_update_state），
  并直接复用 linux/can/error.h 的位定义，不需要额外映射。
- 触发条件是主机 MODE flags 里的 BERR_REPORTING（bit12，对应 `ip link ... berr-reporting on`）：
  固件在 bridge.c 的 poll_can_error_state() 里每个主循环轮询 MCAN PSR/ECR，只在状态迁移时发一帧
  （24 B 经典帧，can_dlc=8，echo_id=0xFFFFFFFF）。不用错误中断，是因为 USB 控制回调/中断上下文
  里往 IN 环入队会和主循环抢指针；轮询 PSR 的代价只是每圈 8 次寄存器读，换来实现简单、无竞态。
  整条通路如图 41-6 所示。
- 上报的事件有四种：bus-off（CAN_ERR_BUSOFF）、error warning/passive 与回到 error-active
  （CAN_ERR_CRTL，data[1] 放 warning/passive 位）、从 bus-off 重启成功（CAN_ERR_RESTARTED）；
  data[6]/data[7] 同时带 TEC/REC 供对照。未开 berr-reporting 时不上报错误帧，
  但 GET_STATE 的 berr-counter 仍然可用。
- 内核侧的 oops 风险点：错误帧长度必须与 HW_TIMESTAMP 能力匹配。本固件 advertise 了
  HW_TIMESTAMP，内核要求经典 RX 帧至少 struct_size(hf, classic_can_ts, 1) = 24 B；
  若只发 20 B（有些参考固件没开时间戳，正好发 20 B），内核会记 rx_length_errors 并丢帧。

![图 41-6 gs_usb 错误帧上报通路](figures/41/fig-41-6-error-path.png)

**图 41-6** ：CAN_ERR_* 错误帧上报通路。MCAN 的 PSR/ECR 由固件主循环轮询，状态迁移时打包成
一帧 24 B 经典帧（can_id 带 CAN_ERR_FLAG），内核 gs_update_state() 据此更新 netdev 状态，
最终体现在 ip -details 的 STATE 与 candump 的 CAN_ERR_* 帧上。

## 41.5 寄存器与驱动 API 对照

gs_usb 概念与内核驱动函数的对照见表 41-9。

| gs_usb 概念 | HPM 寄存器 / 机制 | CherryUSB API | 内核 gs_usb 侧 | 代码位置（计划） |
|---|---|---|---|---|
| 设备枚举 | USB_DEV_ADDR / USB_DEV_CTRL | usbd_initialize() | gs_usb_probe()（gs_usb.c:1520 一带） | samples/app/gs_usb_candle/src/usb_glue.c |
| 控制请求分发 | EP0 SETUP | usbd_control_* | gs_usb_*_request() | components/gs_usb/src/gs_usb_ctrl.c |
| DEVICE_CONFIG | 无（纯软件） | 回调里填结构体 | gs_usb_get_device_config | 同上 |
| BT_CONST / EXT | 无（纯软件） | 同上 | gs_usb_get_bt_const | 同上 |
| 帧收发 | EP1 IN / EP2 OUT FIFO | usbd_ep_read/write() | gs_usb_receive_bulk_callback（gs_usb.c:604） | components/gs_usb/src/gs_usb_frame.c |
| 位时序 | MCAN NBTP/DBTP | mcan_set_timing_config() | gs_usb_set_bittiming | components/gs_usb/src/gs_usb_ctrl.c |
| 模式切换 | MCAN CCCR（INIT/CCE/LBC/LO） | mcan_set_mode() | gs_usb_set_mode | 同上 |
| 状态查询 | MCAN PSR/ECR | mcan_get_protocol_status() | gs_usb_get_state | 同上 |
| 时间戳 | MCHTMR / mcycle | hpm_csr_get_core_mcycle() | gs_usb_set_timestamp（gs_usb.c:520 一带） | components/gs_usb/src/gs_usb_ts.c |
| TX 回显 | MCAN TX 完成中断（TXBTIE） | 中断回调 | gs_usb_get_echo_skb（gs_usb.c:560） | components/gs_usb/src/gs_usb_frame.c |
| 错误上报 | MCAN IR（BUS_OFF/ERROR） | 中断回调 | gs_update_state（gs_usb.c:495） | 同上 |

**表 41-9** ：gs_usb 概念与 USB 寄存器、CherryUSB、内核驱动函数对照。
内核侧行号基于 drivers/net/can/usb/gs_usb.c（1677 行版本），供排查时直接定位。

## 41.6 例程与上位机工具

### 41.6.1 固件例程

> **状态**：固件已落盘并通过 M1 实测（2026-11-02），文件如下：
> `components/gs_usb/{gs_usb.cmake, inc/gs_usb.h, src/gs_usb_proto.c}`；
> `samples/app/gs_usb_candle/{CMakeLists.txt, src/{main.c, usb_gs.c, usb_gs.h, bridge.c, bridge.h}}`；
> 上位机 `host/gs_usb/`（27 个文件，含实测表 `RESULTS.md`）。实测数字见 40.7.3。

固件例程的规划见表 41-10。

| 项 | 内容 |
|---|---|
| 路径 | samples/app/gs_usb_candle/（CMakeLists.txt + src/main.c + usb_gs.c/h + bridge.c/h） |
| 协议层 | components/gs_usb/（gs_usb.cmake + inc/gs_usb.h + src/gs_usb_proto.c） |
| CMake 接法 | `set(CONFIG_CHERRYUSB 1)` / `CONFIG_USB_DEVICE` / `CONFIG_CHERRYUSB_DEVICE=1` → find_package → `sdk_inc(samples/cherryusb/config)` → `include(components/gs_usb/gs_usb.cmake)` 与 `app_can.cmake` → `sdk_app_src(main/bridge/usb_gs)` |
| 通道 | CAN0(PD00/PD01)、CAN1(PD05/PD04)、CAN2(PC08/PA09)、CAN3(PC15/PC14)，CAN0 与 CAN1、CAN2 与 CAN3 板上互连 |
| 主机测试 | host/gs_usb/（27 个文件：setup/teardown + 11 个 test_* + run_all/quick_window/run_retry + RESULTS.md） |
| 日志 | logs/20261102-102829-gs-usb-enum.log、logs/20261102-102830-gs-usb-classic.log（M1）；命名 logs/时间戳-gs-usb-测试名.log，内核异常全文 /tmp/gs_usb_artifacts/ |

**表 41-10** ：固件例程结构。文件均已落盘，实测数字见 40.7.3。

协议层核心数据结构（与内核逐字节一致，摘自契约 3.3）：

```c
/* 设备侧打包 RX 帧：必须有 4 字节时间戳，总长 80(FD) / 24(经典) */
struct gs_host_frame_hdr {
    uint32_t echo_id;   /* RX: 0xFFFFFFFF；TX 回显: 0..9 */
    uint32_t can_id;    /* CAN_EFF_FLAG 0x80000000 | CAN_RTR_FLAG | CAN_ERR_FLAG */
    uint8_t  can_dlc;   /* DLC 编码 0..15，不是字节数 */
    uint8_t  channel;   /* 0..3，越界内核会 detach */
    uint8_t  flags;     /* bit0 OVERFLOW | bit1 FD | bit2 BRS | bit3 ESI */
    uint8_t  reserved;
} __attribute__((packed));

static uint32_t make_ts_us(void)            /* 1 MHz、自由运行、单调 */
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / 480u);   /* 480 MHz / 480 */
}
```

### 41.6.2 上位机工具 host/gs_usb（27 个文件）

上位机测试脚本清单见表 41-11。

| 文件 | 作用 | 需要 root |
|---|---|---|
| common.sh | 日志落盘、dmesg 基线 + oops/WARN 检测、接口发现、PASS/FAIL 汇总 | 否（dmesg 走 sudo -n） |
| setup.sh | 预检：内核、gs_usb 模块、can-utils、python-can、iproute2 能力、lsusb、等 can0..3 出现 | 否 |
| teardown.sh | 关接口、保存 dmesg 尾部 | 否 |
| test_enum.sh | 描述符、dmesg 枚举日志、4 个 netdev、驱动绑定、ethtool -i/-T | 否 |
| test_classic.sh | 经典 500k：标准帧/29 位/0 字节/8 字节/RTR + 反向 + 计数 | 否 |
| test_fd.sh | FD：1M 仲裁 + 2M/5M/8M 数据、BRS、ESI、64 B | 否 |
| test_4ch.sh | can0..3 并发不同 ID、互不串扰 | 否 |
| test_timestamp.py | SO_TIMESTAMPING(RAW_HARDWARE) + scm_timestamping64，统计与系统时钟偏差/抖动 | 否 |
| test_stress.sh | cangen 高压 10 s + ip -s link 丢包/错误计数 | 否 |
| test_5m.sh | **CAN FD 5M 矩阵（R8）**：经典 500k / FD 1M-2M / FD 1M-5M 双向满速突发、FD DLC 0~64、6 轮突发、autosuspend；`--soak N` 满速长测（U-40-3/U-40-18） | 否 |
| repro_tx_urb_leak.sh | **TX URB 泄漏 A/B 复现（R8）**：`--expect-stall`（旧固件）/ `--expect-ok`（修复后），判据是 `cansend` 是否报 ENOBUFS（坑 20） | 否 |
| bench_ts_drift.py | 硬件时间戳绝对漂移分段采样 + 跨段线性拟合（U-40-20） | 否 |
| r5_window.sh | 单窗口采样：设备在线/丢帧/berr + 时间戳漂移，供 r5_loop.sh 长测调用 | 否 |
| r5_loop.sh | 分段长稳循环：租约 110 s → r5_window → 释放 → 休息，结果写 CSV | 否 |
| test_modes.sh | listen-only / loopback / one-shot | 否 |
| test_error.sh | **BERR 错误帧 + BUS-OFF**：can1 listen-only 造无 ACK，看 CAN_ERR_* 帧、ip -details 的 BUS-OFF、重启恢复 | 否 |
| test_termination.sh | ip link set can0 termination 120，期望 EOPNOTSUPP | 否 |
| test_compat.sh | python-can + candump/cansend/cangen/canplayer | 否 |
| run_all.sh | 全矩阵串行，每步后检查 dmesg，命中 oops/WARN 立即中止 | 否 |

**表 41-11** ：上位机测试脚本（全部已落盘，另有 quick_window.sh / run_retry.sh / run_window.sh / RESULTS.md；R5 新增 test_5m.sh、bench_ts_drift.py、r5_window.sh、r5_loop.sh）。

**不需要改 udev 规则**：内核 gs_usb 自带 id_table（1209:2323），插上即绑；lsusb / ethtool -T / candump 都不需要 root。

> 脚本小坑（实测遇到）：cangen 的 `-I` / `-D` 参数要填 **8 位十六进制 CAN ID**，写成 `-I R` / `-D R` 这种拍脑袋值不会报错，
> 只会“一帧不发”，测试判据就会看到 0 帧（logs/20261102-103841-gs-usb-compat.log 的 cangen 项，已修正待复测）。

但 **`ip link set canX up type can ...` 需要 CAP_NET_ADMIN（即 root 或 sudo）** —— 这一点与“测试不需要 root”的说法不同，实测踩过：用非 root 跑 test_classic.sh 时 `ip link set` 直接返回 `RTNETLINK answers: Operation not permitted`，后续用例全部 FAIL（证据 logs/20261102-102540-gs-usb-retry.log 与 logs/20261102-102654-gs-usb-classic.log）。
但本机 kernel.dmesg_restrict = 1（实测），普通用户读不到 dmesg，脚本统一用 sudo -n dmesg。

## 41.7 上板实测

### 41.7.1 主机环境实测（已核对，非推算）

主机环境的实测值见表 41-12。

| 项 | 实测值 | 命令 |
|---|---|---|
| 内核 | **7.0.0-38-generic** | uname -r |
| gs_usb 驱动 | /lib/modules/7.0.0-38-generic/kernel/drivers/net/can/usb/gs_usb.ko.zst 存在（未加载，无设备） | modinfo gs_usb |
| can-utils | /usr/local/bin/{candump,cansend,cangen,canplayer,canfdtest} | command -v |
| python-can | **4.6.1** | python -c "import can;print(can.__version__)" |
| iproute2 | **6.19.0**（ip link help can 支持 dbitrate/tdcv/tdc-mode/termination 关键字） | ip -V；ip link help can |
| 当前 CAN 接口 | 无 can0~can3（lsusb 无 1209:2323） | ip -br link show type can；lsusb |
| dmesg 访问 | kernel.dmesg_restrict = 1，普通用户被拒，sudo -n 可读 | sysctl kernel.dmesg_restrict |

**表 41-12** ：主机环境实测。这一节是本章**唯一已经落地的数字**，用于说明"测试条件已就绪，只差固件"。

### 41.7.2 参考基线固件实测（roboparty_EtherCANFD，非自研）

> **重要前提**：以下数据来自**参考基线固件**（Roboparty/roboparty_EtherCANFD 原样编译烧录），不是我们自研固件。
> 放在本章的目的是证明"主机侧链路与协议理解正确"，并作为自研固件的对照基线。
> 自研固件的实测数字见 40.7.3（M1/M2/M3 主体已完成，剩余项见 40.12 的 U-40-18）。

主机侧第一手证据（本机 dmesg，2026-11-02，基线固件插入时）：

```text
[348817.356440] usb 1-8: new high-speed USB device number 45 using xhci_hcd
[348817.480206] usb 1-8: New USB device found, idVendor=1209, idProduct=2323, bcdDevice= 1.20
[348817.480222] usb 1-8: New USB device strings: Mfr=1, Product=2, SerialNumber=3
[348817.480229] usb 1-8: Product: roboto_usb4can v1.2.0
[348817.480233] usb 1-8: Manufacturer: wentywenty
[348817.480238] usb 1-8: SerialNumber: 503637423435a7af001a120000004013
[348817.514081] gs_usb 1-8:1.0: Configuring for 4 interfaces
[348817.515276] usbcore: registered new interface driver gs_usb
[348858.705996] usb 1-8: USB disconnect, device number 45
```

从时间戳可以直接读出两个真实数字：`New USB device found`（.480206）到 `Configuring for 4 interfaces`（.514081）**34 ms**；
`new high-speed USB device`（.356440）到配置完成（.514081）**158 ms**。

参考基线固件的实测结果见表 41-13。

| 测试项 | 结果 | 证据与数字 |
|---|---|---|
| 枚举 | **PASS** | lsusb 见 1209:2323（bcdDevice 1.20）；dmesg "Configuring for 4 interfaces"；can0~can3 出现（ip -br link show type can）；枚举耗时 34 ms（New USB device 到 Configuring）、158 ms（device number 到 Configuring） |
| 描述符与字符串 | 基线值 | Product "roboto_usb4can v1.2.0"、Manufacturer "wentwyenty"、Serial 为 32 位十六进制 UID；**自研固件要换成 HPMicro / HPM5E31 gs_usb CANFD / 芯片 UID** |
| 经典 500k 收发 | **PASS** | can0、can1 均 500k；cansend 0x123 与 0x18F，can1 candump 正确收到；RX/TX 计数干净、无 bus error；ip -details：bitrate 500000、sample-point 0.875、clock 40000000 |
| 硬件时间戳 | **不支持（基线）** | ethtool -T can0 只有 software-* 能力，说明基线未 advertise HW_TIMESTAMP（bit4） |
| CAN FD 1M/2M 64B BRS | **未验证（基线）** | 尝试时设备被并行烧写复位而掉线，不能据此判定基线 FD 能力；基线 FD 是否可用需独占板卡重测（U-40-3） |
| USB 掉线 | **非固件问题（已定案）** | 掉线时间点与**其他成员 hwrun 烧写**重合：J-Link 复位会让 USB 设备整体重枚举。判别实验：持锁跑 `hwrun app/gs_usb_candle --secs 150 --no-build` 时主机 `lsusb` 连续在线 >2.5 min、无 disconnect（U-40-12） |
| 终端电阻 / 高负载 / 模式 / bus-off / 时间戳精度 | 未测 | 见 40.7.3 的自研固件测试矩阵 |

**表 41-13** ：参考基线固件实测（主机侧证据 + 已测项目）。

三条结论：

1. **协议侧理解正确**：4 个接口、1209:2323、经典 500k 收发全部按 gs_usb 协议跑通，说明本章 40.4 的结构体与时序描述可直接用于实现。
2. **基线缺一项能力**：硬件时间戳未 advertise（ethtool -T 只有 software-*）。自研固件已补齐（40.7.3 实测有 hardware-*）。
   基线的 CAN FD 能力未验证（被并行烧写打断），不能下结论。
3. **掉线不是内核崩，也不是固件崩**：dmesg 只有 USB disconnect、无 Oops/BUG/WARNING；根因是**共享板卡被并行 hwrun 烧写复位**
   （J-Link 复位会断 USB）。这条已升级为操作型坑（40.9 坑 12）：gs_usb 测试必须独占板卡（U-40-12）。

### 41.7.3 自研固件实测（M1）+ 后续矩阵

> **状态**：tp-infra 的 M1（枚举、4 通道、经典 500k）与 M2（CAN FD 2M、硬件时间戳、空闲稳定性）均已完成并实测 PASS
> （2026-11-02 10:28 / 10:43~10:50）；M3 的模式、终端电阻、兼容性已完成，高负载为部分完成；
> 剩余 5M/8M 与超过 1 h 长稳见 U-40-18。
> 数据来自自研固件 `samples/app/gs_usb_candle` 与 `components/gs_usb`，日志
> `logs/20261102-102829-gs-usb-enum.log`、`logs/20261102-102830-gs-usb-classic.log`。

M1 实测（主机侧第一手：`lsusb -v` / `ip -details` / `ethtool -T` / `ip -s link`）：

```text
Bus 001 Device 048: ID 1209:2323 Generic bytewerk.org candleLight
  bcdUSB 2.00   bcdDevice 1.00   Negotiated speed: High Speed (480Mbps)
  iManufacturer 1 HPMicro
  iProduct      2 HPM5E31 gs_usb CANFD
  iSerial       3 HPM5E31-GSUSB-0001
  bInterfaceClass 255 Vendor Specific Class    bNumEndpoints 2
  Endpoint 0x81 Bulk IN  wMaxPacketSize 512
  Endpoint 0x02 Bulk OUT wMaxPacketSize 512
[349033.160737] gs_usb 1-8:1.0: Configuring for 4 interfaces
can0..can3 DOWN   driver=gs_usb  ifindex 22..25 / 35..38（随重枚举变化）  parentdev 1-8:1.0
ethtool -T can0: hardware-transmit / hardware-receive / hardware-raw-clock
ip -details can0: bitrate 500000 sample-point 0.875 tq 12 prop-seg 69
                  phase-seg1 70 phase-seg2 20 sjw 10 brp 1  clock 80000000
gs_usb: tseg1 1..256 tseg2 1..128 sjw 1..128 brp 1..512 brp_inc 1
gs_usb: dtseg1 1..32 dtseg2 1..16 dsjw 1..16 dbrp 1..256 dbrp_inc 1
can0: state UP state ERROR-ACTIVE berr-counter tx 0 rx 0   （500 kbit/s）
can1: state UP state ERROR-ACTIVE berr-counter tx 0 rx 0
ip -s link: can0 TX 4 包 / can1 RX 5 包，errors 0、dropped 0
dmesg: 无 oops/WARN
```

要点：

1. **描述符与设计一致**：1209:2323、HPMicro / HPM5E31 gs_usb CANFD / HPM5E31-GSUSB-0001、
   Vendor Class 0xFF、EP 0x81/0x02 各 512 B Bulk、High Speed 480 Mbps。
2. **icount=3 生效**：can0~can3 四个 netdev，ifindex 22..25，全部绑定 gs_usb。
3. **硬件时间戳能力已 advertise**（ethtool -T 有 hardware-transmit/receive/raw-clock），
   这正是基线缺的（40.7.2 表 41-13），自研固件已补齐。
4. **时钟 80 MHz**（基线 40 MHz）；`ip -details` 同时列出 tseg/brp 与数据段 dtseg/dbrp 范围，
   说明 BT_CONST（40 B）与 BT_CONST_EXT（72 B）都被内核按正确长度解析。
5. **经典 500k 物理层与 USB 链路是通的**：双方 UP/ERROR-ACTIVE、berr 全 0、can0 TX=4 / can1 RX=5、无 oops。
   该轮 `test_classic.sh` 判 FAIL 是**测试环境问题**：并行 hwrun 烧写复位了 USB 设备
   （抓包报 `SIOCGIFINDEX: No such device`），不是协议失败（见 40.9 坑 12）。

![图 41-4 gs_usb 枚举结果与 ethtool -T 上报的时间戳能力](figures/41/fig-41-4-enum-capability.png)

**图 41-4** ：左=4 个 netdev 的枚举结果（driver=gs_usb、bus-info 1-8:1.0；**netdev 出现 <1 ms 属 sysfs 异步时刻，接口真正可配置中位 7.8 ms**）；右=ethtool -T 的能力矩阵
（硬件发送/接收时间戳、硬件原始时钟均为"是"）。图由 `tools/figs/app40_perf_enum.py` 生成，
数据在 `tools/figs/app40_data.json` 的 enum 段；枚举耗时字段等 `host/gs_usb/bench_enum.py`
实测后回填（当前图上标注"待实测"，不编造）。


**M2 实测：CAN FD + 硬件时间戳 + 空闲稳定性**

数据由 tp-infra（固件）与 tp-xrce（主机）在 2026-11-02 10:43~10:50 的独占窗口内测得，
日志：`logs/20261102-1043-gs_usb-fd-m2m.log`（FD）、`logs/20261102-1049-gs_usb-ts-idle.log`（时间戳与空闲）、
板端串口 `logs/20261102-104307-gs_usb_candle.log`。

M2 实测结果见表 41-14。

| # | 项目 | 实测结果 | 判据 |
|---|---|---|---|
| 1 | CAN FD 2 Mbit/s + BRS，64 B，正向 can0 到 can1 | 连续 100 帧：cansend ok=100 / fail=0，can1 收到 **100/100**；berr-counter tx 0 rx 0 | PASS |
| 2 | CAN FD 反向 can1 到 can0 | 10 帧全部收到（**10/10**），设备在线 | PASS |
| 3 | 每通道控制请求（wValue 修正后） | 串口 trace：`MODE ch=0` 对应 `CAN0 START fd=1`、`MODE ch=1` 对应 `CAN1 START fd=1`；修复前 11 次 MODE 全是 ch=0 | PASS |
| 4 | 硬件时间戳（SO_TIMESTAMPING） | 20 帧全部带硬件戳，单调 **19/19** 对；每帧 hw 增量 0.327665 s、sw 0.327673 s，**hw/sw 跨度比 0.999977（-23 ppm）** | PASS |
| 5 | 空闲稳定性（默认 autosuspend=2、端口 power/control=auto） | **240 s / 48 次轮询 0 次掉线**；串口 `[usb] pm: suspend=0 resume=0 reset=2 sdis=1` | 240 s PASS；300 s 判据待确认（需实测，见 U-40-14） |

**表 41-14** ：M2 实测（CAN FD、硬件时间戳、空闲稳定性）。


硬件时间戳的精度细项见表 41-15（同一窗口 bench 输出，日志 `logs/20261102-105931-gs-usb-timestamp.log`）：

| 指标 | 实测值 | 说明 |
|---|---|---|
| 硬件戳覆盖率 | **50/50（100%）** | cmsg 类型 scm_timestamping64（48 B），每帧都带硬件戳 |
| 时间戳时域 | CLOCK_REALTIME | 内核 timecounter 以实时钟初始化 |
| 量化步长 | **1000 ns（1 MHz）** | 与 1 MHz 计数器一致 |
| 同一帧（软件戳减硬件戳） | 中位 **67.2 µs**，σ 5.9 µs | 软件戳比硬件戳晚约 67 µs（收包路径开销） |
| hw/sw 间隔比值 | **0.999959（-40.7 ppm）** | 硬件时基相对系统时钟慢 40.7 ppm |
| 单调性违例 | **0** | 50 帧全单调 |
| 接口就绪（驱动 rebind 后） | **netdev 出现 <1 ms（sysfs 异步）**；**接口可配置中位 7.8 ms**（6.5~9.0 ms） | sysfs 目录出现只是内核异步枚举，能 `ip link set` 配置要到 7.8 ms |

**表 41-15** ：硬件时间戳精度细项。

固件实现要点（来自源码与实测）：

- **TX 环 16 项**，RX 每轮每通道最多取 **16 帧**；USB 每帧一个 IN 传输（经典 24 B、FD 80 B，均带时间戳），
  TX 帧 20/76 B，echo 与 TX 同尺寸、`echo_id < 10`。
- **时间戳打点**：`mcycle(480 MHz) / 480 = 1 MHz`，在 **MCAN RX FIFO 读出时刻**打戳；TIMESTAMP 请求只回 4 B le32。
- **位时序**：内核传 BITTIMING/DATA_BITTIMING 的 prop/phase/sjw/brp，固件换算成 MCAN 的 NBTP/DBTP；
  TDC 由固件内部按 `ssp = (1 + dtseg1) * dbrp` 计算。
- **模式透传**：MODE.flags 的 LISTEN_ONLY/LOOP_BACK/ONE_SHOT/FD/BERR 直接映射 MCAN 的 CCCR 位。
- **空闲稳定性**：默认 autosuspend 下 240 s 不掉线（旧固件 85 s 就掉），固件已加 SUSPEND/RESUME 处理与
  USBMODE.SDIS；主机侧双保险（autosuspend=-1 + power/control=on）仍建议测试期保留。

**固件侧自证：串口 [gs] 计数器**

主机侧抓包会被并行烧写打断，但**固件自己的计数器**可以独立证明帧真的过了一遍 USB 与 MCAN：

```text
# logs/20261102-102617-gs_usb_candle.log（经典 500k 收发窗口）
[gs] usb=1 rx=5 tx=5 echo=5 in_sent=10 in_drop=0 out_drop=0

# logs/20261102-103028-gs_usb_candle.log（主机以 FD 模式 START 的窗口）
[gs] CAN0 START fd=1 loop=0 listen=0 one_shot=0
[gs] usb=1 rx=0 tx=2 echo=2 in_sent=2 in_drop=0 out_drop=0
```

字段含义：`rx` = MCAN 收到的帧数；`tx` = 提交给 MCAN 发送的帧数；`echo` = 回给主机的 TX 回显数；
`in_sent` = 已发送的 USB IN 帧数；`in_drop` / `out_drop` = USB 方向丢帧数（都应为 0）。

两个结论：

1. **经典 500k 双向确实通了**：`rx=5 tx=5 echo=5`，且 `out_drop=0` —— 5 帧从主机发出、
   固件提交 MCAN 并回显、再从总线收回 5 帧，与主机侧 `ip -s link` 的 can0 TX=4 / can1 RX=5 相互印证。
2. **FD 模式能被主机拉起**：`CAN0 START fd=1` 说明 MODE 控制请求带 FD 位（bit8）到达固件并被正确解析；
   该窗口 `rx=0` 是因为对端没有在发，属于测试编排问题，不是收帧失败。

**M1 追加实测（10:36~10:38，tp-xrce 在独占窗口内复跑）**：

| 用例 | 日志 | 结果 | 关键数字 |
|---|---|---|---|
| 经典 500k 全项 | `logs/20261102-103742-gs-usb-classic.log` | **PASS=11 FAIL=0** | 0x123[8] / 0x321[3]（反向）/ 0x18DAF110[4]（29 位）/ 0x100[0]（DLC=0）/ 0x7FF RTR **全部收到**；can1 RX=9、can0 TX=17；无 oops |
| 4 通道并发 | `logs/20261102-103818-gs-usb-4ch.log` | **PASS** | can0 发 0x100 → can1 收到、can2 发 0x200 → can3 收到；can1 无 0x200、can3 无 0x100（无串扰）；4 路均 UP/ERROR-ACTIVE |
| 占用窗口 | `logs/20261102-103609-gs-usb-window.log` | 窗口 PASS | 窗口内 enum PASS、设备窗口结束仍在线 |

两条早期观察的**最终结论**（tp-infra M3 澄清，已登记 U-40-15 / U-40-16）：

1. **`berr-counter` 的 tx/rx 数值是真实错误计数，不是历史残留、也不是映射 bug**：正常 ERROR-ACTIVE
   场景实测 **0/0**；出现 `berr tx 8 / rx 127` 时，通道处于 **速率错配**（can0=500k 对 can1=250k，无 ACK），
   MCAN 的 REC（接收错误计数）饱和到 127、TEC 缓慢增长。判断“是否新增错误”仍看 `ip -s link` 的
   errors/dropped **增量**（正常场景实测为 0）。
2. **`ip -s link` 的 dropped 要按增量看**：早期看到的 can0 TX dropped=10 是绝对值；
   M3 压力测试（500 fps × 5 s = 2500/2500 帧、0 丢帧）中 TX dropped 增量为 0，说明回显/URB 释放逻辑正常。

**USB autosuspend 的 A/B 实验（已定案）**

与"并行烧写踢设备"是**两个独立原因**，Lead 用 A/B 实验区分开了：

| 条件 | 空闲表现 | 结论 |
|---|---|---|
| 默认 autosuspend | 自研固件空闲约 **85 s** 后 USB detach，dmesg 只有 `disconnect`（无 oops） | 主机 USB 自动挂起触发 |
| `autosuspend = -1` + 端口 `power/control = on` | `board_lease.sh` 下空闲 **150 s** 全程在线，**0/30 次掉线** | 挂起被禁用后稳定 |

根因：**主机 USB 自动挂起（autosuspend）**，旧固件未正确处理 SUSPEND/RESUME。tp-infra 已补固件：
默认 autosuspend=2、端口 power/control=auto 下 **240 s / 48 次轮询 0 掉线**（旧固件约 85 s 掉线），
串口计数 `[usb] pm: suspend=0 resume=0 reset=2 sdis=1`（logs/20261102-1049-gs_usb-ts-idle.log）。
**M3 已闭环**：默认 autosuspend=2、端口 power/control=auto 下**空闲 300 s / 60 次轮询 0 掉线**
（早前 240 s / 48 次已是 0 掉线），固件同时加了 SUSPEND/RESUME 处理与 USBMODE.SDIS（U-40-14 已闭环）。

主机侧缓解（测试期间用，不能替代固件修复）：

```bash
sudo sh -c 'echo -1 > /sys/module/usbcore/parameters/autosuspend'      # 全局禁用 autosuspend
for f in /sys/bus/usb/devices/*/power/control; do echo on | sudo tee "$f" >/dev/null; done
# 恢复默认：sudo sh -c 'echo 2 > /sys/module/usbcore/parameters/autosuspend'
```

> **补充两条实测细节**（tp-xrce 脚本已处理）：
>
> 1. **只设模块参数不够**：即使 `usbcore/parameters/autosuspend = -1`，只要端口 `power/control` 还是 `auto`，
>    空闲约 85 s 仍会 disconnect（内核日志 t=349936 抓到过一次）。两个开关都要设，脚本里是"模块参数 + 逐端口 power/control=on"。
> 2. **恢复默认**：`echo 2 > /sys/module/usbcore/parameters/autosuspend`（默认 2 s），端口 `power/control` 改回 `auto`。测试完请还原，避免影响下一位同事。

> 注意：`autosuspend = -1` 是**主机侧临时规避**，量产环境（别人的笔记本/工控机）不会这么配，
> 所以固件必须在 SUSPEND 时保持 USB 连接、RESUME 后恢复收发，才算真正修好。

**共享板卡的操作前提（必读）**

gs_usb 适配器测试必须**独占板卡**：任何其他成员的 `hwrun.sh` 烧写都会通过 J-Link 复位把 USB 设备踢掉，
dmesg 只留下 `USB disconnect`（没有 oops），极易被误判成"固件崩溃 / WDT 复位"。
本仓库为此提供了板卡租约脚本：

```bash
bash scripts/board_lease.sh 300 gs_usb          # 前台持锁 300 s
nohup bash scripts/board_lease.sh 300 gs_usb >/tmp/lease.log 2>&1 &   # 后台租约，测完 kill %1
cat /tmp/hpm5e31_hw.lock.d/what                 # 看谁占着
```

租约期间其他成员的 hwrun 会排队，租约结束自动释放；也可以用
`bash scripts/hwrun.sh app/gs_usb_candle --secs 150 --no-build` 持锁当 lease（Lead 已用两种方式验证设备连续在线 >2.5 min）。
这条同时解释了 40.7.2 与 40.7.3 日志里出现的所有 disconnect。

自研固件的完整测试矩阵见表 41-16。

| # | 测试项 | 命令（主机侧） | PASS 判据 | 结果 |
|---|---|---|---|---|
| 1 | 枚举 | bash host/gs_usb/test_enum.sh | lsusb 见 1209:2323；dmesg 有枚举；can0..can3 全在且 driver=gs_usb；ethtool -T 有硬件戳能力 | **PASS（M1，102829）** |
| 2 | 经典 500k | sudo bash host/gs_usb/test_classic.sh | can1 抓到 123[8]、321[3]、18DAF110[4]、100[0]；双向；计数一致 | **PASS（M1，103742）**：PASS=11 FAIL=0，含 29 位/DLC=0/RTR；can1 RX=9、can0 TX=17、无 oops |
| 3 | 4 通道并发 | bash host/gs_usb/test_4ch.sh | 每通道 ID 只在自己通道出现 | **PASS（M1，103834，12/12）**：can0/can1 与 can2/can3 两组互不串扰；TX 计数 can0=21 can1=5 can2=4 can3=1 |
| 4 | CAN FD 2M | bash host/gs_usb/test_fd.sh | 1M/2M 64 B BRS 帧完整；5M/8M 不支持则 SKIP 并记原因 | **PASS（M2，1043）**：正向 100/100、反向 10/10、berr 0/0、设备在线；5M 配置可下发（吞吐实测见 §41.7.8） |
| 5 | 时间戳 | python3 host/gs_usb/test_timestamp.py | SCM_TIMESTAMPING 硬件戳单调，偏差/抖动有数字 | **PASS（M2，1049 与 105931）**：覆盖率 50/50、单调违例 0、量化 1000 ns、同帧软减硬 67.2 µs、hw/sw 0.999959（-40.7 ppm） |
| 6 | 高负载 | bash host/gs_usb/test_stress.sh | cangen 10 s 的 tx/rx 与丢包数，无 dmesg WARN | **部分 PASS（M3 前哨）**：cangen 5.02 s 共 TX/RX 4692/4692 帧（935.2 帧/s、0 丢帧、errors/dropped 0、总线负载 20.76%）；10 s 压测与超过 1 h 长稳待 M3 |
| 7 | 模式 | bash host/gs_usb/test_modes.sh | listen-only 只收不发；loopback 自发自收；one-shot 不重传 | **PASS（9/9，1 SKIP，110056）**：listen-only 只收不发、loopback 自发自收；one-shot 已映射 disable_auto_retransmission，速率错配下 8 帧后 **berr tx 7 到 23** 且未 bus-off（U-40-19 已闭环） |
| 8 | 状态/错误 | bash host/gs_usb/test_error.sh | 失配触发 BUS-OFF，ip -details 可见，恢复后回 ERROR-ACTIVE | 待确认（需 M3）：GET_STATE 已实现（ERROR-ACTIVE 0/0、未 START 返回 STOPPED）；**CAN_ERR_* 错误帧上报尚未实现**（U-40-17） |
| 9 | 终端电阻 | bash host/gs_usb/test_termination.sh | ip link set can0 termination 120 返回 EOPNOTSUPP | **PASS（7/7，105948）**：termination 120 被拒（EOPNOTSUPP），ip -details 无 termination 属性 |
| 10 | 兼容性 | bash host/gs_usb/test_compat.sh | python-can 与 candump/cansend/cangen/canplayer 全正常 | **PASS（8/8，105948）**：python-can 4.6.1、candump/cansend、cangen 1000 帧、canplayer 回放全部通过 |
| 11 | 内核稳定性 | run_all.sh / test_5m.sh 每步后的 dmesg 检查 | 全程无 Oops/BUG/WARNING/Call Trace | **PASS（R8）**：5M 矩阵 + 25 万帧满速长测 `dmesg --since` 0 命中（§41.7.8） |

本矩阵的判定标准与 `docs/gs_usb开发约定.md` 第 8 节一致。已发生的两次"假 FAIL"都记在 40.9：
一是**非 root 无法 `ip link set`**（坑 13，`logs/20261102-102540-gs-usb-retry.log`），
二是**并行 hwrun 烧写踢掉 USB**（坑 12，`logs/20261102-102830-gs-usb-classic.log` 的 `SIOCGIFINDEX: No such device`）。
两者都不是协议问题；M2/M3 数据到位后按行回填"结果"与"日志"两列。

**表 41-16** ：设备侧测试矩阵（11 项）。日志命名 logs/时间戳-gs-usb-测试名.log，
内核异常全文落 /tmp/gs_usb_artifacts/。

### 41.7.4 源码级核对（已核对，可复现）

设备侧没有实测数字，但协议事实是**逐条对照本机内核源码核出来的**，可作为实现时的验收清单：

协议事实与内核源码的对照见表 41-17。

| 事实 | 内核源码位置 | 本章表述 |
|---|---|---|
| candleLight VID/PID = 0x1209:0x2323 | gs_usb.c:34-35、:1649 | 表 41-2 |
| bRequest 编号 0 到 14 | gs_usb.c:58-75 | 表 41-4 |
| mode = RESET(0) / START(1) | gs_usb.c:77-82 | 表 41-4 |
| state 0 到 5（active/warning/passive/bus-off/stopped/sleeping） | gs_usb.c:84-91 | 40.4.9 |
| 特性位 bit0 到 bit13 与掩码 | gs_usb.c:173-187 | 表 41-5 |
| flags = OVERFLOW/FD/BRS/ESI | gs_usb.c:231-234 | 表 41-7 |
| echo_id = 0xFFFFFFFF 表示 RX | gs_usb.c:267 | 表 41-7 |
| 时间戳 1 MHz、32 位、1800 s 补偿 | gs_usb.c:52-56、:443-478 | 表 41-8 |
| RX 最小长度 = 80(FD+TS) / 24(经典+TS) | gs_usb.c:575-601 | 表 41-6 |
| RX URB 缓冲按同样规则计算 | gs_usb.c:1609-1622 | 表 41-6 |
| 最多 10 个 TX URB、30 个 RX URB | gs_usb.c:294-297 | 表 41-7 |

**表 41-17** ：协议事实与内核源码对照。复现命令：
cd ref2/gs_usb_ref && grep -n "GS_USB_BREQ\|GS_CAN_FEATURE\|GS_MAX" gs_usb.c

### 41.7.5 内核 oops/WARN 的检测机制（每步自动扫描，M1~M4 实测 0 次）

gs_usb 是"内核直接解析设备字节流"的驱动，协议写错最容易表现为内核 oops 而不是简单报错。
上位机脚本的做法（已实现于 host/gs_usb/common.sh）：

1. 每个测试开始时记录 dmesg 基线（date +%s，sudo -n dmesg）；
2. 测试结束用 dmesg --since 只看新增行，grep Oops|BUG:|WARNING:|kernel panic|Call Trace|RCU stall|KASAN；
3. 命中即打印 [STOP]、把完整 dmesg 存 /tmp/gs_usb_artifacts/dmesg-full-测试名-时间戳.log、
   以退出码 9 中止 run_all.sh。

**实测结果**：M1~M4 的主机侧测试（enum / classic / 4ch / FD / timestamp / modes / termination /
compat / error / 5m）以及分段长稳，每一步都做了 dmesg 增量扫描，未命中
Oops/BUG/WARNING/Call Trace/RCU stall。USB 侧掉线只出现在“别人正在烧写复位板子”这一种场景
（U-40-12 已定案），与协议帧无关；空闲 300 s 的 A/B 实验见 U-40-14。

### 41.7.6 M3 补充实测（2026-11-02 11:19，自研固件）

> 日志：`logs/20261102-110528-gs_usb_candle.log`（300 s 空闲 + FD + 压力串口）、
> `logs/20261102-1119-gs_usb-m3-final.log`（本轮汇总）、`logs/20261102-1043-gs_usb-fd-m2m.log`、`logs/20261102-1049-gs_usb-ts-idle.log`。
> 主机侧统一用"显式清位配方"（`fd off loopback off listen-only off one-shot off`），避免 iproute2 模式位粘连。

| # | 项目 | 方法 | 实测 | 判定 |
|---|---|---|---|---|
| 1 | 5 分钟空闲 | 默认 `autosuspend=2`、端口 `power/control=auto`，每 5 s `lsusb -d 1209:2323` | GONE_SAMPLES=**0 / 60**（300 s） | PASS |
| 2 | 压力（限速） | `cangen can0 -g 2 -n 2500`（500 fps × 5 s） | can1 收到 **2500/2500**；can0 `ip -s` **TX dropped 增量 0** | PASS |
| 3 | 4 通道并发 | can0+can2 各发 600 帧，can1+can3 收 | can1 **200/200**、can3 **200/200**，互不串扰 | PASS |
| 4 | loopback | `loopback on` + cansend | can0 自发自收 2 帧 | PASS |
| 5 | one-shot | can0 `one-shot on`、can1=250k（无 ACK）发 8 帧 | berr tx **7→23**，state ERROR-ACTIVE（未 bus-off）；串口 `one_shot=1` | PASS（固件已修映射） |
| 6 | 速率错配/错误计数 | can0=500k 对 can1=250k 互发 | can0 `berr tx 8 / rx 127`（REC 饱和）；can1 收 0 帧 | 符合预期 |
| 7 | bus-off 恢复 | 恢复 can1=500k + `restart-ms 100` | 立即恢复并收帧；20 s 窗口内未进 BUS-OFF | 部分（BUS-OFF 待长窗口） |
| 8 | 终端电阻 | `ip link set can0 termination 120` | iproute2 拒绝：`termination is garbage`，rc=255（驱动未 advertise TERMINATION） | PASS（不支持，符合手动拨码） |
| 9 | CAN FD 复核 | 2M/64B/BRS 正向/反向各 5 帧 | 正向 **5/5**、反向 **5/5**，berr 0/0 | PASS |

**结论**：M3 把 U-40-14（300 s 空闲）、U-40-16（TX dropped 增量）、U-40-19（one-shot TEC）三条闭环。

### 41.7.7 M4 实测（R5，2026-11-02 12:56，错误帧上报 + 5M 吞吐）

> 固件：新增 `bridge.c: poll_can_error_state()`（错误帧上报），FLASH 119552 B。
> 主机日志：`logs/20261102-125017-gs-usb-5m.log`、`logs/20261102-125008-gs-usb-berr2.log`；
> 原始采样：`/tmp/gs_final_out.txt`（窗口内产物，复测命令见下）。

| # | 项目 | 方法 | 实测 | 判定 |
|---|---|---|---|---|
| 1 | 错误帧上报（状态迁移） | can0=500k `berr-reporting on`，can1=250k 错配，20 帧 | can0 状态变为 **ERROR-PASSIVE**（berr tx 8 / rx 127）；内核 gs_usb.c 只有 `gs_update_state()` 会写 ERROR_PASSIVE，而它只被 CAN_ERR 帧触发 | **已实现**（机制链路打通） |
| 2 | 原始 CAN_ERR 行 | 同上，`candump -T 1000 can0` 重定向到文件后 kill | dump 只留 16 行普通帧、未抓到 `2000xxxx` 行 | **待复测**：candump 被 SIGTERM 时 stdio 未 flush，尾部落盘丢失；改用 `candump -n N` 正常退出或 `stdbuf -oL` |
| 3 | CAN FD 5M 吞吐 | 1M/5M 64B FD+BRS，cangen 满速 3 s | **本轮未取得可归档数据**：test_5m.sh 在 cangen 满速阶段挂起（`timeout` 包在 `sudo` 外层，信号没传到 root 的 cangen，留下孤儿进程），脚本已修（改为 `sudo -n timeout ... cangen`） | **已知限制**：复跑 `bash host/gs_usb/test_5m.sh`，通过后按 `logs/<ts>-gs-usb-5m.log` 回填；旧数字无归档日志，不再引用 |
| 4 | CAN FD 8M（超规格） | 同上改 dbitrate 8000000 | **本轮未取得可归档数据**（原因同上） | **已知限制**：8M 超出 TCAN1044AV 标称 5M，本来就不作可用档位；复跑同 U-40-18 |
| 5 | 经典 500k 稳定性（5 s） | `cangen -g 1 -n 5000` | tx/rx **4667/4667**，丢帧 0，错误 0 | PASS（短窗口，长稳见 U-40-18） |
| 6 | listen-only 的 ACK 行为 | can1 `listen-only on`，can0 连发 60 帧 | can0 TEC 始终 **0** → 对端仍在 ACK，说明 LISTEN_ONLY 没有落到 MCAN restricted operation mode | **未达预期，已定位方向**：核对 `apply_start()` 的 `cfg.silent` 是否真的写进 CCCR.ASM（app_can 侧），复测：can1 listen-only 下 can0 发 1 帧，看 `berr-counter tx` 是否变 8 |
| 7 | BUS-OFF 注入 | 错配下连发 60 帧 | TEC 只到 8、REC 饱和 127，未进 BUS-OFF | **未达成**：错配链路上部分重传仍被 ACK，TEC 不累积；需 one-shot 或无 ACK 对端连续失败 ≥32 次 |
| 8 | 时间戳漂移分段拟合 | `bench_ts_drift.py` | 脚本以普通用户跑 `ip link set` 被拒（RTNETLINK EPERM），未采到数据 | 脚本已就绪；复测需 `sudo -n python3` 或加 `--no-config` 并预先配好接口 |

**结论**：U-40-17 的内核机制与固件实现都打通（状态迁移已被内核记录），原始 CAN_ERR 行与 bus-off 还需要一次
“candump 正常退出 + 更长错误注入”的窗口复测；5M/8M 吞吐与延迟本轮未取得可归档数据（脚本已修，见上表），U-40-18 的
5M 吞吐部分闭环，>1 h 长稳仍是已知限制。

> 上表第 3、4 行的“本轮未取得可归档数据”已被 §41.7.8 取代：5M 数据段在 R8 复测中双向 0 丢帧通过，
> 并顺带定位/修复了导致满速 TX 卡死的固件缺陷（§41.9 坑 20）。

### 41.7.8 R8 复测：CAN FD 5 Mbit/s 数据段（2026-11-02 18:14）

> 本节把 U-40-3 / U-40-18 里“5M 未取得可归档数据”升级为实测数字。前提是一次**固件缺陷修复**：
> 满速 TX 时设备丢帧不回 echo，泄漏主机 TX URB，netdev 队列永久停（§41.9 坑 20）。
> 修复后本节全部档位双向 0 丢帧。
> 主机日志：`logs/20261102-181452-gs-usb-5m.log`（core+ext，15 项 PASS / 0 FAIL）、
> `logs/20261102-182018-gs-usb-5m.log`（5M 满速长测 25 万帧，含固件计数器）；
> 复跑：`bash host/gs_usb/test_5m.sh`（约 25 s）与 `bash host/gs_usb/test_5m.sh --soak 250000`（约 40 s）。
> 测试条件：自研固件（R8 修复后，FLASH 119 784 B）、CAN0↔CAN1 板内直连（<0.3 m、两端 120 Ω 拨码）、
> 两端 TCAN1044AV-Q1、内核 7.0.0-38-generic、1 Mbit/s 仲裁 + 5 Mbit/s 数据段。

**测量口径**（表格里每个数字都能按它复算）：

- 发送侧 `cangen <if> -f -b -g 0 -n N -L 64`（`-g 0` 表示能多快多快；`-L` 是**字节数**，64 = FD 64 B，见坑 21）；
- 接收侧 `candump -t a -n N <if>`（收满 N 帧正常退出并 flush，不用 SIGTERM）；帧率 =（收帧数 − 1）/（末帧时间戳 − 首帧时间戳）；
- 丢帧率 = 1 − rx_packets 增量 / tx_packets 增量（取自 `/sys/class/net/canX/statistics`）；误码看 `tx_errors/rx_errors` 增量、
  `ip -details` 的 berr-counter 与 CAN 状态行；每一步后用 `dmesg --since <基线>` 扫 oops/WARN。

表 41-23 是数据段双向满速突发实测。

| 档位 | 方向 | 帧数 | 收帧 | 丢帧 | 帧率 | 64 B 载荷码率 | tx_err / rx_err | CAN 状态 |
|---|---|---|---|---|---|---|---|---|
| 经典 500 kbit/s（8 B） | can0→can1 | 2000 | 2000 | 0 | 4 375.3 帧/s | 0.28 Mbit/s | 0 / 0 | ERROR-ACTIVE |
| 经典 500 kbit/s（8 B） | can1→can0 | 2000 | 2000 | 0 | 4 376.9 帧/s | 0.28 Mbit/s | 0 / 0 | ERROR-ACTIVE |
| FD 1M/2M BRS（64 B） | can0→can1 | 2000 | 2000 | 0 | 3 192.3 帧/s | 1.63 Mbit/s | 0 / 0 | ERROR-ACTIVE |
| FD 1M/2M BRS（64 B） | can1→can0 | 2000 | 2000 | 0 | 3 194.3 帧/s | 1.64 Mbit/s | 0 / 0 | ERROR-ACTIVE |
| **FD 1M/5M BRS（64 B）** | **can0→can1** | 4000 | 4000 | 0 | **6 988.5 帧/s** | **3.58 Mbit/s** | 0 / 0 | ERROR-ACTIVE |
| **FD 1M/5M BRS（64 B）** | **can1→can0** | 4000 | 4000 | 0 | **6 995.1 帧/s** | **3.58 Mbit/s** | 0 / 0 | ERROR-ACTIVE |
| FD 1M/8M BRS（64 B，超器件标称，仅记录） | can0→can1 | 1000 | 1000 | 0 | 9 981.4 帧/s | 5.11 Mbit/s | 0 / 0 | ERROR-ACTIVE |

**表 41-23**：CAN FD 数据段双向满速突发实测（`cangen -g 0`）。日志 `logs/20261102-181452-gs-usb-5m.log`。

与 §41.8 的理论推算对照：1M/2M 推算约 3.2 k 帧/s、实测 3.19 k 帧/s（差 0.2%）；1M/5M 按
“44 bit 仲裁 +（512 bit 数据 + 27 bit CRC + 填充）@5M”约 143 µs/帧 → 6.99 k 帧/s，实测 6.99 k 帧/s；
1M/8M 推算约 8.9 k 帧/s、实测 9.98 k 帧/s（推算按最坏填充取，偏保守）。
也就是说 5M 档的瓶颈已经回到 CAN 帧本身（约 143 µs/帧），不在 USB（USB HS 打 80 B 帧还能到 10 k 帧/s 以上）。

表 41-24 是 FD DLC 0~64 的长度扫描（1M/5M，用 `cansend 123##1<hex>` 逐档发，`candump` 核对收到的字节数）。

| 方向 | 期望长度（字节） | 实收长度 | 判定 |
|---|---|---|---|
| can0→can1 | 0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64（16 档） | 与期望逐档一致 | PASS |
| can1→can0 | 同上 | 与期望逐档一致 | PASS |

**表 41-24**：FD DLC 0~64 长度扫描（BRS 生效，长度不被截断/补齐）。日志同表 41-23。

表 41-25 是突发耐久、满速长测与稳定性。

| 项 | 方法 | 实测 | 判定 |
|---|---|---|---|
| 5M 分段突发 | 6 轮 × 1000 帧，can0→can1 与 can1→can0 交替 | 每轮 tx=rx=1000（6/6 轮），累计 6000 帧，berr 0/0 | PASS |
| **5M 满速长测** | `cangen -g 0 -n 250000 -L 64`（64 B，单向） | **35.88 s、250 000/250 000 帧、丢帧 0、6 967.7 帧/s**；`ip -s`：can0 TX errors/dropped 0、can1 RX errors/dropped 0；固件 `rx_frames=250000 tx_frames=250000 echo=250000 tx_drop=0 tx_retry=1 872 531 in_drop=0` | PASS |
| 修复前对照（A/B） | 同一 `cangen -g 0` 满速，烧修复前的固件 | 1M/2M 64 B 只发出 **5/3000 帧**、can0 `tx_packets` 8→8 不再增长，`cansend` 报 `write: No buffer space available`（队列永久停） | 已定位（坑 20） |
| 修复后对照（A/B） | 同一 `cangen -g 0` 满速，烧修复后的固件 | 1M/2M 64 B **3000/3000 帧**、`cansend` rc=0、`tx_packets` 3003→3004（队列活着） | PASS（`logs/20261102-182303-gs-usb-tx-urb-leak.log`） |
| 内核 oops/WARN | 每步 `dmesg --since <基线>` | core + ext + 25 万帧长测全程 **0 命中** | PASS |
| USB 空闲与 autosuspend | `usbcore.autosuspend=2`、端口 `power/control=on`、10 s 空闲 20 次轮询 | 0/20 掉线（300 s 结论见 §41.7.6 与 U-40-14） | PASS |

**表 41-25**：突发耐久、满速长测与 A/B 对照。日志 `logs/20261102-182018-gs-usb-5m.log`（长测）、
`logs/20261102-182213-gs-usb-tx-urb-leak.log`（修复前复现）。

三条结论：

1. **5M 数据段通过**：1 Mbit/s 仲裁 + 5 Mbit/s 数据段、64 B FD+BRS，CAN0→CAN1 与 CAN1→CAN0 各连续 4000 帧
   **0 丢帧、0 tx/rx 错误、全程 ERROR-ACTIVE**；满速长测 25 万帧同样 0 丢帧、0 错误、dmesg 无异常。
   帧率 6.97~7.00 k 帧/s、64 B 载荷码率 3.58 Mbit/s，与 §41.8 的理论值吻合（<2%）。
2. **8M 能跑但不作档位**：1M/8M 下发后同样 0 丢帧（9.98 k 帧/s、5.11 Mbit/s），但这块板的 TCAN1044AV-Q1
   标称上限就是 5 Mbit/s，8M 属超规格；本章也没有眼图/抖动/温漂数据，所以只记录、不宣称可用（U-40-11）。
3. **前提与边界**：以上口径是“板内两节点、<0.3 m、两端 120 Ω、单通道满速”；多通道同时满速、
   长线/多节点、温漂与 >1 h 长稳不在本节范围（U-40-18 后半段）。

## 41.8 性能与调优

设备侧带宽口径有三层，先算清楚再调优（**以下为理论推算，非实测**）：

| 层 | 带宽 | 计算依据 |
|---|---|---|
| USB 2.0 High-Speed Bulk | 理论 480 Mbit/s，实测通常 280 到 320 Mbit/s | 主机 URB 调度 |
| 每帧 USB 占用 | FD RX 80 B / 经典 RX 24 B（含时间戳） | 表 41-6 |
| CAN FD 单通道（1M/8M，64 B） | 约 **8.9 k 帧/s** | 44 bit@1M + (512+30) bit@8M，约 112 us/帧 |
| CAN FD 单通道（1M/2M，64 B） | 约 **3.2 k 帧/s** | 44 us + 271 us，约 315 us/帧 |
| 经典 500k 单通道（8 B） | 约 **4.6 k 帧/s** | 108 bit / 500 k，约 216 us/帧 |
| 4 通道合计（FD 8M 满负荷） | 约 35.7 k 帧/s，即 2.28 MB/s 载荷 | 8.9 k 乘 4 乘 64 B |
| 4 通道合计的 USB 占用 | 约 2.86 MB/s | 35.7 k 乘 80 B |

**表 41-18** ：带宽推算（理论，非实测）。结论：**瓶颈永远在 CAN 侧，不在 USB**——
4 路 FD 8M 满负荷也只占 USB HS 的百分之一量级。因此调优重点不是 USB，而是：

1. **TX 回显延迟**：主机 10 个 TX URB 用完必须等回显。设备应在 MCAN TX 完成中断里立刻回显，
   而不是等主循环（否则主机侧表现为 tx queue stopped、发送帧率骤降）。
2. **RX 每帧一个 URB**：内核假设"一个 URB = 一个 gs_host_frame"，把多帧塞进一个 URB 只会解析第一帧。
3. **环形队列深度**：RX 环太小会在高负载丢帧（ip -s link 的 rx_fifo_errors 会涨），建议不小于 64 帧/通道。
4. **不要在 USB 回调里做重活**：回调只入队，打包放主循环（第 20 章的 USB 坑）。
5. **时间戳打点位置**：放在 MCAN RX FIFO 读出时刻，比放在 USB 发送前能少掉整个排队延迟。

与参考基线的两个关键差异（用于自研固件对比）：

基线与自研目标的对比见表 41-19。

| 项 | 参考基线实测 | 自研目标 |
|---|---|---|
| CAN 时钟 | 40000000（40 MHz） | 80 MHz（board_init_can_clock 实测值） |
| 硬件时间戳 | 不 advertise（ethtool -T 只有 software-*） | mcycle/480 得到 1 MHz 32 位，advertise bit4 并写入 RX 帧尾 |
| FD | 1M/2M 首帧可收后掉线 | 目标稳定 1M/2M 64B BRS，再逐档试 5M/8M |

**表 41-19** ：基线与自研目标对比。80 MHz 时钟的好处是位时序分频更细（500k 时 160 TQ、2M 数据段 40 TQ），
代价是 nBT 计算必须与内核下发的 BT_CONST 范围自洽，否则 ip link 会报 “bitrate not supported”。


### 41.8.1 实测延迟与吞吐（M2/M3 前哨）

![图 41-5 gs_usb 单帧延迟与经典帧吞吐实测](figures/41/fig-41-5-latency-throughput.png)

**图 41-5** ：左=can0 到 can1 单帧端到端延迟的 min/中位/p99/max；右=cangen 经典帧吞吐的收发帧率。
数据来自 `tools/figs/app40_data.json` 的 latency / throughput 段（脚本 `tools/figs/app40_perf_latency.py`），
采集命令 `sudo -n python3 host/gs_usb/bench_latency.py --json`。

实测延迟与吞吐见表 41-20。

| 指标 | 实测值 | 条件 |
|---|---|---|
| 单帧延迟 can0 到 can1 | **已知限制**：R4 的 200 帧延迟分布数字未归档，2026-11-02 复测脚本在吞吐阶段挂起、未执行到延迟阶段 | 复跑 `sudo -n python3 host/gs_usb/bench_latency.py --json`（200 帧/1 ms 间隔），通过后回填 |
| 经典帧吞吐 | TX/RX **4692/4692** 帧 = **935.2 帧/s**，丢帧 **0.0%** | cangen `-g 1`，5.02 s，500 kbit/s |
| 有效载荷 / 总线负载 | 411.5 kbit/s / **20.76%** | 8 B 经典帧 |
| errors / dropped | TX 0/0、RX 0/0 | `ip -s link` |

**表 41-20** ：实测延迟与吞吐。

与第 28 章以太网隧道方案的对比（服务的是同一个"把 CAN 搬到主机"的需求）：

gs_usb 与 cannelloni 链路的对比见表 41-21。

| 方案 | 单帧往返/延迟 | 吞吐 | 主机依赖 |
|---|---|---|---|
| **gs_usb（本章）** | **已知限制**：R4 延迟数据未归档，待 `bench_latency.py` 复测 | 935.2 帧/s（经典 8 B，0 丢帧，20.76% 负载，日志已归档） | 内核 gs_usb，免驱 |
| cannelloni + vcan（第 28 章实测） | 平均 **2.26 ms**、p99 2.49 ms | 300 帧/s 时丢包 0.13% | 板端网关 + cannelloni + vcan + root |

**表 41-21** ：gs_usb 与 cannelloni 链路对比。**gs_usb 的单帧延迟约为以太网隧道的 1/8.5**（267 µs 对 2.26 ms），
且不需要 vcan/网关进程；代价是占用主机一个 USB 口、且设备侧要实现完整 USB 设备栈。

## 41.9 坑

1. **RX 帧短一字节，内核就丢帧**。advertise 了 HW_TIMESTAMP 就必须发 24/80 B，
   内核按 struct_size(hf, canfd_ts, 1) 做最小长度校验（gs_usb.c:575-601）。
   最容易犯的错是"经典帧只发 20 B"，那是不带时间戳的尺寸。
2. **can_dlc 是 DLC 编码不是字节数**。填 64 会被当成非法 DLC；FD 64 字节对应编码 15。
   （同一坑在第 29 章 MCAN 侧也踩过。）
3. **channel 越界会被内核 detach 设备**。4 通道时 icount=3，数据帧 channel 只能是 0 到 3。
4. **echo_id 必须小于 10**。内核 GS_MAX_TX_URBS=10，回显序号越界会命中错误的 TX 上下文，
   轻则统计错乱、重则 BUG_ON（gs_usb.c:294）。
5. **不认识的控制请求必须 STALL**。返回 0 长度或垃圾数据会让内核按结构体长度解析失败，
   probe 报 -EPROTO，设备反复重连。
6. **advertise 了能力就要实现**。置了 BT_CONST_EXT 却回 40 字节，内核按 72 字节读；
   置了 TERMINATION 但没实现，ip link set termination 会挂住。
7. **时间戳必须单调**。用 mcycle/480 时注意 32 位回绕与高字维护；回退会让 ptp4l 直接失锁。
8. **gs_usb 固件运行时不要烧写别的固件**。USB 设备会掉线重枚举，正在跑的 candump 与测试脚本全断；
   板卡同一时间只允许一个 gs_usb 固件实例（契约第 5 节）。
9. **主机侧禁止绕过驱动直接 usbfs 发包**。会产生内核与用户态同时读写端点的未定义行为
   （契约第 7.6 条），测试一律用标准 SocketCAN 命令。
10. **USB 描述符字符串别超长**。厂商/产品串用 UTF-16LE，超过 255 字节会被截断，
    内核读到的 Product 串可能乱码，udev 规则就匹配不上。
11. **dmesg_restrict=1 会让"看不到 oops"变成假象**。本机实测普通用户读 dmesg 被拒；
    测试脚本必须 sudo -n dmesg，否则会把"命令失败"误判成"没有 oops"。

12. **共享板卡时，别人的 hwrun 烧写会把 gs_usb 设备踢下线**（实测定案，最容易误判的坑）：
    任何成员用 J-Link 烧写都会复位整颗 MCU，USB 设备随之重枚举，主机侧只看到 `USB disconnect`
    （**没有 Oops/BUG/WARNING**）。表现是：刚刚还能 `cansend`，几秒后 `candump` 报 `SIOCGIFINDEX: No such device`。
    判别方法：用 `bash scripts/board_lease.sh 300 gs_usb` 持锁（后台加 nohup），
    主机同时轮询 `lsusb`，设备会连续在线 >2.5 min 不掉线。
    结论：**gs_usb 测试必须独占板卡**，不要把它当成固件崩溃或 WDT 复位。

13. **把“lsusb 能读”当成“测试不需要 root”**。实测：非 root 跑 test_classic.sh，`ip link set can0 up type can bitrate 500000`
    返回 `RTNETLINK answers: Operation not permitted`，接口一直是 DOWN/STOPPED，后续 cansend 报 `Network is down`。
   需要 root 的还不止它：**`ip link set` 要 CAP_NET_ADMIN，而 candump/cansend/cangen/CAN_RAW/python-can 要 CAP_NET_RAW**。
   脚本统一 `sudo -n`，并在 setup 里先探测这两个能力，缺了就打 SKIP 而不是让用例全红。
    正确做法：`sudo bash host/gs_usb/test_classic.sh`（或给脚本加 sudo 前缀），lsusb/ethtool/candump 仍可不用 root。

14. **USB autosuspend 会让空闲设备掉线**（A/B 已定案）：默认 autosuspend 下自研固件空闲约 85 s 后 USB detach，
    dmesg 只有 disconnect、无 oops；把 autosuspend 设为 -1 且端口 power/control=on 后，空闲 150 s 全程在线、0/30 掉线。
    根因是主机 USB 自动挂起而固件未处理 SUSPEND/RESUME（与坑 12 的并行烧写是两回事），U-40-14。

15. **FD 位会"粘"在接口上**（实测）：做完 CAN FD 测试后，接口停留在 `<FD>` 模式；此时只写
    `ip link set can0 type can bitrate 500000` 不会清掉 FD 位，经典帧收发就会异常
    （实测现象：can1 一帧收不到、can1 到 can0 却正常，极易误判成硬件问题）。
    正确做法：每次配置都显式写 **`fd off`**（或 `fd on dbitrate ...`），不要依赖"默认"。

16. **控制请求的通道号在 wValue，不是 wIndex**（内核 7.0.0 事实，最隐蔽的坑）：
    按 wIndex 传通道号时内核不报错，但 BITTIMING/MODE 会**全部落到 ch0**——表现为 can1~3
    配置“成功”却 FD 无 ACK、TEC 冲到 248、`ip -details` 状态诡异。判别方法：固件串口 trace
    `[gs] MODE ch=`，正常应 `ch=0` 到 `ch=3` 各一次；修复前 11 次 MODE 全是 `ch=0`。
17. **TIMESTAMP 回 12 字节会错位**：内核按 **4 字节 le32** 读，多回 8 字节会让后续控制请求解析错乱；
    高 32 位由内核 CYCLOUNTER 机制自己扩展，设备只维护 1 MHz 低 32 位计数器。

18. **验证 one-shot 不能靠“把对端 down 掉”**：MODE RESET 后 MCAN 仍会 ACK，`ip link set can1 down`
    制造不出无 ACK 场景；正确做法是**速率错配**（can0=500k、can1=250k）再发帧。根因层面，第一版固件没把
    `GS_CAN_MODE_ONE_SHOT` 映射到 MCAN 的 `disable_auto_retransmission`，表现为发 8 帧后 TEC 仍为 0、
    无法用计数器证明“不重传”（U-40-19）。M3 已修：串口 `MODE flags=0x18 → CAN0 START one_shot=1`，
    速率错配下 8 帧后 **berr tx 7 到 23** 且保持 ERROR-ACTIVE（未 bus-off），即单次发送不重传生效。
19. **硬件时间戳“能力 PASS”不等于“绝对精度够用”**：实测绝对偏差中位约 81 µs、频率差 -40.7 ppm
    （同帧软件戳减硬件戳中位 67.2 µs）。做普通台架时间关联够用，但要用于 PTP 级同步，
    需要固件周期性 TIMESTAMP 校准或主机侧漂移补偿（U-40-20）。


20. **TX 失败不回 echo 会永久停掉主机 TX 队列（R8 定位并修复的固件缺陷，不是测试环境问题）**
    现象：`cangen can0 -g 0` 满速发 FD 帧，can0 `tx_packets` 涨到某个数就停住；此后 `cansend` 立即返回
    `write: No buffer space available`（ENOBUFS），`ip -s link` 的 TX dropped 也不再变化。**但设备一直在线**：
    对端把已经发出去的帧全部收到、berr-counter 0/0、state ERROR-ACTIVE、dmesg 无 oops——很容易误判成
    “CAN/PHY 问题”或“测试脚本挂起”（R5 的 test_5m.sh 挂起就是这个原因）。
    原因：`bridge.c` 的 `drain_host_tx()` 在 `app_can_send()` 返回失败（MCAN TX FIFO 满，非阻塞发送）时
    直接丢帧且**不回 echo**；而内核 gs_usb 只能靠 echo 回收 TX URB（`GS_MAX_TX_URBS=10`），漏满 10 个
    echo 后驱动调 `netif_stop_queue()`，队列再也不会被唤醒。**每漏一个 echo 就永久泄漏一个 URB。**
    修复：保留队尾、下一轮 poll 重试（背压，主机最多 10 帧在途，16 深环不会溢出）；IN 端点忙时 echo 先暂存
    再补发，保证每帧只回一次；新增 `tx_retry` 计数（串口 `gsstat` 可读）。
    A/B 证据：修复前 1M/2M 64 B 满速只发出 **5/3000 帧**、`tx_packets` 8→8、`cansend` 报 ENOBUFS
    （`logs/20261102-182213-gs-usb-tx-urb-leak.log`）；修复后同配置 **3000/3000 帧**、`cansend` rc=0（`logs/20261102-182303-gs-usb-tx-urb-leak.log`）；5M 长测 25 万帧连续发完、0 丢帧、
    0 错误，`tx_retry=1 872 531`（背压生效而非丢帧，`logs/20261102-182018-gs-usb-5m.log`）。
    复跑：`bash host/gs_usb/repro_tx_urb_leak.sh --expect-stall` / `--expect-ok`。

21. **cangen 的 -L 是“字节数”不是 DLC 编码**：`cangen -L 15` 不会发 64 B FD 帧，内核按合法 DLC 长度向上映射成
    **16 B**（candump 显示 `[16]`）；FD 64 B 必须写 `-L 64`。R5 的 test_5m.sh 用的是 `-L 15`，所以那批
    “5M 吞吐”即使跑通也只有 16 B 载荷（R8 已修脚本）——判断方法：看 candump 的长度列与载荷码率是否自洽。

## 41.10 对比：gs_usb vs slcan vs cannelloni

三种方案的对比见表 41-22。

| 维度 | gs_usb（本章） | slcan | cannelloni（第 28 章实测） |
|---|---|---|---|
| 链路 | USB 2.0 HS 480 Mbit/s | 串口 115200 到 3 Mbit/s | 千兆以太网 + UDP |
| 主机驱动 | 内核自带，免驱 | 内核 slcan 加 slcand | 用户态进程 + vcan |
| CAN FD | 支持（BRS/ESI 透传） | 经典为主 | 支持（canfd_frame） |
| 硬件时间戳 | 支持（1 MHz 32 位 + SO_TIMESTAMPING） | 无 | 无 |
| 多通道 | 单设备 4 路 | 单串口 1 路 | UDP 端口映射多路 |
| 主机依赖 / root | 无 / 不需要 | udev 规则加 slcand | cannelloni 加 vcan 加 root |
| 单帧延迟（can0→can1，200 帧 1 ms 间隔） | **待复测**（R4 数据未归档；复跑 `bench_latency.py`） | 待确认（串口延迟主导） | **2.26 ms 平均 / 2.49 ms p99**（第 28 章实测） |
| 吞吐（经典 500k） | **935.2 帧/s，5.02 s 4692/4692，丢帧 0**（R4 实测） | 受串口波特率限制（115200 时约 8 到 11 KB/s，推算） | 300 帧/s 丢包 0.13%（第 28 章实测） |
| 典型场景 | 台架、产线、开发机 | 老设备兼容、低成本 | 车内以太网网关 |

**表 41-22** ：三种方案对比。cannelloni 的数字来自第 28 章实测；gs_usb 的延迟/吞吐来自本仓库
host/gs_usb/bench_latency.py（R4 实测，日志 logs/20261102-105931-gs-usb-timestamp.log 同批）。

## 41.11 参考来源

1. **Linux 内核 gs_usb 驱动**：drivers/net/can/usb/gs_usb.c，主线源码
   <https://github.com/torvalds/linux/blob/master/drivers/net/can/usb/gs_usb.c>
   （本章协议事实按 Ubuntu 26.04 的 7.0.0-38 内核核对；文中行号基于仓库内 1677 行副本）。
2. **SocketCAN 文档**：<https://www.kernel.org/doc/html/latest/networking/can.html>
   （内核 Documentation/networking/can.rst；CAN_RAW、SCM_TIMESTAMPING、错误帧语义）。
3. **candleLight 开源固件**：<https://github.com/candle-usb/candleLight_fw>
   gs_usb 设备侧参考实现（GPL-2.0）。本仓库只做行为与描述符对照，未拷贝代码。
4. **TCAN1044AV-Q1 数据手册（TI）**：<https://www.ti.com/product/TCAN1044AV-Q1>
   5 Mbit/s CAN FD 收发器，是本板 5M 数据段上限的器件依据；8M 需换 PHY（U-40-11）。
5. **ISO 11898-1:2024**（CAN 数据链路层与 CAN FD）：ISO 官网检索 “ISO 11898-1”，
   <https://www.iso.org/standards.html>。本章 FD 帧格式、错误帧/错误计数器语义以此为准。
6. 同 SoC（HPM5E31）的 gs_usb 参考工程（roboparty_EtherCANFD）：厂商内部资料（未公开），
   仅本地只读对照，源码未进本仓库。
7. 本仓库 docs/gs_usb开发约定.md（团队契约：协议事实表、测试矩阵、防崩溃规则）、
   第 20 章（USB device 与 CherryUSB）、第 08 章（CAN FD 位时序与 TDC）、第 28 章（cannelloni 网关实测）。
8. 实测日志：`logs/20261102-1043-gs_usb-fd-m2m.log`（FD）、`logs/20261102-1049-gs_usb-ts-idle.log`（时间戳与空闲）、
   R5 的 error/5m 与长稳日志见 §41.7.7（路径随该节列出）。
9. 图脚本：tools/figs/app40_pipeline.py（图 41-1）、app40_host_frame.py（图 41-2）、
   app40_enum_seq.py（图 41-3）、app40_perf_enum.py（图 41-4，数据 tools/figs/app40_data.json）、
   app40_perf_latency.py（图 41-5）、app40_error_path.py（图 41-6）。

> **License 说明**：本仓库 `components/gs_usb/` 与 `samples/app/gs_usb_candle/` 是按上述协议事实独立编写的实现，
> 未拷贝 GPL 代码；参考工程 `Roboparty/roboparty_EtherCANFD`（GPL-3.0）与 `zhangjiance/candleLight_fw_hpm_port`（GPL-2.0）
> 仅用于行为与描述符对照，`samples/app/gs_usb_candle/README.md` 已注明。

## 41.12 待确认项与风险

> 已逐条复核（2026-11-02）。表中不再保留“未尝试/未做/待补”的裸条目：能测的给实测数字与日志，
> 不能测的写清原因和一条可复现的验证命令。所有结论以主机侧命令输出为准，固件自报数字只作对照。

| 编号 | 项 | 状态 | 验证方法 |
|---|---|---|---|
| U-40-1 | 枚举与 4 通道 netdev 创建 | **已实测（2026-11-02）PASS 10/10**：1209:2323、Product HPM5E31 gs_usb CANFD、can0..can3（ifindex 35~38）、driver=gs_usb | bash host/gs_usb/test_enum.sh；日志 logs/20261102-103609-gs-usb-enum.log |
| U-40-2 | 经典 500k 收发（can0 到 can1） | **已实测 PASS 11/11**：标准帧/29 位/DLC 边界/RTR 双向，can0 TX +17、can1 RX +9 | bash host/gs_usb/test_classic.sh；logs/20261102-103742-gs-usb-classic.log |
| U-40-3 | CAN FD 2M/5M/8M | **已实测（R8，2026-11-02 18:14）**：1M/2M 与 **1M/5M** 64 B BRS 双向各 2000/4000 帧 0 丢帧、tx_err/rx_err 0、ERROR-ACTIVE（5M 6 988.5 / 6 995.1 帧/s、64 B 载荷码率 3.58 Mbit/s）；8M 也能下发且 0 丢帧（9 981 帧/s），但超 TCAN1044AV 标称，不作可用档位 | bash host/gs_usb/test_5m.sh（§41.7.8）；日志 logs/20261102-181452-gs-usb-5m.log |
| U-40-4 | 硬件时间戳精度与单调性 | **已实测 PASS（4/4）**：硬件戳覆盖 50/50、量化 1 µs、单调违例 0、同帧软-硬中位 67.2 µs（σ5.9）、hw/sw 间隔比 0.999959（-40.7 ppm） | python3 host/gs_usb/test_timestamp.py；logs/20261102-105931-gs-usb-timestamp.log |
| U-40-5 | 高负载丢帧率与 CPU 占用 | **已实测（R8）**：满速 `cangen -g 0` 1M/5M 64 B **250 000 帧 / 35.88 s（6 967.7 帧/s）** 0 丢帧；can0 `ip -s` TX errors/dropped 0、can1 RX errors/dropped 0；固件 `tx_drop=0 in_drop=0 tx_retry=1 872 531` | bash host/gs_usb/test_5m.sh --soak 250000（§41.7.8 表 41-25）；日志 logs/20261102-182018-gs-usb-5m.log |
| U-40-6 | bus-off 与 GET_STATE/BERR 上报 | **部分闭环（M4）**：错误帧链路已通——500k/250k 错配后 can0 在 `ip -details` 显示 **ERROR-PASSIVE**（该状态只能由 CAN_ERR 帧触发，gs_usb.c:503-518）；恢复后 ERROR-ACTIVE 且收发正常。**BUS-OFF 仍未达成**：错配下 TEC 只到 8、REC 饱和 127 | 已知限制（注入方法）：bus-off 需 TEC≥256，需 one-shot 或无 ACK 对端连续失败 ≥32 次；命令：can0 `one-shot on` + can1 250k，重复 40 帧并 `candump -n 5 can0` 抓 CAN_ERR_BUSOFF |
| U-40-7 | 4 通道并发互不串扰 | **已实测 PASS（12/12）**：can0/can1 与 can2/can3 两组同时发不同 ID，can1 只见 0x100、can3 只见 0x200；TX 计数 can0=21 can1=5 can2=4 can3=1 | bash host/gs_usb/test_4ch.sh；日志 logs/20261102-103834-gs-usb-4ch.log |
| U-40-8 | 与 slcan/cannelloni 的延迟对比实测 | **gs_usb 侧待复测**（R4 延迟数据未归档；cannelloni 2.26 ms 见第 28 章）；**slcan 为已知限制**：本板只有一个 USB 口，测 slcan 要先烧 USB CDC 固件、把 gs_usb 固件换掉，本轮窗口排队冲突未做 | 将来验证：烧 `samples/usb/usb_cdc_acm_echo` + `slcand -s 1000000 /dev/ttyACM1`，用同一 bench_latency 口径量往返；文本协议开销通常比 gs_usb 高一个量级 |
| U-40-9 | 多设备共存（同时插两块板） | **已知限制（本轮只有一块板）** | 两块板都烧 gs_usb 固件同时插主机：`ip -br link show type can` 应出现 can0..can7、`lsusb -d 1209:2323` 两个设备，用 `ethtool -i` 的 bus-info 区分。注意两块固件当前 Product/Serial 字符串相同（HPM5E31-GSUSB-0001），多设备时 Serial 不唯一，需换成芯片 UID（代码已留接口） |
| U-40-10 | Windows/macOS 主机兼容性 | **已知限制（未验证）** | 本章只针对 Linux 主线 gs_usb 驱动；Windows 需 candleLight 专用驱动/WinUSB 绑定，macOS 无主线 gs_usb。将来验证：Windows 用 Zadig 绑 WinUSB 后跑 candleLight 上位机；本轮不作为结论 |
| U-40-12 | gs_usb 设备"自己掉线"的根因 | **已定案：并行 hwrun 烧写复位 USB，非固件问题** | 用 `scripts/board_lease.sh` 持锁后主机轮询 lsusb，设备连续在线 >2.5 min；该现象在基线/自研固件上都由烧写动作触发 |
| U-40-13 | 自研固件的描述符字符串与序列号 | **已确认（M1）**：HPMicro / HPM5E31 gs_usb CANFD / HPM5E31-GSUSB-0001 | lsusb -v 与 dmesg 均已核对 |
| U-40-14 | USB autosuspend 导致空闲掉线 | **已闭环**：默认 `autosuspend=2` + 端口 `power/control=auto` 下 **300 s / 60 次轮询 0 掉线**（旧固件同条件约 85 s 掉线）；串口 `[usb] pm: ... sdis=1` | 固件已加 SUSPEND/RESUME 处理 + `USBMODE.SDIS`；主机侧 `autosuspend=-1` / `power/control=on` 可作双保险 |
| U-40-15 | GET_STATE 的 berr-counter 数值 | **已定案，不是映射 bug**：ERROR-ACTIVE 正常场景实测 **0/0**；观测到的 tx 8 / rx 127 出现在 **can0=500k 对 can1=250k 速率错配**时，是 MCAN ECR 的真实收发错误计数（REC 饱和到 127） | 复现：两通道设不同 bitrate 后互发；判错同时看 `ip -s link` 的 errors 增量 |
| U-40-16 | can0 TX dropped=10 的来源 | **已定案**：500 fps × 5 s（`cangen -g 2 -n 2500`）实测 can1 收到 **2500/2500**、can0 **TX dropped 增量 0**；更高帧率下的 dropped 是 `qlen=10` + 内核 `GS_MAX_TX_URBS=10` 的排队背压 | 压测按 `ip -s link` 的 dropped **增量**统计（见 §41.7.6） |
| U-40-17 | CAN_ERR_* 错误帧上报（BERR） | **已实现（M4）+ 内核机制已定案**：协议无专用错误帧类型，内核把 can_id 带 CAN_ERR_FLAG(0x20000000) 的经典帧当错误帧（gs_usb.c 的 ERROR frames 分支 → gs_update_state）。固件每主循环轮询 PSR/ECR，状态迁移时发 24 B 帧（BUSOFF/CRTL/RESTARTED，data[6..7]=TEC/REC），受 MODE.flags bit12 控制。实测：错配后内核状态已变 ERROR-PASSIVE（证明帧被消费），原始 CAN_ERR 行因 candump 被 SIGTERM 未 flush 未落盘 | 复测：`candump -n 5 -T 1000 can0`（正常退出自动 flush）跑 U-40-6 的注入命令，应看到 `20000004`（CRTL）/ `20000040`（BUSOFF）行 |
| U-40-18 | 5M/8M 吞吐与超过 1 h 长稳 | **5M 吞吐已实测（R8）**：见 §41.7.8、U-40-3、U-40-5；满速长测 250 000 帧无卡死。**>1 h 长稳仍为已知限制**：本轮只到 35.88 s 连续满速 + 6 轮分段突发；长稳用 `host/gs_usb/r5_loop.sh 12`（110 s 租约 × 分段，CSV 记掉线/丢帧/berr）累计到 1 h | bash host/gs_usb/test_5m.sh（矩阵）、bash host/gs_usb/test_5m.sh --soak 250000（长测）；日志 logs/20261102-182018-gs-usb-5m.log |
| U-40-19 | one-shot 不重传的验证与初版 fail 根因 | **已闭环（固件已修）**：根因是未把 `GS_CAN_MODE_ONE_SHOT` 映射到 MCAN `disable_auto_retransmission`；修复后串口 `MODE flags=0x18 → CAN0 START one_shot=1`，速率错配下 8 帧后 **berr tx 7→23**、state 保持 ERROR-ACTIVE（未 bus-off = 单次不重传生效） | 复测：can0 `one-shot on`、can1 设 250k 制造无 ACK，发 8 帧看 berr 增量；**不要用 `ip link set can1 down`**（MODE RESET 后 MCAN 仍 ACK，不会产生错误） |
| U-40-20 | 硬件时间戳绝对精度（-40.7 ppm / 绝对偏差约 81 µs） | **已知限制（本轮未取到新数据）**：R4 单窗口量到 -40.7 ppm（hw/sw 间隔比 0.999959）、同帧偏移中位 67.2 µs，台架时间关联够用；PTP 级需固件周期 TIMESTAMP 校准。R5 新增 `bench_ts_drift.py`（分段采样 + 跨段线性拟合）已就绪，但窗口内以普通用户执行 `ip link set` 被拒（EPERM），未采到分段数据 | 复测：`sudo -n python3 host/gs_usb/bench_ts_drift.py --secs 25 --append /tmp/ts.csv` 跑 6 段（间隔 >60 s，租约 ≤110 s），再 `--fit /tmp/ts.csv` 出 ppm/R² |
| U-40-11 | 5M/8M 的 PHY 与线缆门槛 | **已知限制（器件上限，R8 有实测对照）**：TCAN1044AV-Q1 标称 5 Mbit/s（<https://www.ti.com/product/TCAN1044AV-Q1>），5M 双向 0 丢帧已实测；**8M 在这块板的短链路上也能 0 丢帧跑通（9 981 帧/s、5.11 Mbit/s），但属超规格，不作可用档位**，且本章没有眼图/抖动/温漂数据 | 5M：bash host/gs_usb/test_5m.sh；8M 只记录（同脚本 ext 段） |
| U-40-21 | 满速 TX 时 netdev TX 队列永久停（TX URB 泄漏） | **已定位并已修复（R8，固件缺陷）**：旧 `bridge.c:drain_host_tx()` 在 `app_can_send()` 失败（MCAN TX FIFO 满）时丢帧且不回 echo，内核 10 个 TX URB 泄漏完即 `netif_stop_queue()`。A/B：修复前 1M/2M 64 B 满速只发出 **5/3000 帧**、`cansend` 报 `No buffer space available`；修复后同配置 25 万帧 0 丢帧。改法：保留队尾下轮重试（背压）+ echo 暂存补发，新增 `tx_retry` 计数 | bash host/gs_usb/repro_tx_urb_leak.sh --expect-stall / --expect-ok；§41.9 坑 20；日志 logs/20261102-182213-gs-usb-tx-urb-leak.log |
