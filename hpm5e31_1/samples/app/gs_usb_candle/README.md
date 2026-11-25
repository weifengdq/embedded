# samples/app/gs_usb_candle —— gs_usb / candleLight 兼容 4 路 CAN FD USB 适配器

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

> 目标：hpm5e31_1 插到 Linux（内核 **7.0.0-38 generic**，`gs_usb.ko` 自动加载）后直接出现
> `can0~can3`，SocketCAN 工具（candump/cansend/cangen/python-can）无需厂商驱动即可用。
> 协议事实以本机内核 `drivers/net/can/usb/gs_usb.c` 为准（见 `docs/gs_usb开发约定.md`）。
> 第 41 章（`docs/chapters/41-gs-usb-candlelight.md`）为技术说明与实测。

## 1. 硬件

| 资源 | 连接 | 说明 |
|---|---|---|
| USB0 | Pin67(USB_N)/Pin68(USB_P) → 主机 | **High-Speed 512B 端点**；设备口，不是调试串口 |
| 调试串口 | CH343 → /dev/ttyACM0 | 921600 8N1，letter-shell 控制台 |
| CAN0 | PD00/PD01（TCAN1044AV） | 与 CAN1 板内同一总线（双方 120Ω） |
| CAN1 | PD05/PD04 | 与 CAN0 互连 |
| CAN2 | PC08/**PA09** | 与 CAN3 互连（注意 CAN2 RXD 是 PA09，不是 PC09） |
| CAN3 | PC15/PC14 | 与 CAN2 互连 |
| 终端电阻 | 手动拨码 | **软件不可控**，收到 SET/GET_TERMINATION 一律 STALL |

## 2. USB 协议实现要点（与内核 7.0.0 对齐）

| 项 | 值 / 说明 |
|---|---|
| VID:PID | `0x1209:0x2323`（内核 gs_usb id_table 直接匹配） |
| 接口/端点 | 接口 0：Vendor Specific(0xFF)，1 个 Bulk IN `0x81` + 1 个 Bulk OUT `0x02`，HS 512B |
| DEVICE_CONFIG | `icount = 3` → 内核创建 **4** 个 netdev |
| BT_CONST.feature | **13595** = LISTEN_ONLY|LOOP_BACK|ONE_SHOT|HW_TIMESTAMP|FD|BT_CONST_EXT|BERR_REPORTING|GET_STATE；不 advertise TRIPLE_SAMPLE / TERMINATION / PAD_PKTS |
| BT_CONST | `fclk_can=80 MHz`，tseg1 1..256，tseg2 1..128，sjw 1..128，brp 1..512 |
| BT_CONST_EXT | 数据段：dtseg1 1..32，dtseg2 1..16，dsjw 1..16，dbrp 1..256 |
| RX 帧长度 | 经典 **24 B** = 12 B 头 + 8 B 数据 + **4 B 1 MHz 时间戳**；FD **80 B** = 12 + 64 + 4。**必须发满**，短帧内核记 `rx_length_errors` |
| TX 帧长度 | 经典 20 B（12+8）、FD 76 B（12+64）；**echo 帧与 RX 同尺寸（24/80，带时间戳）** |
| TIMESTAMP 请求 | **只回 4 字节 le32**（1 MHz 计数低 32 位）；高 32 位由内核 CYCLECOUNTER 机制扩展（不可回 12 字节结构） |
| 控制请求通道号 | **放在 wValue**（不是 wIndex！）；HOST_FORMAT/DEVICE_CONFIG 用 wValue=1、wIndex=接口号 |
| 时间戳来源 | `mcycle(480 MHz) / 480 = 1 MHz`，RX 帧在 MCAN FIFO 读出时刻打戳 |
| TDC | 内核此版本不传 TDC 字段，固件按数据段 prescaler 内部计算 `ssp=(1+dtseg1)*dbrp` |

## 3. 目录与构建

```
samples/app/gs_usb_candle/
├── CMakeLists.txt
├── src/main.c      # 初始化 + 主循环 + shell（usbinfo/gsstat/ctrllog）
├── src/usb_gs.c    # CherryUSB vendor class：描述符、控制请求、EP IN/OUT
├── src/usb_gs.h
├── src/bridge.c    # gs_usb 帧 ↔ 4x MCAN 桥接（RX/TX/echo/时间戳/状态）
├── src/bridge.h
└── README.md
components/gs_usb/  # 纯软件协议层（结构体/常量/编解码/位时序换算），可单测
host/gs_usb/        # 上位机测试脚本（tp-gs-host 维护）
```

```bash
cd hpm5e31_1
bash scripts/build.sh samples/app/gs_usb_candle
bash scripts/hwrun.sh samples/app/gs_usb_candle --secs 30 --send "usbinfo;gsstat" --lock-wait 600
```

串口应看到：

```
=== gs_usb_candle: gs_usb/candleLight 4x CAN FD (HPM5E31) ===
[gs] VID:PID=1209:2323 feature=0x351B(timestamp/FD/GET_STATE/BERR)
[gs] 4x MCAN initialized (default 500k classic)
[gs] usb device initialized, waiting for host enumeration...
[gs] usb=1 rx=0 tx=0 echo=0 ...
```

## 4. 主机侧使用（三步）

```bash
# 1) 确认枚举（USB 设备口插主机）
lsusb -d 1209:2323                       # 1209:2323 Generic bytewerk.org candleLight
dmesg | grep gs_usb                      # "Configuring for 4 interfaces"
ip -br link | grep can                    # can0..can3

# 2) 配 500k 经典（can0↔can1 板内相连）
sudo ip link set can0 type can bitrate 500000
sudo ip link set can1 type can bitrate 500000
sudo ip link set can0 up; sudo ip link set can1 up
cansend can0 123#1122334455667788
candump can1                              # can1  123   [8]  11 22 33 44 55 66 77 88

# 3) CAN FD 1M/2M 64B BRS
sudo ip link set can0 down; sudo ip link set can1 down
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set can1 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set can0 up; sudo ip link set can1 up
cansend can0 123##1$(printf '11%.0s' {1..64})   # FD + BRS + 64B
candump can1                                    # can1  123  [64] 11 11 ...
```

> 主机侧需要 `CAP_NET_ADMIN`（`ip link`）与 `CAP_NET_RAW`（`cansend/candump`）→ 用 `sudo -n`。
> **关闭 autosuspend**（可选，见 §6 易错点 1）：`echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend`；
> 端口级：`echo on | sudo tee /sys/bus/usb/devices/1-8/power/control`（设备号按实际 `lsusb -t`）。

## 5. 实测（2026-11-02，本板）

| # | 项目 | 命令/方法 | 实测 | 日志 |
|---|---|---|---|---|
| 1 | 枚举 | `lsusb` / `dmesg` | `1209:2323`、`Configuring for 4 interfaces`、can0~can3 | `logs/20261102-102357-gs_usb_candle.log` |
| 2 | 经典 500k 双向 | `cansend can0 123#..` → `candump can1` | 3 帧全对（123/18F/7DF）；反向 321/456 全对；RX/TX errors/dropped 0 | 同上 |
| 3 | CAN FD 1M/2M 64B BRS 单帧 | `cansend can0 123##1 + 64B` | `can1  123  [64]` 收到，BRS 生效，berr 0/0 | `logs/20261102-103503-gs_usb_candle.log`（wValue 修复前）；修复后见 `/tmp/m2b_fd.log` |
| 4 | FD 100 帧 | 连续 100 帧 FD 64B BRS | `cansend ok=100 fail=0`，can1 收到 **100/100**，设备不掉线 | 同上 |
| 5 | 每通道控制请求 | 串口 `[gs] MODE ch=..` trace | wValue 修复后 can0/can1 各自收到 MODE（ch 号正确） | 见 §6 易错点 2 |
| 6 | 硬件时间戳 | SO_TIMESTAMPING + SCM_TIMESTAMPING | 见共享验收日志（tp-gs-host `logs/20261102-*-gs-usb-*`） | — |
| 7 | 空闲稳定性 | 默认 autosuspend=2 / port power=auto，300 s / 60 次轮询 | **GONE_SAMPLES=0/60 PASS**（旧固件同条件 ~85 s 掉线）；串口 `[usb] pm: ... sdis=1` | `logs/20261102-110528-gs_usb_candle.log` |
| 8 | 压力（cangen 限速） | `cangen can0 -g 2 -n 2500`（500 fps × 5 s） | can1 收到 **2500/2500**；`ip -s` can0 **TX dropped 0→0** | 同上 |
| 9 | 4 通道并发 | can0+can2 各发 600 帧，can1+can3 收 | can1 **200/200**、can3 **200/200**，互不串扰 | 同上 |
| 10 | 模式 | loopback / one-shot / terminate | loopback 自发自收 2 帧 PASS；one-shot 串口确认 `MODE flags=0x18 → CAN0 START one_shot=1`；`ip link set can0 termination 120` 被 iproute2 直接拒绝（rc=255，驱动未 advertise TERMINATION）→ 符合"不支持" | 同上 |
| 11 | bus-off 与恢复 | can1 down（无 ACK）→ can0 连发 → can1 up + `restart-ms 100` | 恢复后 can1 正常收帧；BUS-OFF 状态数值与 one-shot TEC 增长**待确认（需 60 s 复测窗口，见 U-40-6）** | — |
| 12 | **CAN FD 1M/5M 64 B 双向** | `cangen -g 0 -n 4000 -L 64`（每方向） | can0→can1 与 can1→can0 各 **4000/4000** 帧，丢帧 0、tx_err/rx_err 0、ERROR-ACTIVE，**6 988.5 / 6 995.1 帧/s**（64 B 载荷 **3.58 Mbit/s**） | `logs/20261102-181452-gs-usb-5m.log` |
| 13 | **5M 满速长测** | `cangen -g 0 -n 250000 -L 64` | **250 000/250 000** 帧 / 35.88 s（**6 967.7 帧/s**），丢帧 0、tx_err/rx_err 0；固件 `tx_retry=1 872 531`（背压生效）、`tx_drop=0`、`in_drop=0` | `logs/20261102-182018-gs-usb-5m.log` |
| 14 | **FD DLC 0~64 扫描** | `cansend 123##1<hex>` 16 档双向 | 16/16 档收到长度与发出完全一致（0~8/12/16/20/24/32/48/64），BRS 生效 | 同第 12 行日志 |

> 本表只写**真实测过**的数字；未测项写"—”不编造。完整上位机矩阵见 `host/gs_usb/` 与 `docs/chapters/41-gs-usb-candlelight.md`。

## 6. 易错点（都是本轮实测遇到过的）

1. **主机 USB autosuspend 会把空闲设备 detach（最重要的易错点）**
   现象：设备空闲 ~85 s 后 `USB disconnect`，dmesg 无 oops，串口仍在打印 → 不是固件挂死。
   原因：Linux `usbcore` 默认 `autosuspend=2`（秒），主机挂起端口后，
   HPM USB0 固件若未正确处理 SUSPEND/RESUME（端点未重新 arm），host 判 detach。
   本固件已加 `USBD_EVENT_SUSPEND/RESUME` 计数 + RESUME 重新 arm，并设置 `USBMODE.SDIS`（可 `-DGS_USB_SET_SDIS=0` 做 A/B）。
   主机侧缓解（双保险）：`echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend`、端口 `power/control=on`。
2. **控制请求的通道号在 wValue，不在 wIndex**（内核 7.0.0 事实）
   现象：can0 一切正常，can1~can3 的 BITTIMING/MODE 全部生效在 ch0 → FD 时 can1 没进 FD、can0 发 FD 无 ACK（TEC 飙到 248）。
   依据：`usb_control_msg_send(..., GS_USB_BREQ_MODE, ..., dev->channel, 0, ...)`。
   本固件读 `setup->wValue`；串口 `[gs] MODE ch=` 可直接确认。
3. **TIMESTAMP 请求必须是 4 字节**：内核 `gs_usb_get_timestamp()` 读 `u32`；回 12 字节结构会控制传输长度不符。
4. **RX echo 帧也必须带时间戳**：advertise HW_TIMESTAMP 后，内核 `minimum_length` 对 echo 也是 24/80 B，短了会 `short read` + `rx_length_errors`。
5. **HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM**：必须 `ATTR_PLACE_AT(".ahb_sram") + mcan_set_msg_buf_attr()`（components/app_can 已处理，不要自己再 initialize）。
6. **模式位（FD/loopback/listen-only/one-shot）会"粘住"**：iproute2 的 `ip link set canX type can ...` 是"增量修改"，上一次设过的位会保留；换测试场景要**显式把每个位都写上**（如 `fd off loopback off listen-only off one-shot off`）。沿用旧位会让后续测试出现"TX 被禁/TEC 不增长"等假象。
7. **FD 位会"粘住"**：做完 FD 测试后接口仍是 `can <FD>`，别人只写 `bitrate 500000` 不清 FD 位 → 经典帧异常。切换回经典要显式 `fd off`。
7. **不要在多 agent 环境里裸跑**：hwrun 锁只覆盖"烧写+抓串口"，别人烧写会复位板子导致 USB 掉线（看起来像崩溃）。长测用 `scripts/board_lease.sh` 持租约。
8. **License 边界**：本实现按协议事实**独立编写**（内核驱动 + 契约文档），未拷贝 GPL 源码；
   参考实现 `Roboparty/roboparty_EtherCANFD`（GPL-3.0）与 `zhangjiance/candleLight_fw_hpm_port`（GPL-2.0）仅用于行为/描述符对照。本子目录不引入 GPL 代码。


9. **满速 TX 时丢帧不回 echo 会让主机 TX 队列永久停（R8 定位并修复的固件缺陷）**
   现象：`cangen -g 0` 满速发 FD 帧，`tx_packets` 涨到某个数就停住，`cansend` 立即报
   `write: No buffer space available`；但设备仍在线、对端收到了已发出的帧、berr 0/0、无 oops。
   原因：`bridge.c` 的 `drain_host_tx()` 在 `app_can_send()` 失败（MCAN TX FIFO 满）时直接丢帧、
   **不回 echo**；内核 gs_usb 靠 echo 回收 TX URB（`GS_MAX_TX_URBS=10`），漏满 10 个即停队列，且不可恢复。
   修复：保留队尾、下一轮 poll 重试（背压）+ IN 端点忙时 echo 暂存补发（保证每帧只回一次）+ `tx_retry` 计数。
   A/B：修复前 1M/2M 64 B 满速只发出 **5/3000 帧**、`tx_packets` 8→8、`cansend` 报 ENOBUFS；
   修复后 3000/3000 帧、`cansend` rc=0；5M 长测 25 万帧 0 丢帧。复现脚本 `host/gs_usb/repro_tx_urb_leak.sh`。

## 7. 待确认项

| # | 项 | 现状 | 验证方法 |
|---|---|---|---|
| U-40-1 | 5M/8M 数据段 | **已实测（R8，2026-11-02）**：1M/5M 64 B BRS 双向 0 丢帧、3.58 Mbit/s 载荷；25 万帧满速长测无卡死；8M 也能 0 丢帧跑通但超 TCAN1044AV 标称，只记录不作档位 | `bash host/gs_usb/test_5m.sh` 与 `bash host/gs_usb/test_5m.sh --soak 250000`；日志见 §5 第 12/13 行 |
| U-40-2 | 长稳（>1 h） | 未测 | board_lease 长租 + lsusb/cangen 轮询 |
| U-40-3 | SerialNumber 固定为 `HPM5E31-GSUSB-0001` | 未用芯片 UID | 读 OTP UUID 后替换 |
| U-40-4 | BERR 错误帧（bus-off/error-passive）上报 | GET_STATE 已实现（berr 0/0 实测）；CAN_ERR_* 帧未实现 | 拔线制造 bus-off，看内核是否收到 err 帧 |
| U-40-6 | one-shot 下 TEC 增长数值 / BUS-OFF 状态行 | 固件已映射 MCAN `disable_auto_retransmission`（串口 MODE flags=0x18 + START one_shot=1 已确认），但 TEC 数值复测窗口被其他成员烧写打断 | 用"显式清位配方"（fd/loopback/listen-only/one-shot 全写）后 60 s 复测：can1 down + can0 one-shot 发 8 帧，期望 berr tx≈64、state=ERROR-WARNING；bus-off 用 can0=500k/can1 不同速率触发 |
| U-40-5 | listen-only/loopback/one-shot 行为 | MODE flags 已透传到 MCAN，未逐项实测 | `ip link set can0 type can loopback on` 等 |