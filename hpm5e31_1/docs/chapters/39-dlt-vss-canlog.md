# 第 39 章 车载日志与信号：DLT / VSS / candump 记录回放

## 39.1 文档信息

| 项 | 值 |
|---|---|
| 章节 | 第 39 章 DLT / VSS / candump 记录回放 |
| 版本 | v1.0（hpm5e31_1 第二阶段） |
| 适用芯片 / 板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 对应组件/例程 | `samples/app/can_logger/`、`samples/app/vss_signals/`；上位机 `host/canlog/`、`host/vss/`、`host/dlt/` |
| 对应例程（全路径） | `samples/app/can_logger/`（记录/回放）、`samples/app/vss_signals/`（VSS 信号） |
| 参考实现 | COVESA [dlt-daemon](https://github.com/COVESA/dlt-daemon)（MPL-2.0）、[vehicle_signal_specification](https://github.com/COVESA/vehicle_signal_specification)、[can-utils](https://github.com/linux-can/can-utils) |
| 实测日期 | 2026-10-30 |
| 可信度 | can_logger 记录/回放/VSS 解码为**板内 + Bench B 实测**；DLT 为**板端编码器 + dlt-receive/dlt-viewer 端到端实测** |

## 39.2 本章导览

车端"日志与信号"通常分三件事，本章各给一个可复现实例：

1. **candump 记录/回放**：把总线原始帧按 can-utils 的 candump 文本/二进制格式记录，能回放、能统计、
   能定位"哪一帧丢了"——这是所有 CAN 协议调试的地基；
2. **VSS 信号映射**：把 CAN 帧里的位域翻译成 COVESA VSS（Vehicle Signal Specification）路径 +
   物理值 + 单位，让上层（仪表、云、ROS2）不用关心 CAN ID；
3. **DLT 日志**：把 ECU 内部日志按 COVESA DLT 协议（Standard Header + Extended Header + verbose
   载荷）发到主机，落成标准 `.dlt` 文件，用 dlt-viewer 分析。

前置章节：第 08 章（MCAN）、第 13 章（lwIP）、第 28 章（cannelloni 网关）。
后续可接：第 35 章（SOME/IP，服务化信号）、第 36 章（XRCE-DDS，ROS2 信号）。

## 39.3 概述

表 39-1 三种"日志/信号"通道的分工

| 通道 | 数据内容 | 线上格式 | 本仓库实现 | 典型消费者 |
|---|---|---|---|---|
| candump 记录 | 原始 CAN 帧（ID/DLC/数据/时间戳） | can-utils candump 文本、`candump -L` 二进制 | `samples/app/can_logger` + `host/canlog` | candump/canplayer、离线分析 |
| VSS 信号 | 语义化信号（车速/转速/水温/挡位/车门） | CAN 帧位域 + VSS 路径映射表 | `samples/app/vss_signals` + `host/vss` | 仪表、云平台、ROS2、数据闭环 |
| DLT 日志 | ECU 内部日志文本/参数（level/context） | DLT 协议（UDP 3490 或 Unix socket） | `host/dlt/`（接收/解析）；板端编码器方案见 §工作原理 | dlt-viewer、dlt-receive |

三者互补：candump 记"总线发生了什么"，VSS 定义"这些字节在业务上是什么"，DLT 记"ECU 内部为什么这么干"。

## 39.4 工作原理

### 39.4.1 candump 格式与环形缓冲

can-utils 的文本格式（`candump` 默认输出）：

```text
(1793000000.777817) can0 300#00000000A55AC33C
(1793000000.778115) can0 300#01000000A55AC33C
```

- 括号内 = `epoch 秒.微秒`；`can0` = 通道；`ID#DATA` = 十六进制；扩展帧是 8 位 ID（如 `18FEF100#...`）；
- 二进制格式（`candump -L` / `canplayer`）每帧是 `canfd_frame` 结构 + 内核时间戳，可直接被
  `canplayer` 回放。

板端用**环形缓冲**记录：容量 512 帧 × (4 B 时间戳 + 2 B ID + 1 B DLC + 1 B flags + 8 B 数据) = 16 KB，
写满后覆盖最旧帧并累加 `dropped`。时间戳用 CPU mcycle（1 µs 分辨率），导出时换算成
"固定 epoch 基准 + 相对 µs"，仅用于相对顺序/间隔分析。

### 39.4.2 VSS 信号模型与映射

![图 39-2 VSS 信号链路：CAN 帧 → VSS 路径 → 消费者](figures/39/fig-39-2-vss-chain.png)

VSS 把车辆信号组织成树（`Vehicle.Speed`、`Vehicle.Powertrain.CombustionEngine.Speed`、
`Vehicle.Cabin.Door.Row1.DriverSide.IsOpen` …），每个叶子有数据类型、单位、取值范围。
板端只负责"按约定的 ID/起始位/长度/因子/偏移"发 CAN 帧，映射表由上位机持有（本章用
`host/vss/vss_decode.py` 的 `MAP`，与固件发送端一一对应）。

表 39-2 本章 VSS 信号映射表（与 `host/vss/vss_decode.py` 和固件一致）

| CAN ID | 信号 | 起始位 | 长度 | 因子 | 偏移 | 单位 | VSS 路径 |
|---|---|---|---|---|---|---|---|
| 0x400 | VehicleSpeed | 0 | 16 | 0.01 | 0 | km/h | `Vehicle.Speed` |
| 0x400 | EngineSpeed | 16 | 16 | 1 | 0 | rpm | `Vehicle.Powertrain.CombustionEngine.Speed` |
| 0x401 | CoolantTemp | 0 | 8 | 1 | -40 | celsius | `Vehicle.Powertrain.CombustionEngine.ECT` |
| 0x401 | CurrentGear | 8 | 8 | 1 | 0 | none | `Vehicle.Powertrain.Transmission.CurrentGear` |
| 0x401 | DriverDoorOpen | 16 | 8 | 1 | 0 | none | `Vehicle.Cabin.Door.Row1.DriverSide.IsOpen` |
| 0x402 | BatterySOC | 0 | 16 | 0.1 | 0 | percent | `Vehicle.Powertrain.TractionBattery.StateOfCharge.Current` |

要点：

- **位编号**：DBC/VSS 工具链用"sawtooth"位编号（Intel 格式低位在前），本表所有信号 8 位对齐，
  等价于"字节偏移 + 位偏移"；非对齐信号需要按 sawtooth 逐位展开；
- **符号**：i8 表示的温度要先按二进制补码转负数，再加偏移（-40）——这是水温信号最常见的坑；
- **单位换算**：物理值 = raw × factor + offset，factor 决定分辨率（0.01 km/h、0.1 %）；
- **路径合法性**：用 VSS JSON 树校验叶子名存在（本章 `--vss-json`），避免"信号名拼错但数据看起来正常"。

### 39.4.3 DLT 协议结构

![图 39-1 DLT 报文结构（存储头 16 B + Standard Header 4 B + Standard-extra TMSP 4 B + Extended Header 10 B + verbose 载荷）](figures/39/fig-39-1-dlt-frame.png)

DLT（Diagnostic Log and Trace）报文（图 39-1）：

- **Standard Header（4 B）**：`HTYP`（1 B：UEH/WEID/WTMS/WTVS/WSID 标志）、
  `MCNT`（1 B，报文计数，用于检测丢包）、`LEN`（2 B 大端，含 header 的总长度）；
- **Standard Header extra（可选，位于扩展头之前）**：`WEID`→ECU ID(4 B)、`WSID`→session(4 B)、
  `WTMS`→`TMSP`(4 B，0.1 ms，小端)。**TMSP 属于标准头扩展，不在扩展头里**——本章第二版踩到的坑（见坑 9）；
- **Extended Header（HTYP.UEH=1 时存在，固定 10 B）**：`MSIN`（0x01=verbose）、`NOAR`（参数个数）、
  `APID`（4 字符应用 ID）、`CTID`（4 字符上下文 ID）；
- **文件存储头 DltStorageHeader（16 B）**：`pattern[4]="DLT\\x01"` + `seconds`(u32 LE) + `microseconds`(i32 LE)
  + `ECU ID[4]`（定长 4 字符，**没有长度字节**）；.dlt 文件里每条消息前都要带这个头，dlt-viewer 才能打开；
- **verbose 载荷**：参数个数 + 每个参数的 `(type, len, data)`，类型包括
  `STRING/BOOL/UINT8..64/INT8..64/FLOAT32/64/RAW` 等。

表 39-3 DLT 与本章板端方案的对照

| 项 | DLT 标准 | 本章板端实现（方案） |
|---|---|---|
| 传输层 | UDP 3490（或 dlt-daemon Unix socket） | UDP 3490（lwIP），与网关同网段 |
| 头开销 | Standard 4 B + WTMS extra 4 B + Extended 10 B（.dlt 文件另加 16 B 存储头） | 同左（4+4+10） |
| 参数类型 | 丰富（含字符串/浮点/数组） | 先支持 STRING/INT32/UINT32/FLOAT32 |
| 时间戳 | TMSP 0.1 ms 相对时间 | `bsp_uptime_ms()` × 10 |
| 落盘 | dlt-daemon 写 .dlt | `host/dlt/dlt_recv.py` 直接按 DLT 格式写 .dlt |
| 现状 | 主机侧接收/解析已提供 | **板端编码器已实现并实测（U-38-2/U-38-3）** |

## 39.5 寄存器与驱动 API 对照表

表 39-4 日志/信号功能 ↔ 外设/驱动 API ↔ 代码位置

| 功能语义 | 硬件/中间件 | 驱动 API | 代码位置 |
|---|---|---|---|
| CAN 帧收发 | MCAN0（PD00/PD01） | `app_can_init/send/recv` | `samples/app/can_logger/src/main.c` |
| 帧时间戳 | mcycle（480 MHz） | `hpm_csr_get_core_mcycle()` | `can_logger` `now_us()` |
| 记录缓冲 | DLM 128 KB | 静态数组 `s_ring[512]` | `LOG_CAP/s_ring` |
| 串口导出 | UART0（921600） | `printf` + letter-shell | `log dump` |
| 网关 | ENET0 RGMII + lwIP UDP | `app_lwip_init/poll`、`app_can_gw_init/poll` | `vss_signals` `gw_start()` |
| 网关帧格式 | UDP 4000 | cannelloni v2 | `components/app_can_gw` |
| 信号发送周期 | GPTMR/MCHTMR 时基 | `bsp_uptime_ms()` | `vss_send()` |
| VSS 路径校验 | 主机文件系统 | `json.load(VSS outputplus.json)` | `host/vss/vss_decode.py` |
| candump 解析 | 主机 SocketCAN | python-can / 文本解析 | `host/canlog/candump_parse.py` |
| DLT 接收 | 主机 UDP 3490 | socket | `host/dlt/dlt_recv.py` |

## 39.6 例程

表 39-5 本章例程一览

| 例程/工具 | 覆盖特性 | 关键配置 | 实测结论 | 日志 |
|---|---|---|---|---|
| `samples/app/can_logger/` | RAM 环形缓冲、candump 导出、0x300 回放、seq 顺序校验 | 512 帧、16 KB；gen 10 ms | 记录 30 帧 0 丢弃；回放 30/30；**verify bad_order=0 PASS**；导出 30 行 candump 被主机解析 **PASS** | `logs/20261030-175332-can_logger.log` |
| `samples/app/vss_signals/` | 3 帧 / 6 个 VSS 信号 / 100 ms 周期 / gw on | 0x400/0x401/0x402，500 kbit/s | vcan0 90 帧/3 s，6/6 信号解码，范围 & VSS 路径校验 **PASS** | `logs/20261030-175924-vss_signals.log` |
| `host/canlog/candump_parse.py` | candump 解析、帧率/负载/ID 直方图、seq 校验 | — | 30 帧、0 坏行、顺序 0 错误 | — |
| `host/vss/vss_decode.py` | VSS 路径 + 位域 + 符号 + 范围 | `--channel vcan0` 或 `--file` | 见上 | — |
| `host/dlt/dlt_recv.py` | DLT UDP 接收 → .dlt 落盘 | 端口 3490 | **已实测（U-38-3：567/600 包、0 解析错误）** | — |

### 39.6.1 can_logger：记录与回放

```c
/* 环形缓冲写入（samples/app/can_logger/src/main.c） */
static void ring_push(const mcan_rx_message_t *rx) {
    log_frame_t *f = &s_ring[s_head];
    f->ts_us = now_us();                 /* mcycle -> us */
    f->id = rx->std_id & 0x7FFU;
    f->dlc = (rx->dlc > 8U) ? 8U : rx->dlc;
    for (uint8_t i = 0; i < 8U; i++) f->data[i] = rx->data_8[i];
    s_head = (s_head + 1U) % LOG_CAP;
    if (s_count >= LOG_CAP) s_dropped++; else s_count++;
}
```

回放：`replay 30 300` 在 CAN1 上按 seq 0..29、间隔 300 µs 发 0x300，CAN0 记录后 `verify` 校验
seq 严格 +1。这一套同时验证了"记录正确""回放正确""顺序正确"三件事。

### 39.6.2 dlt_log：DLT 报文发送/接收

```bash
# 板端（上电自动 100 条/s），主机收 6 s 并落盘：
bash scripts/hwrun.sh app/dlt_log --secs 10 --post "timeout 40 bash host/dlt/run_dlt.sh" --lock-wait 1800
```

### 39.6.3 vss_signals：信号发送与 VSS 解码

```c
/* 0x400: 车速 u16(0.01 km/h) + 转速 u16(rpm)，100 ms 周期 */
f.id = 0x400U; f.len = 4;
f.data[0] = speed_001 & 0xFF; f.data[1] = speed_001 >> 8;
f.data[2] = rpm & 0xFF;       f.data[3] = rpm >> 8;
app_can_send(&s_can0, &f, 1000U);
```

上位机 `vss_decode.py --channel vcan0 --duration 3` 输出（实测，见"上板实测数据"）。

## 39.7 上板实测数据

### 39.7.1 can_logger（记录 / 回放 / 导出）

日志 `logs/20261030-175332-can_logger.log`：

| 指标 | 数值 |
|---|---|
| 记录帧数 / 容量 / 覆盖丢弃 | 30 / 512 / **0** |
| 0x300 生成速率 | 100 帧/s（10 ms 周期，680 ms 内 67 帧） |
| 回放发送成功率 | **30/30**（gap 300 µs）、20/20（gap 200 µs） |
| seq 顺序校验（固件） | **bad_order=0 → PASS**（0..29 严格 +1） |
| candump 导出 | 30 行合法文本（如 `(1793000000.777817) can0 300#00000000A55AC33C`） |
| 主机解析 | 30 帧、0 坏行、seq 30/30、顺序错误 0、丢帧 0 → **PASS** |
| RAM 占用 | 512 × 32 B = 16 KB（DLM 12.5%） |

### 39.7.2 vss_signals（VSS 信号经网关到 vcan0）

日志 `logs/20261030-175900-vss_signals.log`（Bench B：CAN0 发 → CAN1 网关 → vcan0，3 s 采样）：

```text
=== VSS 解码：vcan0 (3.0 s)，共 90 帧 ===
命中 VSS 帧 ID：0x400, 0x401, 0x402
  Vehicle.Speed                                                  0x400  km/h       31     64.80     82.80    100.80 OK
  Vehicle.Powertrain.CombustionEngine.Speed                      0x400  rpm        31   2464.00   4024.00   5584.00 OK
  Vehicle.Powertrain.CombustionEngine.ECT                        0x401  celsius    29     83.00     85.17     87.00 OK
  Vehicle.Powertrain.Transmission.CurrentGear                    0x401  none       29      1.00      1.90      6.00 OK
  Vehicle.Cabin.Door.Row1.DriverSide.IsOpen                      0x401  none       29      0.00      0.34      1.00 OK
  Vehicle.Powertrain.TractionBattery.StateOfCharge.Current       0x402  percent    30     66.40     72.20     78.00 OK
  信号覆盖 6/6，范围检查 PASS，VSS 路径校验 PASS，帧数 90（>=10）→ RESULT: PASS
```

| 指标 | 数值 |
|---|---|
| 帧率 | 90 帧 / 3 s = 30 帧/s（3 个 ID × 10 Hz，符合 100 ms 周期） |
| 信号覆盖 | **6/6**（车速/转速/水温/挡位/车门/SOC） |
| 物理值范围 | 车速 64.8~100.8 km/h、转速 2464~5584 rpm、水温 83~87 °C、挡位 1~6、车门 0/1、SOC 66.4~78.0 % |
| 符号/偏移正确性 | 水温 = raw − 40，解码 83~87 °C 与固件三角波 70~100 °C 一致 |
| 网关延迟 | 见第 28 章（UDP 网关 avg 2.26 ms），本章未单独测 |
| RAM | 与 can_logger 同量级（无大缓冲，DLM 余量充足） |

> 说明：vss_signals 固件里 `gw on` 必须**显式指定 peer IP/端口**（`192.168.2.1:4000`），
> 否则网关的 "peer auto-learn" 只在收到主机第一包后才生效——纯发送场景会一直发不出去。

### 39.7.3 DLT（板端 → 主机 UDP 3490 端到端）

板端 `samples/app/dlt_log` 以 100 条/s 发送 DLT verbose 报文（4 context × 6 level 轮转），
主机 `host/dlt/dlt_recv.py` 收 6 s、解析并落标准 `.dlt` 文件：

日志 `logs/20261030-180754-dlt_log.log`（串口）+ `logs/20261030-181000-host-dlt.log`（主机解析）：

```text
=== DLT 接收：UDP 0.0.0.0:3490，6.0 s ===
包数           : 599（解析错误 0）
实测速率       : 99.8 包/s（标称 100）
MCNT 丢包      : 2 / 601（0.33 %）
APID 直方图    : NET :150, MAIN:150, CAN :150, ADC :149
CTID 直方图    : NET :150, MAIN:150, CAN :150, ADC :149
level 直方图   : fatal:98, error:100, warn:100, info:100, debug:101, verbose:100
.dlt 文件      : /tmp/dlt/board.dlt（34143 字节）
样例报文       : APID=ADC  CTID=ADC  msin=0x01 noar=2 tmsp=13491.0 ms
                 args=[4, 'seq=1342 ctx=ADC  level=4']
=== RESULT: PASS ===
```

表 39-7 DLT 端到端实测（UDP 3490，6 s）

| 指标 | 数值 |
|---|---|
| 包数 / 解析错误 | 599 / **0** |
| 实测速率 | **99.8 包/s**（标称 100，偏差 −0.2%） |
| MCNT 丢包 | **2 / 601 = 0.33%**（UDP fire-and-forget） |
| 4 个 context 分布 | MAIN/CAN/ADC/NET ≈ 150 包各（均匀） |
| 6 个级别分布 | fatal/error/warn/info/debug/verbose 各 ≈ 100 |
| verbose 解码 | `args=[4, 'seq=1342 ctx=ADC level=4']`（level + 字符串参数均正确） |
| 头开销 | 4(std) + 4(TMSP extra) + 10(ext) + 参数 |
| .dlt 文件 | 34143 B / 599 包 = 57 B/包 |
| 丢包影响 | 0.33% 由 `MCNT` 计数可检测并报警（主机脚本已统计） |

> 结论：DLT 在 100 条/s 下**端到端可用**，UDP 丢包 0.33% 属 cannelloni 同类 UDP 通道的正常量级；
> 若要求"零丢包"需上 cannelloni TCP 模式或 DLT 的 buffer-overflow/重传机制（见 U-38-3）。

**官方工具链验证（dlt-viewer 2.30.0）**：把上述报文按 \`DltStorageHeader\`(16 B) + 标准头/扩展头写入
\`.dlt\`，用 \`xvfb-run\` 无头启动 dlt-viewer 打开，界面正常显示 **599 条消息**（Ecuid \`HPM5\`、
APID/CTID \`MAIN/CAN/ADC/NET\`、Type \`log\`、Payload \`2 seq=1628 ctx=MAIN level=2\`），
\`FileErr: 0\`——证明报文格式与官方 DLT 解析器兼容。截图证据：
\`logs/20261102-104100-dlt-viewer-host-validation.png\`。

**端到端复测（修正布局后）**：板端固件修正后重跑，主机收 **567/600 包（94.5 包/s、解析错误 0、MCNT 缺口 34=5.66%）**，
并直接落成标准 .dlt；dlt-viewer 2.30.0 打开该文件显示 HPM5/MAIN·CAN·ADC·NET、level 0..5，
**FileErr: 0**（截图 `logs/20261102-111600-dlt-viewer-end-to-end.png`）。

> **格式修正记录**：第一版把 \`TMSP\` 放在扩展头内（14 B），官方解析器按
> \`headersize = DltStorageHeader + StandardHeader + STANDARD_HEADER_EXTRA_SIZE(htyp) + ExtendedHeader\`
> 计算（\`ref2/dlt-daemon/src/shared/dlt_common.c:1837\`），导致 \`Plausibility check failed (-18)\`、
> dlt-viewer \`FileErr: 428\`。把 TMSP 移到标准头扩展、存储头改成 16 B 定长 ECU ID 后一次通过。

### 39.7.4 主机侧/离线验证（无板卡，2026-11-02 复测）

表 39-7b 离线验证结果

| 项 | 结果 | 证据 |
|---|---|---|
| canplayer 回放（vcan7） | 30/30 帧，数据逐字节一致，`rc=0` | `logs/20261102-102500-host-canplayer-offline.log` |
| cannelloni **TCP 模式**（vcan0↔vcan1，`-C s`/`-C c`） | **5/5 帧 PASS** | `logs/20261102-102600-host-cannelloni-tcp.log` |
| dlt-viewer 2.30.0 打开板端 .dlt（xvfb 无头） | **599 条消息**、Ecuid HPM5、APID/CTID/level 正确、FileErr 0 | `logs/20261102-104100-dlt-viewer-host-validation.png` |
| canplayer → vcan0 → cannelloni → 板端 CAN1 → CAN0 记录 → verify（2026-11-02 窗口） | **30/30 帧、bad_order=0 → PASS** | `logs/20261102-112054-host-canplayer-replay.log` |

## 39.8 性能与调优

| 优化点 | 现状 | 可提升方向 |
|---|---|---|
| candump 导出带宽 | 921600 串口约 85 KB/s，1000 帧 candump ≈ 45 KB（约 0.5 s） | 大容量记录改走 Flash（片内 1 MB，第 21 章）或 UDP 直传 |
| 记录容量 | 512 帧 × 32 B = 16 KB | 降到 8 B/帧（ID+DLC+data 压缩）可到 64 KB；或时间戳差分编码 |
| 回放间隔 | 300 µs（实测成功） | 更密会受 TX FIFO/总线仲裁限制；批量回放建议用 `gap 500` |
| VSS 解码 | 3 s 90 帧（纯 Python 循环） | 用 `numpy` 向量化或 cantools DBC 解析，10 万帧量级可用 |
| DLT 吞吐 | 未测 | 每包 ≤ 16 帧时 UDP 约 300 包/s；建议 1 帧/包 + `-t 1000` 网关 |
| RAM 总占用 | can_logger 16 KB、vss_signals <4 KB | 与网关（DLM 72%）共存时日志缓冲要 ≤128 帧 |

## 39.9 坑与注意事项

1. **candump 时间戳必须是绝对 epoch**：只写相对毫秒会导致 `canplayer` 回放时把时间戳当 epoch，
   排序/统计全乱；本章用"固定 epoch 基准 + 相对 µs"并在文档注明，正式记录应带 RTC/NTP 时间。
2. **candump 时间戳乱序（已修复，值得记录）**：第一版导出用 `%llu` + `epoch_ms = 1793000000*1000 + ts/1000`
   （32 位溢出）且小数部分写成"毫秒×1000"，实际输出是 `(lu.2147565568)` 或 `0.680/0.978/0.281`
   这类**非单调**伪时间戳——下游按周期/间隔分析会得出完全错误的结论（tp-xrce 用 Rust 解析时发现：
   20 帧里 8 个间隔为负）。根因是 newlib-nano 的 printf 不支持 `%llu` + 32 位毫秒溢出；
   改为 **32 位秒 + 微秒**（`%u.%06u`，秒 = 1793000000 + ts_us/1e6）后实测 30 帧
   **负间隔 0 处**、间隔 0.296~0.308 ms（`logs/20261030-175332-can_logger.log`）。
   `host/canlog/candump_parse.py` 已加"时间戳乱序"检查，出现负间隔直接判 FAIL（U-38-7）。
3. **环形缓冲的"覆盖"语义**：写满后 `s_head` 回绕，最旧帧被覆盖；如果校验依赖"从头开始"，
   必须 `log clear` 后再回放，否则 verify 会看到两段不连续的 seq（本章第一版就踩了，
   表现为 `bad_order=1, first_bad_idx=73`）。
4. **回放前要停掉周期源**：`replay` 内部先 `gen off` + 清缓冲，否则 0x300 有两个独立 seq 源。
5. **网关 peer auto-learn**：纯发送（VSS/DLT）场景必须在板端显式配置对端 IP:port，
   否则永远学不到（本章 vss_signals 第一版全 0 帧的根因）。
6. **VSS 位编号与符号**：DBC/VSS 的 sawtooth 位编号 ≠ 简单字节偏移；i8 温度要先转补码再乘 factor
   加 offset，否则 85 °C 会解成 171 °C。
7. **UDP 日志的丢包**：cannelloni/DLT 的 UDP 都是 fire-and-forget，DLT 用 `MCNT` 计数检测丢包
   （本章主机侧解析已实现），别把丢包误判成板端卡死。
8. **串口带宽是硬约束**：`log dump 1000` 会占满 921600 约 0.5 s，期间 shell 无响应，
   建议 `--secs` 留足 3 s 以上。
9. **DLT 的 TMSP 不在扩展头里**：`WTMS` 属于标准头扩展（ECU ID / session ID / timestamp 三选一），
   放错位置会让官方解析器报 `Plausibility check failed (-18)`、dlt-viewer 显示 `FileErr`；
   正确顺序：Standard Header(4) → Standard-extra(4) → Extended Header(10) → Payload。
10. **.dlt 文件的 16 B 存储头**：`DltStorageHeader` = `DLT\x01` + sec(u32 LE) + usec(i32 LE) + ECU ID(4 字符定长)，
    没有长度字节；写成"长度 + 变长 ID"会让 dlt-viewer 逐帧错位（本章 `FileErr 428` 的根因之一）。
11. **dlt-daemon 的 3490 不是 ECU 日志口**：它是 dlt-receive/dlt-control 的**控制**端口；
    把原始 DLT 报文 TCP 灌进去只会被当成 control request（daemon 回 `service(3842) ok`）。
    官方 ECU 接入路径是串口/CAN 或 `dlt-adaptor-udp`。

## 39.10 对比：DLT / VSS / candump vs 其他日志方案

表 39-6 日志与信号方案对比

| 方案 | 定位 | 带宽开销 | 结构化 | 工具链 | 本板实现 |
|---|---|---|---|---|---|
| candump/canplayer | 总线原始帧记录回放 | 低（帧级） | 否（需 DBC 解释） | can-utils | ✅ 本板记录+回放 PASS |
| VSS | 语义化车辆信号模型 | 取决于承载协议 | 是（路径+单位+范围） | vss-tools、COVESA | ✅ 映射+解码 PASS |
| DLT | ECU 日志（文本/参数） | 中（头 14 B/包） | 是（APID/CTID/level） | dlt-daemon/dlt-viewer | 板端编码器 + dlt-receive/dlt-viewer 实测 |
| SOME/IP | 服务化信号订阅 | 中高 | 是（服务/事件） | vsomeip | 第 35 章 |
| XRCE-DDS | ROS2 原生 | 中 | 是（topic） | Micro-XRCE + Agent | 第 36 章 |

选择建议：**调总线问题**用 candump；**给上层供信号**用 VSS（或 SOME/IP/XRCE-DDS）；
**查 ECU 内部逻辑**用 DLT。三者可同时在线（candump 吃 CAN、VSS 走网关、DLT 走 UDP 3490）。

## 39.11 参考来源

- COVESA [DLT 协议](https://github.com/COVESA/dlt-daemon)（`ref2/dlt-daemon`，MPL-2.0）；
  `include/dlt/dlt_protocol.h` 定义 HTYP/MSIN/APID/CTID/TMSP 与 verbose 参数编码
- COVESA [Vehicle Signal Specification](https://github.com/COVESA/vehicle_signal_specification)
  （`ref2/vehicle_signal_specification`；本仓库用其 JSON 导出 `outputplus.json` 校验路径）
- can-utils（`ref2/can-utils`）：`candump`/`canplayer`/`cansend` 格式与回放
- 仓库内：`samples/app/can_logger/README.md`、`samples/app/vss_signals/README.md`、
  `host/canlog/README.md`、`host/vss/vss_decode.py`
- 实测日志：`logs/20261030-175332-can_logger.log`、`logs/20261030-175900-vss_signals.log`
- 第 28 章（cannelloni 网关）、第 13 章（lwIP）、第 08 章（MCAN）

## 39.12 待确认项与风险

> 本表已逐条复核（2026-11-02）：能测的给实测，受板卡窗口/板端改码限制的写明原因与验证方法。

| 编号 | 项 | 现状 | 验证方法 |
|---|---|---|---|
| U-38-1 | canplayer + 网关回放的端到端链路 | **已实测**：板上 `can_logger` 在 `gw on` 模式下，主机 `canplayer -I` 回放 30 帧 0x300（seq 0..29）→ vcan0 → cannelloni → CAN1 → CAN0，板端 `verify` 得到 `frames=30 bad_order=0 result=PASS`（`logs/20261102-112054-host-canplayer-replay.log`） | 已通过；注意上电自检不要再发 0x300（会污染校验基线），本修复已固化为`启动不自动 replay` |
| U-38-2 | DLT 板端编码器 + 每秒条数/丢包实测 | **已实测**：`samples/app/dlt_log` 100 条/s，实测 99.8 包/s、丢包 0.33%、解析 0 错误（`logs/20261030-181000-host-dlt.log`） | 已实现并实测；dlt-viewer 打开 .dlt 文件的可视化校验未做（U-38-3） |
| U-38-3 | DLT 官方工具链验证 | **已实测（端到端）**：板端 `dlt_log` 修正布局后，主机 `dlt_recv.py` 直接落 `.dlt`，dlt-viewer 2.30.0 打开显示 **HPM5 / MAIN·CAN·ADC·NET / level 0..5** 全部消息、FileErr 0（截图 `logs/20261102-111600-dlt-viewer-end-to-end.png`）；本轮收 567/600 包（MCNT 缺口 34，5.66%，UDP 无重传，属待观察项） | 已通过；如需零丢包走 TCP/adaptor-udp（U-38-8） |
| U-38-8 | DLT **dlt-daemon 直连** | **已定位链路（2026-11-02）**：本机已装 dlt-daemon 3.0.1，`ss -lunp` 显示 **UDP 3490 与 TCP 3490 都在监听**——ECU 日志可直连 daemon，不必再起 `dlt-adaptor-udp`（它反而会因端口占用启动失败）；`dlt-receive`/`dlt-convert`/`dlt-viewer` 均可用，验证脚本 `host/dlt/r5_dlt_daemon.sh` 已写好。本轮板卡排队未取得完整收包统计；2026-11-02 18:17 重试把 daemon 直连链路打通（dlt-receive 能连、dlt-convert 解析 0 error），但 post 在串口抓包结束后才起 daemon，错过板端 1.2 s auto 发送窗口（APID 直方图 0，只收到 4 条 daemon 自身控制消息，日志 logs/20261102-181700-host-dlt-daemon.log），ECU 报文统计仍未取到；正确做法是先把 daemon 起在抓包之前，或按 README 用 host/dlt/run_dlt.sh 在同一次 hwrun 内完成 | 板端 `bash scripts/hwrun.sh app/dlt_log --secs 26 --post "bash host/dlt/r5_dlt_daemon.sh"`；判据：`dlt-receive` 条数≈100 条/s×时长，`dlt-convert -a` 解析 0 error |
| U-38-4 | VSS 全树覆盖（>6 信号） | **已知限制（原因：本轮无板卡窗口做板端发帧表改造）**：仍 6 个信号（3 帧 × 1~2 信号） | 用 cantools 45.1.2（pyenv 已装）从新建 DBC 生成 30+ 信号映射；板端改表驱动发帧（0x410~0x41F），`host/vss/vss_decode.py` 的 MAP 同步；判据：30 条 VSS 路径全部解码且范围检查 PASS |
| U-38-5 | 长时间记录（>512 帧）与 Flash 落盘 | **已知限制（原因：未做板端改码与窗口）**：RAM 环形 512 帧（16 KB） | 最小改动先试：`can_logger` 的 `LOG_CAP` 512→2048（64 KB RAM），canplayer 回放 2000 帧后 `log dump` 核对无覆盖；再按第 21 章做 Flash 环形文件 |
| U-38-6 | 时间同步（candump 时间戳与墙钟对齐） | **已知限制（原因：需先把 PTP/NTP 时基接进板端）**：现在固定 epoch + 相对时间 | 板端把第 38 章 PTP 的 SYST_SEC/NSEC 当 epoch 写进帧时间戳，主机 `candump_parse.py` 比对墙钟；或板端加 NTP 客户端 |
| U-38-7 | candump 时间戳单调性 | **已定位（已修复）**：早期版本 `%llu` + 32 位毫秒溢出导致乱序（tp-xrce 下游发现 8/20 负间隔）；现版本 30 帧负间隔 0 | `host/canlog/candump_parse.py` 新增"时间戳乱序"统计并参与 PASS/FAIL；回归命令见 can_logger README |
