# gs_usb（candleLight）适配开发约定 —— hpm5e31_1

> 本文件是本轮（2026-11-02）gs_usb 固件 + 上位机 + 第 41 章的统一契约。
> 所有参与成员先读本文件，再读 `docs/应用层开发约定.md`（通用契约）。

## 1. 目标

把 hpm5e31_1 做成**标准 gs_usb / candleLight 兼容的 4 路 CAN FD USB 适配器**：
上电插到 Linux（当前内核 **7.0.0-38 generic**，`gs_usb.ko` 已加载）后直接出现 `can0~can3`，
SocketCAN 工具（candump/cansend/cangen/canplayer/python-can）可直接使用，无需厂商驱动。

**必须实现（能实现都实现）**：
- 4 通道（DEVICE_CONFIG.icount = 3 → 内核创建 4 个 netdev）；
- 经典 CAN 与 CAN FD（仲裁 1M、数据 2M/5M/8M 可配，BRS/ESI 透传）；
- **硬件时间戳**（1 MHz 32 位计数器 + 高 32 位，随帧回传；内核 `SO_TIMESTAMPING` 硬件时间戳可用）；
- 模式：LISTEN_ONLY、LOOP_BACK、ONE_SHOT、HW_TIMESTAMP、FD、BERR_REPORTING；
- 状态查询 GET_STATE（`ip -details link show` 能看到 bus-off/error-passive 等）；
- 错误帧上报（BERR）：bus-off、error warning/passive、RX overflow 用 CAN_ERR_* 帧上报；
- 位时序：BT_CONST + BT_CONST_EXT（数据段），TDC 由设备内部按数据段波特率计算（内核此版本**不支持 TDC 协议字段**，不要 advertise）。
- **不实现终端电阻控制**：板上是手动拨码开关，不 advertise GS_CAN_FEATURE_TERMINATION；
  若主机仍发 SET/GET_TERMINATION，返回 STALL（安全）。
- 不实现 TRIPLE_SAMPLE（MCAN 无三采样）、IDENTIFY/USER_ID 可选（有 LED 可做 IDENTIFY 闪烁）。

## 2. 参考实现（按优先级）

| 位置 | 说明 |
|---|---|
| `Roboparty/roboparty_EtherCANFD` | **同 SoC（HPM5E31）4 路 CAN FD gs_usb 固件**，CherryUSB + HPM SDK，最接近，作为主参考/起点 |
| `drivers/net/can/usb/gs_usb.c` | **本机内核 7.0.0 系列驱动源码**（从 torvalds master 拉取，与 N32 适配的 7.0.0 枚举一致），协议权威 |
| `D:\Nations.N32H7xx_Library.1.2.0\projects\N32_GS_USB` | N32H765 8 路 CAN FD 移植，已适配 7.0.0 内核（无/弱时间戳），协议与背压策略参考（本地 Windows 路径，成员可用 read 工具读） |
| `zhangjiance/candleLight_fw_hpm_port` | HPM 全系列 gs_usb 移植（HPM SDK + CherryUSB，multichannel 上游分支）；其 git 子模块未检出，需要时可用代理 `git submodule update --init --recursive` |
| `https://github.com/Roboparty/roboparty_EtherCANFD` | 已 clone 到 ref2，同上 |

## 3. 协议事实表（按内核驱动源码核对，务必遵守）

### 3.1 控制请求（bRequest；**通道号在 wValue**）

> 2026-11-02 由 tp-infra 实测 + 内核源码核对修正：**通道号放在 wValue、wIndex=0**；
> 只有 HOST_FORMAT / DEVICE_CONFIG 是 wValue=1、wIndex=interface 号。
> （早期按 wIndex 实现会导致 can1~can3 的 BITTIMING/MODE 全部打到 ch0。）

| 请求 | 编号 | 方向 | 数据 |
|---|---|---|---|
| HOST_FORMAT | 0 | OUT | `u32 byte_order = 0x0000beef`（小端）；回复成功即可 |
| BITTIMING | 1 | OUT | `gs_device_bittiming{prop_seg,phase_seg1,phase_seg2,sjw,brp}` 各 u32（小端） |
| MODE | 2 | OUT | `gs_device_mode{mode,flags}`；mode: 0=RESET/1=START；flags: bit0 LISTEN_ONLY, bit1 LOOP_BACK, bit3 ONE_SHOT, bit4 HW_TIMESTAMP, bit8 FD, bit12 BERR_REPORTING |
| BERR | 3 | OUT | 错误上报使能（内核按需）；可接受并忽略/记录 |
| BT_CONST | 4 | IN | `gs_device_bt_const{feature,fclk_can,tseg1_min/max,tseg2_min/max,sjw_max,brp_min/max/inc}` |
| DEVICE_CONFIG | 5 | IN | `{reserved1..3, icount, sw_version, hw_version}`；**icount = 通道数-1 = 3** |
| TIMESTAMP | 6 | IN | **单个 `le32`（4 字节）**：1 MHz 自由运行计数器的低 32 位（wValue=0/wIndex=0；内核每 1800s 读取做溢出补偿，不要返回 12 字节结构） |
| IDENTIFY | 7 | OUT | `{mode}` 0/1（LED 闪烁，可选） |
| GET_USER_ID / SET_USER_ID | 8 / 9 | IN/OUT | 可选（返回固定 ID 或 STALL） |
| DATA_BITTIMING | 10 | OUT | 同 BITTIMING，用于 FD 数据段 |
| BT_CONST_EXT | 11 | IN | BT_CONST + 数据段 dtseg1/2、dsjw、dbrp 范围 |
| SET_TERMINATION / GET_TERMINATION | 12 / 13 | OUT/IN | **不 advertise；收到则 STALL** |
| GET_STATE | 14 | IN | `{state, rxerr, txerr}`，state: 0=error-active 1=warning 2=passive 3=bus-off 4=stopped 5=sleeping |

### 3.2 特性位（BT_CONST.feature）

bit0 LISTEN_ONLY、bit1 LOOP_BACK、bit3 ONE_SHOT、bit4 HW_TIMESTAMP、bit7 PAD_PKTS（内核此版本未使用）、
bit8 FD、bit10 BT_CONST_EXT、bit11 TERMINATION、bit12 BERR_REPORTING、bit13 GET_STATE。
**本轮 advertise：0|1|3|4|8|10|12|13（十进制 1+2+8+16+256+1024+4096+8192 = 13595），另加 PAD_PKTS 可选。**

### 3.3 数据帧格式（小端，结构体 packed）

```c
struct gs_host_frame_header {  // 12 B
  u32 echo_id;   // 0xffffffff = RX 帧；否则为 TX 回显序号
  u32 can_id;    // 含 CAN_EFF_FLAG(0x80000000)/CAN_RTR_FLAG/CAN_ERR_FLAG
  u8  can_dlc;   // **DLC 编码值**（0..15），FD 时 9..15
  u8  channel;   // 0..3
  u8  flags;     // bit0 OVERFLOW, bit1 FD, bit2 BRS, bit3 ESI
  u8  reserved;
};
```

- **RX 帧（设备→主机）**：长度必须 ≥ 内核要求的 minimum：
  - FD 帧 + HW_TIMESTAMP：**12 + 64 + 4 = 80 B**（时间戳在 data[64] 之后）；
  - 经典帧 + HW_TIMESTAMP：**12 + 8 + 4 = 24 B**；
  - 时间戳 = 1 MHz 计数器的低 32 位（`timestamp_us`）。
  设备每帧发一个 USB IN 传输（内核每 URB 只解析一帧），512B 端点每次传输 ≤80B 也完全可行。
- **TX 帧（主机→设备）**：经典 20 B（12+8），FD 76 B（12+64）；按 MODE.flags 的 FD 位判断，且按 flags.FD 位兜底。
- **回显（echo）**：设备成功把 TX 帧交给 CAN 控制器后，把同一帧（echo_id 原样）按 TX 尺寸回给主机；
  echo_id 必须 < 10（GS_MAX_TX_URBS）。
- 通道号必须 < 4，否则内核会 detach 设备。
- **错误帧（BERR）也走这条路**：没有专用帧类型，设备把一帧经典 CAN 帧的 can_id 置
  CAN_ERR_FLAG(0x20000000) 并带 linux/can/error.h 类别位（0x04 CRTL / 0x40 BUSOFF /
  0x100 RESTARTED / 0x200 CNT），can_dlc=8，data 按错误类填写。长度同样必须满足
  HW_TIMESTAMP 的 minimum（经典 24 B）；只发 20 B 会被内核记 rx_length_errors。
  触发条件是主机 MODE.flags 的 BERR_REPORTING(bit12)，设备不需要错误中断——
  固件在主循环轮询 MCAN PSR（BO/EP/EW）与 ECR（TEC/REC），状态迁移才发一帧。
  内核据此更新 netdev 的 can.state（`ip -details` 的 STATE 行）与 can_stats，candump 可见。

### 3.4 时间戳

- 用 mcycle（480 MHz）/480 得到 1 MHz 32 位计数；TIMESTAMP 控制请求**只返回低 32 位（4 字节 le32）**，高 32 位由内核用 1800s 周期读取 + CYCLECOUNTER 机制自行扩展，固件不需要维护高 32 位。
- RX 帧在设备侧时间戳应尽量贴近"CAN 帧收完"时刻（MCAN RX FIFO 读出的时基），不要用发送 USB 的时刻。
- 内核用 TIMESTAMP 请求做 32 位溢出补偿（71 分钟一次），所以计数器必须单调、1 MHz、自由运行。

## 4. USB 配置

- VID/PID：**0x1209 / 0x2323**（candleLight，内核 gs_usb 驱动 id_table 支持）。
- 接口 0：Vendor Specific Class（0xFF），**1 个 Bulk IN（0x81）+ 1 个 Bulk OUT（0x02）**，wMaxPacketSize 512，High-Speed。
- 字符串：Manufacturer "HPMicro"、Product "HPM5E31 gs_usb CANFD"、SerialNumber 用芯片 UID（固定）。
- 用 SDK CherryUSB 1.6.1 的 `class/vendor` 模板 + 自定义描述符/回调；参考 `samples/usb/usb_cdc_acm_echo` 的 CMake 接法与 `Roboparty/roboparty_EtherCANFD/src/usb_config.h`。

## 5. 板级事实

- USB0（Pin67 USB_N / Pin68 USB_P）→ 主机；`board_init_usb(HPM_USB0)` 已在本仓库 board.c 实现（引脚 + 时钟），直接调用。
- CAN0 PD00/PD01、CAN1 PD05/PD04、CAN2 PC08/PA09、CAN3 PC15/PC14；**CAN0↔CAN1、CAN2↔CAN3 已两两相连 + 120Ω**，可做设备自测（主机发 can0 → 从 can1 收）。
- **终端电阻是手动拨码开关，软件不可控**（不实现，文档写清）。
- MCAN 消息 RAM 必须在 AHB SRAM 并 `mcan_set_msg_buf_attr()`（见 `components/app_can` 的易错点）。
- 调试串口仍是 CH343 `/dev/ttyACM0` 921600，烧写/抓串口流程不变；**gs_usb 固件运行时不要烧别的固件**（USB 设备会掉）。

## 6. 目录与作用域

```
samples/app/gs_usb_candle/        固件专用例程（CMakeLists + src + README）
components/gs_usb/                可复用协议层（gs_usb 帧编解码、控制请求、时间戳）
host/gs_usb/                      上位机测试脚本（setup/test_*/bench_*）+ README
docs/chapters/41-gs-usb-candlelight.md   第 41 章
docs/figures/41/ + tools/figs/app40_*    图（脚本生成）
```

## 7. 防内核崩溃规则（强制）

1. 所有 USB IN 帧长度严格按 3.3 节（80/24 字节），**不能短**；TX 解析按 20/76 字节边界与剩余长度判断，不越界。
2. `channel` 必须 < 4；`can_dlc` 必须合法（经典 0..8，FD 0..15 且映射到 0/1..64）；`echo_id` 回显必须 < 10。
3. 未知控制请求一律 STALL（返回 -1），不要返回垃圾数据。
4. 测试顺序：单通道 500k 经典帧 → 4 通道枚举 → FD 2M → 5M/8M → 时间戳 → 高负载。
   每步先 `dmesg -w` 另开窗口记录，出现 oops/WARN 立即停止并保存 `dmesg`。
5. 不在固件里做 USB OUT 端点的"边收边解析大缓冲"，先排队再解析（参考 roboparty 的 ring 设计）。
6. 主机侧只使用标准 SocketCAN 命令，禁止绕过驱动直接 usbfs 发包（避免未定义行为）。

## 8. 测试矩阵（至少 8 项，全部要有日志/命令输出）

| # | 项目 | 判定 |
|---|---|---|
| 1 | 枚举 | `dmesg` 见 4 个 can 设备；`ip -br link` 出现 can0..can3 |
| 2 | 经典帧收发 | can0 500k 发 0x123 → can1 收到（CAN0-CAN1 相连）；反向亦可 |
| 3 | 4 通道并发 | can0..can3 同时发不同 ID，互不串扰 |
| 4 | CAN FD | 1M/2M 64B、BRS=1；再测 5M/8M（如硬件/线缆允许） |
| 5 | 时间戳 | `ethtool -T can0` + SO_TIMESTAMPING 读到硬件时间戳，与系统时钟偏差统计 |
| 6 | 高负载 | cangen 高帧率 10s，统计 tx/rx 计数与 `ip -s link` 丢包 |
| 7 | 模式 | listen-only / loopback / one-shot 生效验证 |
| 8 | 状态与错误 | bus-off 注入（对端 listen-only，无 ACK）后：`ip -details` 出现 BUS-OFF、candump 见 CAN_ERR_BUSOFF/CRTL、重启后 ERROR-ACTIVE 且收到 CAN_ERR_RESTARTED、链路恢复收发 |
| 9 | 终端电阻 | `ip link set can0 termination 120` 返回不支持（EOPNOTSUPP） |
| 10 | 兼容性 | python-can 收发、candump/cansend/cangen/canplayer 正常 |
| 11 | 5M/8M 数据段 | 1M/5M 64B BRS 满速（cangen -g 0）丢帧 <1%、tx_errors 增量 0；1M/8M 只记录（TCAN1044AV 上限 5M，8M 需换 PHY） |
| 12 | 时间戳绝对漂移 | 多窗口采（硬件戳−软件戳）偏移，跨段线性拟合 ppm；单窗口比值仅作参考 |
| 13 | 分段长稳 | 租约窗口循环（r5_loop.sh）：设备在线、丢帧、berr、dmesg oops 全 0；记录累计时长 |

## 9. 协作与提交

- 上板一律 `bash scripts/hwrun.sh ...`（PID 自愈锁）；gs_usb 固件测试期间**独占板卡**，测试前在群里同步。
- 成员不 commit；Lead 统一提交推送。里程碑完成 send_message 给 lead（文件/实测数字/日志/风险）。
- 文档门槛同 `docs/写作规范.md`（第 41 章按外设章：≥250 行、≥8 H2、≥3 表、≥2 图、≥1 代码块、必备关键词、图号 40-x）。
