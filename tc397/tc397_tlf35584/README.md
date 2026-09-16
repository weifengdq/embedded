# tc397_tlf35584 — TC397 + TLF35584 安全电源 SBC 全功能调试工程

以 `tc397_uart_lettershell`（ASCLIN0 921600 + Letter-Shell + P13.0 LED）为蓝本，
新增 **QSPI2 主机驱动 + TLF35584 全功能 `tlf` Shell 命令**。覆盖 TLF35584 的
电源轨、状态机、SPI 寄存器、保护锁、窗口/功能看门狗、ERR 监控、安全状态、
ABIST、Buck 微调、唤醒定时器等全部功能（官方 `SPI_TLF_1_KIT_TC397_TFT` 例程仅
演示 unlock→关 WWD/ERR→进 NORMAL，本工程将其扩展为可交互调试全集，
外加**开机自初始化**解决 MPS=0 复位循环）。

* 基线：`tc397_uart_lettershell`（见其 README；UART/时钟/LED 均保留）
* 工具链：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ CMake/Ninja + TAS/DAS 8.3.0 +
  `aurix_flasher`（复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`）
* 串口：`/dev/ttyACM0`（1a86:55d3），921600-8N1；DAP MiniWiggler `058b:0043` 仅用于下载
* 实测状态：TLF **NORMAL** 全轨就绪，`tlf` 全命令验证通过；开机自初始化
  `TLF auto-init: NORMAL ok` 已验证（§7.9）

---

## 1 硬件与原理

### 1.1 引脚连接（本工程）

| TLF35584 引脚 | TC397 引脚 | 方向（MCU 视角） | 说明 |
| --- | --- | --- | --- |
| SDI (MOSI) | P15.6 | 输出（QSPI2 MTSR alt3） | SPI 主→从 |
| SDO (MISO) | P15.7 | 输入（QSPI2 MRST RxSel_b，下拉） | SPI 从→主；读回首位恒 1 |
| SCL | P15.8 | 输出（QSPI2 SCLK alt3） | 默认 2MHz（`tlf baud` 可改 100k–10M；SLEEP 态 TLF 侧上限 1.5M） |
| SCS | P14.2 | 输出（QSPI2 SLSO1 alt3，硬件片选） | 每帧自动拉低/释放 |
| WDI | P14.3 | 输出（GPIO，idle 低） | 看门狗触发输入（TLF 内下拉 150–330uA）；`tlf wdi` |
| SS1 | P33.9 | 输入（GPIO 上拉） | 安全状态输出；**低 = 安全状态**；`tlf ss` |
| ERR | P33.8 | 输出（GPIO，idle 高） | 台架位 bang 模拟；量产应路由 SMU FSP0（`IfxSmu_FSP0_P33_8_OUT`，idle 高）；`tlf err` |
| ROT | nPORST | （专用复位脚，无 SW 动作） | 本次台架 INIT 迁移均未引起 TC397 复位（§7.7；MPS=1 阻断 ROT，连续性待示波器复核） |
| INT | nESR1 | （专用中断脚，无 SW 动作） | 推挽低脉冲；由 `IF` 读回（`INTMISS` 置位即证明 INT 曾跳变，§7.5） |
| MPS | →VCO（高/低切换） | 台架 strapping | 高=Test Mode 1（编程支持模式，§1.6）；低=Normal（量产模式，需开机自初始化 §1.7） |
| SEC | 悬空 | strapping | 使用 step-up 前级（`DEVCFG2.STU=1` 实测确认） |
| FRE | 悬空 | strapping | Buck 高频 2.2MHz（`DEVCFG2.FRE=1` 实测确认） |
| VCI | 电阻分压约 0.8V | 模拟输入 | 外部芯核电源检测 |
| EVC | 外部 DC-DC 使能 | 输出使能 | 外部后级芯核电源使能（`DEVCFG2.EVCEN=1` 实测确认） |

> 注意：P14.2/P14.3 是 TC397 的 HWCFG 引脚（复位采样），但不影响其复位后作
> QSPI/GPIO 使用（本工程实测正常）。

### 1.2 TLF35584 电源树（一句话）

VS（电池）→ 前级 **Buck（2.2MHz，FRE=开）+ Step-up（SEC=开）** → 预稳 VPRE →
后级：`LDO_uC (QUC)` 给 MCU、`VCI` 外挂 DC-DC 给芯核、`QST` 待机、`QCO` 通信、
`QVR` 基准、`QT1/QT2` 传感器 tracker。每路独立 OV/UV/StG 监控，
故障记入 `MONSF0/1/2/3`，严重故障触发状态机迁移（§1.3）。

### 1.3 状态机（DS Ch.11，实测行为见 §7）

`POR → INIT → NORMAL`，另有 SLEEP/STANDBY/WAKE/FAILSAFE/POWERDOWN。关键结论：

* 上电默认停在 **INIT**（`DEVSTAT=0xF9`，SS1=0），等 MCU 经 SPI 配置后发
  `DEVCTRL+DEVCTRLN` 进 NORMAL（`tlf demo` / 开机自初始化一键完成：
  unlock→关 WWD/FWD/ERR→lock→开 COM/VREF→清标志→goto NORMAL）。
* `INIT→NORMAL` 的前提（DS §11.3.2，台架证实）：WWD/FWD 安静（关闭或被正确
  服务）+ ERR 关闭或翻转正常 + 标志清零，否则请求被静默拒绝（复现过 2 次）。
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
  否则 `SYSSF.NO_OP` 置位。`STATEREQ` 被硬件清零即表示已受理（实测回读
  `STATEREQ=000`）。
* SPI 错误（PARE/LENE/ADDRE/DURE/LOCK）置位 `SPISF` 并产生 INT 中断
  （`IF.SPI`）。SCS 拉低超约 2ms 即 DURE（复位/下载期间引脚浮空会在上电残留
  该标志，属正常，开工前 `tlf clear spisf` 即可）。

### 1.5 看门狗（DS Ch.15，window-watchdog 两份应用笔记已转 txt 在 `ref/`）

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
  字节间隔必须大于心跳周期（600ms 心跳用 700ms+ 间隔 2 次成功；
  10ms/150ms 间隔一律丢弃且每次 +2）。`tlf fwd answer/bgauto` 已按
  “心跳+100ms 自适应间隔”实现为后台非阻塞序列机。
* WWD-SPI 连续喂狗的正确姿势（§7.3 实测 56 次全有效）：窗口配成
  CW=OW（如 100ms/100ms），首次手动触发定相后，以 **CW+OW 周期**
  自动喂（`tlf wwd auto on 200`），命中 CW 的触发会终止 CW 实现自同步；
  启动初相随机，头几次 CW 命中 +2 属正常，需给 `WWDETHR` 留余量。

### 1.6 ERR 监控、安全状态与测试模式（DS Ch.12 / §11.7）

* `ERR` 引脚期望翻转信号（量产接 SMU FSP，典型百 Hz 量级）；常高/常低超
  `ΔtDET` 即判错：`ERRRECEN=0` 直接进 INIT（`INITERR.ERRF`），
  `ERRRECEN=1` 则先给恢复窗（`ERRREC` 1/2.5/5/10ms），窗内恢复则只告警。
* 出错时序：`SS1` 立即拉低，`SS2` 按 `SS2DEL`（0/10/50/100/250ms）延迟拉低；
  WWD/FWD 计数器超限同样先拉 `SS1/SS2`（阈值 `ΣWWO/ΣFWO` 即 `WWDETHR/FWDETHR`）。
* **MPS=1 测试模式（DS §11.7）**：INIT 定时器停止；WWD/FWD/ERR 对 ROT 的
  贡献被阻断，但状态机迁移照常（溢出仍进 INIT，只是不复位 MCU）。
  台架行为与之完全吻合（§7.7），故 MPS=1 下可从容调试。
* **MPS=0 正常模式**：POR 后 INIT 定时器运转；若固件不及时配置+服务，
  超时软复位（ROT 拉低 tRD，`RESDEL=6`→10ms）→ MCU 重启死循环（§7.9）。
  本工程用开机自初始化（§1.7）解决。

### 1.7 开机自初始化（`TLF_AUTO_INIT`，`App/tlf35584.h`，默认 1）

`Tlf_Init()` 只配 QSPI2/GPIO（被动）；紧随其后的 `Tlf_AutoInit()`
（`Cpu0_Main.c` 调用，启动日志打印 `TLF auto-init: ...`）完成 `demo` 同款序列：
`LinkOk` 检查 → 已 NORMAL 则直接返回 → 否则 unlock→关 WWD+FWD→关 ERR→lock→
保轨开 COM/VREF→清全部标志→进 NORMAL（带一次重试），返回成功与否。
`LinkOk` 以 MISO 首位判空（MISO 有下拉，无 TLF 时读 `0x0000`），无 TLF 时静默
跳过、不卡启动。`tlf demo` 已重构为调用同一函数（单点真相）。
**MPS=0 依赖它**：POR 后 TLF 停 INIT 且 INIT 定时器/WWD/ERR 全开，
没有这一步 MCU 必被循环复位（§7.9）。任何异常软件复位后它也会把 TLF 带回
NORMAL（配置 retained 时直接返回）。

### 1.8 台架 USB 串口重放干扰（测试方法约束，先读！）

本台架 CH340（`/dev/ttyACM0`）叠加 xhci/EMI 不稳定（handover 已记录两次
hub 掉线），会**把之前发送过的命令原文重放注入 RX**：无人操作时仍有
`Command not Found`/完整旧命令回显、旧 `tlf` 命令被真实执行
（`xfer` 莫名暴涨数万）、旧 `reset` 导致“自发重启”、旧 `wdcfg/syscfg` 篡改
TLF 配置。已排除本机残留进程与设备端状态（软件重枚举无效，源头在传输层）。
判别与应对：

* 用 `xfer` 连续性 + `mcu`（`RSTSTAT`：SW=bit4 软件复位，ESR0/1=bit0/1，
  PORST=bit16 粘性）区分“真复位”与“重放 reset”；
* 关键结论（如 ROT 零复位）以多次复核为准，不采单次快照；
* 关键测量前先静默 30 秒（串口无自发字节）再动手；风暴持续则把 CH340 的
  USB 线物理重插；
* 重放的 `reset` 无害（重启后自初始化 1 秒内回 NORMAL）；重放的
  `wdcfg/syscfg` 若改了监控配置，跑一遍 `tlf demo && tlf clear all` 即恢复。

---

## 2 目录结构

```
tc397_tlf35584/
├── App/tlf35584.{h,c}      # QSPI2 主机 + WDI/ERR/SS1 GPIO + 全套 helper
│                           # （unlock/lock、GotoState、WWD 触发、FWD Table 26、
│                           #  后台 auto、FWD 序列机、LinkOk、AutoInit）
├── Shell/shell_tlf.c       # `tlf` 命令（30+ 子命令，全寄存器/功能覆盖）
├── Shell/shell_port.c      # 原有 mcu/temp/led 等命令（保留）
├── Configurations/ConfigurationIsr.h  # 新增 QSPI2 TX50/RX51/ER52
├── Cpu0_Main.c             # Tlf_Init() + TLF_AUTO_INIT 自初始化 + 后台 Tlf_Background()
├── cmake/ + CMakeLists.txt # 复用 uart 基线（App/ 由 AurixProject.cmake 自动收集）
├── build.sh / serial_monitor.py
└── build/gcc/tc397_tlf35584.{elf,hex,map}  # Debug=text110245；Release 另编验证
```

---

## 3 构建与下载（Ubuntu）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_tlf35584
./build.sh build                        # Debug（板上版本）
./build.sh build --build-type Release   # 仅编译验证
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 经 flasher -read 触发复位
```

下载说明与 TAS 排查同 uart 基线 README。注意烧录后串口约 15 秒二进制乱码
风暴（§1.8），自清除后再测；首条命令若被吞掉属正常，重发即可。

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw   #（miniterm 在无 tty 下可能 ioctl 报错，改用 serial_monitor.py）
python3 serial_monitor.py --port /dev/ttyACM0 --baud 921600 --cmd "tlf link" --duration 5
```

启动日志新增行：

```
TLF35584 on QSPI2 (P15.6/15.7/15.8 + nCS P14.2, 2MHz HW-parity), try 'tlf link'
TLF auto-init: NORMAL ok (DEVSTAT 0xF9->0xFA NORMAL, SS1=1)
```

---

## 4 `tlf` 命令手册（`tlf help` 亦可查）

| 子命令 | 说明 |
| --- | --- |
| （无参）/`state` | DEVSTAT/VMONSTAT/SS1/WDI/ERR/lock 摘要 + 全标志 |
| `link`/`test` | 读 DEVSTAT/PROTSTAT/GTM，校验 MISO 首位=1（链路判定） |
| `dump [f [t]]` | 寄存器转储（默认 0x00–0x33 + 0x3F，附名） |
| `rd <a> [n]` / `wr <a> <v>` | 原始读写（16 进制可；写保护寄存器需先走配置类命令） |
| `unlock`/`lock`/`prot` | 保护序列 / PROTSTAT+KEYx+LOCK 回显 |
| `goto <normal\|sleep\|standby\|wake\|init> [trk2 trk1 com vref]` | DEVCTRL+反码 DEVCTRLN（缺省保轨） |
| `flags` / `clear <all\|sysfail\|initerr\|if\|syssf\|wksf\|spisf\|monsf\|ot>` | 读/清 rw1c 标志 |
| `devcfg [trdel]` | DEVCFG0（TRDEL 步进 100us）+ DEVCFG1/2 解码（含 EVC/STU/FRE strapping 回读） |
| `syscfg [ss2 errslp erren recrec rec]` | SYSPCFG1（自动 unlock+lock） |
| `wdcfg [ethr wden fwden tsel cyc]` / `wdcfg1 [slpen fwdethr]` | WDCFG0/1（自动 unlock+lock） |
| `fwdcfg [hbt]` | FWDCFG（心跳周期数，自动 unlock+lock） |
| `wwcfg [cw ow]` | CW/OW 窗（×50 周期，自动 unlock+lock，附 ms 换算） |
| `wwd trig [n]` / `wwd status` / `wwd auto <on [ms]\|off>` | SPI 喂狗（读 TRIG_STATUS 写反值）/计数器/后台相位锁定自喂 |
| `fwd status\|answer\|resp <b>\|sync <b>\|bgauto <on [ms]\|off>` | 问答：状态/后台 paced 作答/单字节/同步字节/后台重复作答 |
| `wdi <high\|low\|toggle\|pulse <ms>\|auto <on [ms]\|off>>` | WDI 引脚 |
| `err <high\|low\|toggle\|burst <n> <ms>\|auto <on [ms]\|off>>` | ERR 引脚（台架模拟；量产用 SMU FSP0） |
| `ss` | 读 SS1（P33.9） |
| `rails <com> <vref> [trk1] [trk2]` | 不换态改轨配置 |
| `wktim [cycles]` | 24 位唤醒定时器（单位 DEVCFG0.WKTIMCYC） |
| `abist <show\|sel <m> <v>\|ctrl0 <v>\|ctrl1 <v>\|start>` | ABIST 比较器自检 |
| `buck [set <freq\|spread\|main> <v>]` | Buck 频率微调/展频/主控 |
| `baud <hz>` | QSPI2 重配（100k–10M） |
| `demo`/`init` | 开机同款序列（Tlf_AutoInit， подготовку NORMAL） |
| `gpio` | P14/P33 OUT/IN/IOCR/PDISC 原始寄存器转储（排障） |

## 5 移植要点（vs uart 基线 / 官方例程）

* **保留 uart 基线全部**（iLLD TC39xB、ASCLIN0 921600、Shell、LED、Lcf、6 核）。
  新增：`App/tlf35584.*`、`Shell/shell_tlf.c`、`ConfigurationIsr.h` 三个优先级、
  `Cpu0_Main.c` 三处（`Tlf_Init` + 自初始化 + `Tlf_Background` + banner 行）。
* **iLLD 版本差**：本仓 iLLD 比官方例程新，QSPI 接口已变
  （`IfxQspi_Mode_master`、扁平 Config、`ch.ch.*` 通道配置、
  `IfxQspi_ShiftClock_shiftTransmitDataOnTrailingEdge`、
  `IfxQspi_SlsoTiming_2`、`IfxQspi_Status_busy`），Pins 结构相同。
  例程的寄存器位域表（TLF35584.h）未照搬——本工程用地址宏 + shell 解码，
  避免老 iLLD 类型名（`SpiIf_*`/`Ifx_ParityMode_*`）编译失败。
* 引脚与例程一致（QSPI2 的 P15.6/15.7/15.8 + P14.2），波特率取 2MHz
  （例程 5MHz；TLF 上限 10MHz，SLEEP 态 1.5MHz），`tlf baud` 可改。

---

## 6 寄存器总表（地址 / 复位值 / 本板 POR 实测）

| Addr | 名称 | 复位 | POR 实测 | 备注 |
| --- | --- | --- | --- | --- |
| 00 | DEVCFG0 | 08 | 08 | TRDEL=8（900us） |
| 01 | DEVCFG1 | 06 | 06 | RESDEL=6（10ms） |
| 02 | DEVCFG2 | — | E0 | EVC=1 STU=1 FRE=1（strapping 回读） |
| 03 | PROTCFG | 00 | 00/CA | 读回=最后写入的 key 字节 |
| 04 | SYSPCFG0 | 01 | FE（取反） | STBYEN=1 |
| 05 | SYSPCFG1 | 00 | F7（取反） | ERREN=1（默认开！） |
| 06 | WDCFG0 | 9B | 64（取反） | ETHR=9 WWDEN=1 FWDEN=0 SPI 1ms |
| 07 | WDCFG1 | 09 | F6（取反） | FWDETHR=9 |
| 08 | FWDCFG | 0B | F4（取反） | WDHBTP=11（600 周期心跳） |
| 09/0A | WWDCFG0/1 | 06/0B | F9/F4（取反） | CW=300 OW=550 @1ms |
| 0B/0C | RSYSPCFG0/1 | 01/08 | 01/08 | 状态（非取反） |
| 0D/0E | RWDCFG0/1 | 9B/09 | 9B/09 | 状态（非取反） |
| 0F/10/11 | RFWDCFG/RWWDCFG0/1 | 0B/06/0B | 同左 | 状态（非取反） |
| 12–14 | WKTIMCFG0–2 | 00 | 00 | 24 位唤醒定时器 |
| 15/16 | DEVCTRL/N | 00 | 00/E8→00 | 请求受理后 STATEREQ 被硬件清零 |
| 17 | WWDSCMD | 00 | TRIG_STATUS 翻转 | 喂狗：读 bit7 写反值 |
| 18/19 | FWDRSP/SYNC | 00 | 末次写入值可读回 | 问答字节 |
| 1A | SYSFAIL | 00 | 全 rw1c | INITF/ABISTERR/VMONF/OTF/VOLTSELER |
| 1B | INITERR | 00 | 全 rw1c | HARDRES/SOFTRES/ERRF/FWDF/WWDF/VMONF |
| 1C | IF | 00 | 全 rw1c | INTMISS(r)/ABIST/OTF/OTW/MON/SPI/WK/SYS |
| 1D | SYSSF | 00 | 全 rw1c | NO_OP/TRFAIL/ERRMISS/FWDE/WWDE/CFGE |
| 1E/1F | WKSF/SPISF | 00 | 全 rw1c | 唤醒源 / LOCK/DURE/ADDRE/LENE/PARE |
| 20/21/22 | MONSF0/1/2 | 00 | 全 rw1c | StG/OV/UV（8 路×3 组） |
| 23 | MONSF3 | 00 | 全 rw1c | BIAS/BG12/VBATOV |
| 24/25 | OTFAIL/OTWRNSF | 00 | 全 rw1c | 过温故障/预警 |
| 26 | VMONSTAT | 00 | FC（全就绪） | 各轨 ready（禁用的轨为 0） |
| 27 | DEVSTAT | 00 | F9→FA | 使能位 + STATE（INIT→NORMAL） |
| 28 | PROTSTAT | 01 | 01/F0/F1 | KEYxOK + LOCK（unlock 后 F0，lock 后 F1） |
| 29 | WWDSTAT | 00 | 计数器 | WWDECNT（+2/−1，≥ETHR 溢出） |
| 2A/2B | FWDSTAT0/1 | 30/00 | RSPOK/RSPC/QUEST/FWDECNT | 问答状态 |
| 2C–30 | ABIST_* | 00 | STATUS=5 为 PASS | CTRL0 bit0=START（硬件自清） |
| 31–33 | BCK_* | 00 | rw | Buck 微调（±1.5% 步进） |
| 3F | GTM | 02 | 02 | NTM=1/TM=0（注意：≠MPS 状态，见 §7.7） |

FWD Table 26（问→答 RESP3..RESP0）：0:FF0FF000 1:B040BF4F 2:E919E616
3:A656A959 4:75857A8A 5:3ACA35C5 6:63936C9C 7:2CDC23D3 8:D222DD2D
9:9D6D9262 A:C434CB3B B:8B7B8474 C:58A857A7 D:17E718E8 E:4EBE41B1
F:01F10EFE（作答顺序 RESP3,2,1→FWDRSP，RESP0→FWDRSPSYNC，间隔须大于心跳）。

---

## 7 测试方法、步骤与结果（2026-09-16，MPS=1；日志 `temp/tlf_test*.log`）

通用准备：TAS 运行中 → `./build.sh download`（Pass 3.1s）→ 等乱码风暴过去 →
`tlf link` 显示 `link OK`。所有关键结论均多次复核（含重放干扰下的复核）。

### 7.1 链路（`tlf link`，test1）

`DEVSTAT rx=0x40F9 msb=1` / `PROTSTAT rx=0x4001` / `GTM rx=0x4002`，
MISO 首位恒 1 + HW 校验通过，2MHz 与 5MHz 均 OK。累计约 60000 transfers
**`tout=0`**（含重放命令的真实传输）。

### 7.2 寄存器基线（`tlf dump` + 各 `*cfg`，test2）

全表复位值与手册 Table 22 一致；保护请求寄存器读回取反
（如 `WDCFG0=0x64` vs `RWDCFG0=0x9B`，XOR=`0xFF`），反证 SPI 链路正确。
上电陈旧 `SPISF=0x0B`（PARE+LENE+DURE，复位/下载期引脚浮空所致），
`clear` 后做数百次传输保持 `0x00`。`DEVCFG2=0xE0` 反推 SEC/FRE/EVC
strapping 与任务书一致。

### 7.3 WWD-SPI 喂狗与相位锁定（test3/13/14，两次独立复现）

* 单次 `tlf wwd trig`：`TRIG_STATUS` 0→1→0 翻转，OW 内命中计数器 −1，
  CW 内命中 +2（窗口语义活体演示）。
* 相位锁定：`wwcfg 2 2`（CW=OW=100ms）+ 使能 + 手动 trig 定相 +
  `wwd auto on 200`（周期=CW+OW），8→0 排空后 **56 次连续有效、
  计数器恒 0**；`feeds` 计数与周期数学自洽。初相随机，前几次 CW 命中 +2
  属正常（自同步后收敛），启动时须给 `WWDETHR` 留余量——以保留计数器 8
  启动时曾直接溢出进 INIT（§7.8 反例）。

### 7.4 FWD 问答（test6/7/9/12）

* Table 26 + 顺序（RESP3,2,1→FWDRSP，RESP0→SYNC）正确：**两次 `RSPOK=1`
  并产生新题号**（QUEST 0x0→0xF）。
* 核心发现：FWD FSM 每心跳周期最多消费 1 字节——**字节间隔必须大于心跳周期**
  （600ms 心跳：750ms 间隔成功；10ms/150ms 间隔字节被丢弃且各 +2）。
  `answer/bgauto` 已实现为“心跳+100ms 自适应间隔”的后台非阻塞序列机。
* 持续运维 FWD 在数学上不可持续（每轮约 3 次 interim 到期 +2、成功 −1），
  故生产建议关闭 FWD（官方例程同样关闭），本工程保留完整手动/后台作答能力
  供研究。

### 7.5 ERR 监控（test11）

`syscfg 0 0 1 0 0`（ERREN=1，RECEN=0）+ ERR 静态高 → **INIT +
`INITERR.ERRF=0x20` + SS1 拉低**，`SYSSF/IF` 无附加位。
`err burst/auto` 翻转即模拟 SMU FSP（量产切 `IfxSmu_FSP0_P33_8_OUT`）。

### 7.6 INIT 迁移行为（多次复现，与手册差异已标出）

* WD 溢出（WWD/FWD）与 ERR 故障均进 INIT + SS1 低 + 对应 `INITERR` 位置位；
  计数器与 `*R3` 配置**保留不清零**（RWDCFG0=0x93/0x97、FWDECNT=14、
  WWDSTAT=8 在迁移后原样保留，3 次复现）——与 DS Table 22 的 `*R3` 复位标注
  不符，利于 post-mortem，软件须显式清理。
* `goto NORMAL` 当且仅当 WD/ERR 安静且标志清零才受理（2 次被拒 wolves：
  一次 FWDE pending，一次 WWDEN=1 未服务；清零+关闭后均一次成功）。

### 7.7 ROT/INT 接线验证状态（重要，MPS 行为证据）

* **6+ 次 INIT 迁移（含 3 次 WD 溢出）TC397 零复位**：`xfer` 计数连续、
  静默期串口零自发字节。唯一一次“重启”事后证实为重放的 `reset`
  软件命令（`RSTSTAT: SW=1`，ESR0/1=0）。
* 与 **MPS=1 测试模式**（DS §11.7：INIT 定时器停 + WD/ERR→ROT 阻断、
  迁移照常）完全吻合；辅证：设备可在 INIT 静置 10 分钟以上零复位。
* `GTM=0x02`（NTM=1/TM=0）**不是** MPS 指示（它是硅片 test 结构指示），
  不得作为模式判据——行为证据优先。
* INT→nESR1 有效性证据：每次 INT 事件（SPI 错/WD 错/ABIST 完成）后
  `IF.INTMISS=1`（TC397 侧无 ESR1 ISR，预期行为；配 ESR1/NMI 即闭环）。
* **未完成**：ROT→nPORST 的电连续性/脉冲需示波器复核（MPS=1 下无脉冲可测；
  MPS=0 故障注入时应能抓到；ERR 与 WD 溢出两种 move 都值得各抓一次）。

### 7.8 ABIST 与 Buck（test15/16）

* ABIST 单项（PREGOV，SINGLE=1）：`CTRL0 0x05→0x54`（STATUS=5 PASS、
  START 硬件自清）、`SEL0` 位自清、`IF.ABIST` 置位、`MONSF` 全 0、
  全程保持 NORMAL；`clear` 后归零。
* `buck set freq 0x01`（+1.5%）写回读正常并恢复 `0x00`；`wktim` 置数/清零、
  `baud` 2M↔5M、`gpio` 转储（OUT==IN 即时一致）、`rails` 均验证。

### 7.9 MPS=0 复位循环问题（用户 09-16 下午报告）→ 已修复验证

* 根因：MPS=0 时 POR→INIT 且 INIT 定时器运转、WWD/ERR 默认全开；
  旧固件启动不配置 TLF → 定时器超时软复位（ROT）→ MCU 无限重启。
* 修复：`TLF_AUTO_INIT=1`（默认），启动约 1 秒内完成配置并进 NORMAL。
* 验证（TestMode 下模拟 POR 条件：INIT + WWDEN=1 + ERREN=1 + ERRF 锁存，
  再软件复位）：启动日志 **`TLF auto-init: NORMAL ok
  (DEVSTAT 0xF9->0xFA NORMAL, SS1=1)`**，两次复现；`tlf demo` 同函数可手动重跑。
* 用户 MPS=0 复测步骤（见 §8）。

---

## 8 MPS=0（Normal）上线步骤（给用户）

1. 烧录本工程（自初始化默认开），MPS 置低后整板重新上电。
2. 串口应 1 秒内出现 `TLF auto-init: NORMAL ok (DEVSTAT 0xF9->0xFA NORMAL, SS1=1)`，
   且**不再循环重启**；随后 `tlf state` 复核（NORMAL/SS1=1/全 0）。
3. 若 `auto-init: FAILED`：用 `tlf flags/wwd status/fwd status` 找阻塞源
   （多为 WD 计数器过高或 ERR 有效），`tlf demo` 手动重跑。
4. 持续运维（二选一）：保持 WWD/FWD/ERR 关闭（当前默认， loop-free）；
   或按 §7.3 起 WWD 相位锁定喂狗 + `err auto` 翻转（台架）/ SMU FSP0（量产）。
5. 测量前先静默 30 秒确认无 USB 重放（§1.8），再用示波器抓 ROT
  （故障注入时）与 INT（`tlf abist start` 可稳定复现一次 INT 脉冲）。
6. SLEEP/STANDBY/WAKE 尚未实测（需 ENA/WAK 配合；命令已就绪，
   先读 DS §11.3.3–11.3.6 再动手）。

---

## 9 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植/驱动代码内部许可
