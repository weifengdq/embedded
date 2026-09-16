# tc397_tlf35584 — TC397 + TLF35584 安全电源 SBC 全功能调试工程

以 `tc397_uart_lettershell`（ASCLIN0 921600 + Letter-Shell + P13.0 LED）为蓝本，
新增 **QSPI2 主机驱动 + TLF35584 全功能 `tlf` Shell 命令**。覆盖 TLF35584 的
电源轨、状态机、SPI 寄存器、保护锁、窗口/功能看门狗、ERR 监控、安全状态、
ABIST、Buck 微调、唤醒定时器等全部功能（官方 `SPI_TLF_1_KIT_TC397_TFT` 例程仅
演示 unlock→关 WWD/ERR→进 NORMAL，本工程将其扩展为可交互调试全集）。

* 基线：`tc397_uart_lettershell`（见其 README；UART/时钟/LED 均保留）
* 工具链：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ CMake/Ninja + TAS/DAS 8.3.0 +
  `aurix_flasher`（复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`）
* 串口：`/dev/ttyACM0`（1a86:55d3），921600-8N1；DAP MiniWiggler `058b:0043` 仅用于下载
* 实测状态（2026-09-16，板上即本 commit 的 Debug 版）：TLF **NORMAL**，
  全轨就绪，全部标志清零，`tlf` 全命令验证通过（§7）

---

## 1 硬件与原理

### 1.1 引脚连接（本工程）

| TLF35584 引脚 | TC397 引脚 | 方向（MCU 视角） | 说明 |
| --- | --- | --- | --- |
| SDI (MOSI) | P15.6 | 输出（QSPI2 MTSR alt3） | SPI 主→从 |
| SDO (MISO) | P15.7 | 输入（QSPI2 MRST RxSel_b，下拉） | SPI 从→主；读回首位恒 1 |
| SCL | P15.8 | 输出（QSPI2 SCLK alt3） | 默认 2MHz（`tlf baud` 可改 100k–10M；SLEEP 态 TLF 侧上限 1.5M） |
| SCS | P14.2 | 输出（QSPI2 SLSO1 alt3，硬件片选） | 每帧自动拉低/释放 |
| WDI | P14.3 | 输出（GPIO，idle 低） | 看门狗触发输入（TLF 内下拉 150–330µA）；`tlf wdi` |
| SS1 | P33.9 | 输入（GPIO 上拉） | 安全状态输出；**低 = 安全状态**；`tlf ss` |
| ERR | P33.8 | 输出（GPIO，idle 高） | 台架位 bang 模拟；量产应路由 SMU FSP0（`IfxSmu_FSP0_P33_8_OUT`，idle 高）；`tlf err` |
| ROT | nPORST | （专用复位脚，无 SW 动作） | 本次台架 6+ 次 INIT 迁移均**未观察到复位**（见 §7.7，MPS=1 阻断） |
| INT | nESR1 | （专用中断脚，无 SW 动作） | 推挽低脉冲；由 `IF` 寄存器读回（`INTMISS` 置位即证明 INT 曾跳变，§7.5） |
| MPS | →VCO（高） | 台架 strapping | **Test Mode 1**（编程支持模式）：INIT 定时器停止；WWD/FWD/ERR 对 ROT 的贡献被阻断，但状态机照常迁移（DS §11.7）。证据见 §7.7 |
| SEC | 悬空 | strapping | 使用 step-up 前级（`DEVCFG2.STU=1` 实测确认） |
| FRE | 悬空 | strapping | Buck 高频 2.2MHz（`DEVCFG2.FRE=1` 实测确认） |
| VCI | 电阻分压约 0.8V | 模拟输入 | 外部芯核电源检测 |
| EVC | 外部 DC-DC 使能 | 输出使能 | 外部后级芯核电源使能（`DEVCFG2.EVCEN=1` 实测确认） |

> 注意：P14.2/P14.3 是 TC397 的 HWCFG 引脚（复位采样），但不影响其复位后作
> QSPI/GPIO 使用（本工程实测正常）。

### 1.2 TLF35584 电源树（一句话）

VS（电池）→ 前级 **Buck（2.2MHz，FRE=开）+ Step-up（SEC=开）** → 预稳 VPRE →
后级：`LDO_µC (QUC)` 给 MCU、`VCI` 外挂 DC-DC 给芯核、`QST` 待机、`QCO` 通信、
`QVR` 基准、`QT1/QT2` 传感器 tracker。每路独立 OV/UV/StG 监控，
故障记入 `MONSF0/1/2/3`，严重故障触发状态机迁移（§1.3）。

### 1.3 状态机（DS Ch.11，实测行为见 §7）

`POR → INIT → NORMAL ⇄ SLEEP/STANDBY/WAKE`，另有 `FAILSAFE`（严重故障）、
`POWERDOWN`。关键实测结论：

* 上电默认停在 **INIT**（`DEVSTAT=0xF9`，SS1=0），等 MCU 经 SPI 配置后发
  `DEVCTRL+DEVCTRLN` 进 NORMAL（`tlf demo` 一键完成：unlock→关 WWD/ERR→
  lock→开 COM/VREF→清标志→goto NORMAL）。
* `INIT→NORMAL` 的前提（DS §11.3.2，台架证实）：WWD/FWD 安静（关闭或被正确
  服务）+ ERR 关闭或翻转正常 + 标志清零，否则请求被静默拒绝（`DEVSTAT` 保持
  INIT，本工程复现过 2 次，见 §7.8）。
* **`*R3` 寄存器在 INIT 迁移后并不复位**（与 DS Table 22 标注不符，3 次独立
  复现）：`RWDCFG0/RWWDCFGx/RWDCFG1/RFWDCFG/RSYSPCFG1`、看门狗计数值全部保留，
  利于 post-mortem，但要求软件显式关闭/清理。详见 §7.6。

### 1.4 SPI 协议（DS Ch.13）

* 16 clocks/帧：`CMD(1) + ADDR(6) + DATA(8) + PARITY(1)`。
  MOSI 在 SCL 上升沿采样；MISO 写时环回 MOSI，读时返回
  `1'b1 + STATUS[5:0](=0) + DATA[7:0] + PARITY`。
* 奇偶：15 位异或（= 偶校验语义），本工程用 QSPI **硬件偶校验**
 （`dataWidth=15, parityCheck=TRUE, parityMode=even`），与官方例程一致；
  MOSI 下降沿移位（`shiftTransmitDataOnTrailingEdge`）。
* **保护寄存器**（`SYSPCFG0/1, WDCFG0/1, FWDCFG, WWDCFG0/1`）写前必须
  `UNLOCK（AB EF 56 12）`，写后 `LOCK（DF 34 BE CA）` 生效；期间穿插其它写会
  中断序列（`SPISF.LOCK` 置位）。读保护请求寄存器返回**按位取反**值
  （与对应状态寄存器 XOR 应为 `0xFF`，本工程以此自检链路，§7.2）。
* `DEVCTRL(0x15)+DEVCTRLN(0x16)` 必须连写且后者按位取反，CS 上升沿生效；
  否则 `SYSSF.NO_OP` 置位。`STATEREQ` 被硬件清零即表示已受理（实测 `DEVCTRL`
  回读 `STATEREQ=000`）。
* SPI 错误（PARE/LENE/ADDRE/DURE/LOCK）置位 `SPISF` 并产生 INT 中断
 （`IF.SPI`）。SCS 拉低超 ~2ms 即 DURE（复位/下载期间引脚浮空会在上电残留
  该标志，属正常，开工前 `tlf clear spisf` 即可，§7.2）。

### 1.5 看门狗（DS Ch.15，两份 window-watchdog 应用笔记已转 txt 在 `ref/`）

* **WWD（窗口）**：INIT 的 ROT 上升沿后从 LONG OPEN WINDOW 启动；
  触发源可选 WDI 引脚或 SPI 写 `WWDSCMD`（读 `TRIG_STATUS` 后写反值）。
  OW 内有效触发进 CW，CW 内触发或 OW 超时皆判无效：无效 +2 / 有效 −1；
  计数器 ≥ `WWDETHR` 即溢出进 INIT（`INITERR.WWDF`）。
  窗长单位 50×`WDCYC`（0.1ms/1ms）。
  WDI 引脚需“2 高采样 + 2 低采样”，判决点（第 2 个低采样）落在 OW 内才有效。
* **FWD（功能/问答）**：使能后按 `WDHBTP×50` 心跳出题（`FWDSTAT0.QUEST`），
  按 Table 26 把 `RESP3→FWDRSP, RESP2→FWDRSP, RESP1→FWDRSP, RESP0→FWDRSPSYNC`
  依次作答；末字节必须走 SYNC 寄存器才复位心跳。答对 −1 / 答错 +2，
  ≥ `FWDETHR` 进 INIT（`INITERR.FWDF`）。
* **本工程的关键实测发现（§7.4）**：FWD FSM 每心跳周期最多消费 **1 个响应字节**，
  字节间隔必须大于心跳周期（600ms 心跳用 700ms 间隔 2 次成功；
  10ms/150ms 间隔一律丢弃且每次 +2）。`tlf fwd answer/bgauto` 已按
  “心跳+100ms 自适应间隔”实现为后台非阻塞序列机。
* WWD-SPI 连续喂狗的正确姿势（§7.3 实测 56 次全有效）：窗口配成
  CW=OW（如 100ms/100ms），首次手动触发定相后，以 **CW+OW 周期**
  自动喂（`tlf wwd auto on 200`），命中 CW 的触发会终止 CW 实现自同步；
  启动初相随机，头几次 CW 命中 +2 属正常，需给 `WWDETHR` 留余量。

### 1.6 ERR 监控与安全状态（DS Ch.12）

* `ERR` 引脚期望翻转信号（量产接 SMU FSP，典型百 Hz 量级）；常高/常低超
  `ΔtDET` 即判错：`ERRRECEN=0` 直接进 INIT（`INITERR.ERRF`），
  `ERRRECEN=1` 则先给恢复窗（`ERRREC` 1/2.5/5/10ms），窗内恢复则只告警。
* 错
...[truncated 10976 chars]