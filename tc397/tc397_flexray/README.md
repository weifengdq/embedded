# tc397_flexray — TC397 双 ERAY (FR0A + FR1A) FlexRay 自收发测试工程

本工程以 `tc397_uart_lettershell`（ASCLIN0 921600 + Letter-Shell + P13.0 LED）为蓝本，
新增 **ERAY0/ERAY1 双控制器 FlexRay 自测试**：两路 A 通道（FR0A/FR1A）在板上连到同一
FlexRay 总线（经 2 片 NCV7383 收发器），互相冷启动、同步并交换静态段帧，
全部经 `flexray`/`fr`/`eray` Shell 命令驱动与验证，无需外部 FlexRay 设备。

* 基线：`tc397_uart_lettershell`（UART/时钟/LED/Shell 保留，见其 README）
* 工具链：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ CMake/Ninja + TAS/DAS 8.3.0 +
  `aurix_flasher`（复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`）
* 串口：`/dev/ttyACM0`（1a86:55d3），921600-8N1；DAP MiniWiggler `058b:0043` 仅用于下载
* 实测状态：**`fr test 10` 双向 20/20 OK，PASS**（§7）；板上为 Debug 版

---

## 1 原理说明

### 1.1 FlexRay / ERAY（TC397 协议控制器）

* FlexRay 是车用确定性总线（本工程 10 Mbit/s，E-Ray IP V3.2.11，遵循 FlexRay Protocol v2.1）：
  通信按固定**周期（cycle，本工程约 5 ms）**进行，每周期分静态段（时分复用 slot）、
  动态段、符号窗、网络空闲（NIT）四部分；节点在自己拥有的静态 slot 发送，其余时间接收。
* TC397 的 FlexRay 协议控制器叫 **ERAY**，一片 TC397 有 **2 个（ERAY0/ERAY1）**，
  各带 A/B 双通道物理接口 + 8 KB Message RAM（最多 128 个 Message Buffer）。
* **POC（Protocol Operation Control）** 状态机：`DEFAULT_CONFIG → CONFIG（配 GTU/MessageRAM/引脚）
  → READY → WAKEUP → COLDSTART → RUN → NORMAL_ACTIVE`。启动一个集群至少需要
  **2 个 coldstart 节点**（互相收 startup/sync 帧对时），本工程 FR0/FR1 互为 coldstart。
* 关键寄存器（`fr regs` 可看）：`SUCC1`（命令/通道/启停配置）、`CCSV`（POC 状态）、
  `CCEV`（时钟校正错误计数）、`EIR/SIR`（错误/状态中断）、`TEST1`（引脚活动/编码错误计数）、
  `MHDS`（收发 buffer 指示）、`NDAT1`（新数据标志）、`TXRQ1`（发送请求标志）。
* 手册：TC3xx User Manual Part-2 第 41 章（已抽取 `temp/eray_ch41.txt`，148 页，不进 git）。

### 1.2 NCV7383DB0R2G（FlexRay 收发器，onsemi）

* 单通道 FlexRay 收发器，符合 Electrical Physical Layer Rev 3.0.1，最高 10 Mbit/s，
  差分 BP/BM，发送差分幅度约 0.6–2 V（40–55 Ω 负载），总线 ESD >10 kV。
* 引脚：`VIO`（数字电平适配 2.3–5.25 V）、`TxD/TxEN/RxD`（接 ERAY）、`BGE`（bus guardian 使能，
  低电平封发送）、`STBN`（High=Normal，Low=Standby 低功耗+唤醒检测）、
  `ERRN`（错误指示）、`CSN/SCK/SDO`（SPI 读状态寄存器，可不用）。
* **TrackMode vs LatchedMode**：出厂默认 Latched（经 SPI 读状态清错）；
  把 `CSN` 接地 + `SCK` 拉高保持 `dERRNModeChange` 即进 TrackMode，
  错误直接反映在 `ERRN` 引脚，无需 SPI。本板即此接法（§2）。
* 保护：TxEN 超时（>约 1.5 ms 持续使能则封发送）、过温关断（约 165 ℃）、
  VCC/VIO 欠压进 Standby、BP/BM 短路限流（约 60 mA）。
* 外围：VCC/VIO 各 100 nF 去耦；总线端 `RBUS1+RBUS2` 匹配线缆阻抗（本板两端各约 47.5 Ω，
  终端电阻已启用）；共模扼流 + 4.7 nF 共模电容（参考手册 Figure 2）。
* 手册已抽取 `temp/ncv7383.txt`（1302 行，不进 git）；
  在线：https://www.onsemi.com/products/interfaces/wired-transceivers-modems/ncv7383

### 1.3 本板接线（§2 表）

| 信号 | TC397 引脚 | ERAY 符号 | 说明 |
| --- | --- | --- | --- |
| FR0A_TXD | P02.0 | `IfxEray0_TXDA_P02_0_OUT`（alt6） | ERAY0 A 通道发送 |
| FR0A_TXEN | P02.4 | `IfxEray0_TXENA_P02_4_OUT`（alt6） | 发送使能（低=使能） |
| FR0A_RXD | P02.1 | `IfxEray0_RXDA2_P02_1_IN`（RxSel_c） | ERAY0 A 通道接收 |
| FR1A_TXD | P14.10 | `IfxEray1_TXDA_P14_10_OUT`（alt7） | ERAY1 A 通道发送 |
| FR1A_TXEN | P14.9 | `IfxEray1_TXENA_P14_9_OUT`（alt7） | 发送使能（低=使能） |
| FR1A_RXD | P14.8 | `IfxEray1_RXDA0_P14_8_IN`（RxSel_a） | ERAY1 A 通道接收 |
| 调试串口 | P14.0/P14.1 | ASCLIN0，921600 | 与 FlexRay 引脚无冲突（不同 pin） |
| LED | P13.0 | 低=亮，1 Hz 心跳 | 同 uart 基线 |

* 两路 A 通道（FR0A/FR1A）在板上连在一起自收发；B 通道未贴片，软件只配 A
 （`SUCC1.CCHA=1/CCHB=0`，RX header 只有 `channelAFiltered`）。
* 收发器 strapping（免 SPI）：`BGE/STBN/SCK`=3.3 V（Normal + 发送使能），
  `CSN`=GND（TrackMode），`SDO/ERRN` 悬空。

### 1.4 集群配置（双节点 identical）

* 10 Mbit/s，静态段 **91 slot × 24 MT**，静态 payload **8 word = 16 B**，
  动态段 289 × 5 MT，周期 3636 MT（约 5 ms），cycle filter = 1（奇周期）。
  数值沿用 `ref/uarteray` 在 TC387 验证过的 PowerTrain/BSC profile
 （`Libraries/FlexRay/flexray_dual.c:frd_fill_cluster`）。
* FR0（ERAY0）key slot **11**，FR1（ERAY1）key slot **12**（各 static TX buf0，
  continuous，startup+sync 帧，错开 slot 防碰撞；两节点都 coldstart+sync，
  谁先 wakeup 谁先 cold-start，另一方 integrate，约 2–4 s 进双 NORMAL_ACTIVE）。
* Message RAM（每节点 3 buffer，无 FIFO）：buf0=TX key slot，
  buf1/buf2=RX slot 11/12（channel A，cycle 1）。
* POC 命令一律经 **LCK 解锁的全字 SUCC1 写**（`0xCE/0x31`，见 §8.2），
  每步带 STM 超时，`fr init`/`fr test` 直接报 rc（§5）。

---

## 2 目录结构

```
tc397_flexray/
├── Libraries/FlexRay/flexray_dual.{h,c}  # 双 ERAY 驱动（frd_ 前缀；prepare/startup/send/poll/status/view）
├── Shell/shell_flexray.c                 # flexray|fr|eray 命令（init/status/send/recv/test/regs/pins/txact/txview）
├── Shell/shell_port.c                    # Shell_Get 直接句柄（见 §8.1）+ 原有 mcu/temp/led 等命令
├── Cpu0_Main.c                           # 基线 + frd_poll() 后台轮询 + 直接打印 banner（见 §8.1）
├── build/gcc/tc397_flexray.{elf,hex,map} # 本地构建产物（不进 git）= 板上 Debug 版
└── 其余同 tc397_uart_lettershell（UART/STM/DTS/Lcf/CMake/build.sh/serial_monitor.py）
```

`ref/uarteray`（3150 行）/`ref/mcutc3` 仅借了集群时序数值、LCK 全字写、
POC 状态值表等经验；本工程驱动为重写（`frd_`），无 UART 二进制桥接协议，
命令全部是可读 Shell 文本。

---

## 3 构建与下载（Ubuntu）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_flexray
./build.sh build                        # Debug（text ~86K，hex ~300K）
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 经 flasher -read 复位并恢复运行
```

* 下载后串口约 15 s 二进制乱码风暴（CH340/USB 毛刺，tlf 工程同源记录），自清除后再测；
  首条命令若被风暴吞掉属正常，重发即可。
* `ModemManager` 会抢 `/dev/ttyACM0`（AT 探测吞字节、报 device disconnected），
  已 `sudo systemctl disable ModemManager`（§8.5）。
* 不要用 `aurix_flasher -read <DSRAM/SCU 地址>` 查运行状态——热挂接读会复位/挂起 CPU
  （§8.5）；以串口为准。

---

## 4 串口与 Shell

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw
python3 /tmp/cap.py "fr init" "fr test 3"   # 复位+抓启动窗口+依次执行（日志 temp/bootcap.log）
```

| 命令 | 说明 |
| --- | --- |
| `fr`（= `flexray` = `eray`） | 无参数=用法 |
| `fr init` | 双节点阻塞 bring-up（HALT→CONFIG→READY→WAKEUP→COLDSTART→RUN→NORMAL），打印每节点 rc/POC/寄存器 |
| `fr status` | 双节点 POC/CCSV/SUCC1/EIR/SIR + 收发计数 + MBS 快照 |
| `fr send <0\|1> [hex]` | 发一静态帧（默认 seq 递增 pattern；`fr send 0 001122...` 自定义 16 B） |
| `fr recv <0\|1>` | 看某节点最后收到的有效帧（slot/cycle/len/hex） |
| `fr test [n]` | 自动双向回环（默认 5）：FR0→FR1 slot11 原样 + FR1→FR0 slot12 按位取反，逐轮比对 |
| `fr regs` | 原始寄存器（SUCC1/CCSV/CCEV/EIR/SIR/TEST1/MHDS/ACS/FSR/NDAT1/TXRQ1） |
| `fr pins` | P02/P14 复用（IOCR）+ 电平粘滞采样（TXD/TXEN/RXD 空闲应全高） |
| `fr txact` | 全周期轮询 TEST1 活动位（AOA/RXA/TXA/TXENA）+ CERA/CERB 编码错误计数 |
| `fr txview <0\|1> [buf]` | OB VIEW 看 message RAM（header/CRC/DP/MBS/MHDS/数据，诊断用） |
| `help/mcu/temp/led/...` | 基线命令（`help` 内建命令因 §8.1 原因可能无响应，用 `fr` 系 + `mcu` 即可） |

---

## 5 bring-up 步骤与 rc 表（`fr init` 失败时看这里）

Phase A（逐节点到 RUN）→ Phase B（双节点联合等 NORMAL，12 s）。
`FrBootInfo.rc`（`fr init` 打印 `rc=-N@STEP`）：

| rc | STEP | 含义 |
| --- | --- | --- |
| 0 | OK | 到达 RUN（Phase A）/双 NORMAL（Phase B 成功） |
| -1 | HALT | FREEZE 后 300 ms 未进 HALT（含 PBSY 卡死） |
| -2 | DEFAULT_CONFIG | CONFIG 命令后 300 ms 未进 DEFAULT_CONFIG |
| -3 | CONFIG | 同上未进 CONFIG |
| -10 | APPLY | message RAM setSlot（IBBS 卡死，100 ms 超时） |
| -4 | READY | READY 后 300 ms 未进 READY |
| -5 | WAKEUP | WAKEUP 命令被拒（SUCC1.CMD 回读≠3，见 §8.2） |
| -6 | WAKEUP_WAIT | 1500 ms 未见 LISTEN/SEND/DETECT（集齐 `{1,16,17,18,19}`） |
| -7 | COLDSTART | ALLOW_COLDSTART 被拒 |
| -8 | RUN | 500 ms 内 RUN 未被接受 |
| -9 | NORMAL | 12 s 联合等待未双 NORMAL（看双方 POC/CCSV/EIR 定位） |

正常 `fr init`（§7 日志）：双 `rc=0@OK poc=2(NORMAL_ACTIVE)`，
`SUCC1=0x04004304`（bring-up 末）→ `0x04004305`（ALLSLOTS 后，CMD=5），
`EIR=0x00000016`（启动期瞬态，见下）随后清零。

---

## 6 测试方法

1. 烧录 → 等风暴自清除（约 15–20 s）→ `fr init`（双 NORMAL_ACTIVE）。
2. `fr pins`（复用正确：P02.0=0xB0/alt6，P14.9/10=0xB8/alt7，RXD 输入；电平全高）。
3. `fr test n`：每轮 A 向（FR0 slot11 发 `r*16+i` 递增 16 B）+ B 向
   （FR1 slot12 发按位取反），200 ms 窗口轮询比对整帧；计数器汇总。
4. `fr regs` 复核（EIR 应回 0，CERA/CERB 恒 0，NDAT/TXRQ 语义见 §8）。
5. 日志存 `temp/fr_*.log`（不进 git）。

---

## 7 测试结果（2026-09-16 bench）

* `fr init`：FR0/FR1 均 `NORMAL_ACTIVE`（CCSV POCS=2），可重复；
  启动期 `EIR=0x00000016` = CNA（bit1，命令曾被拒）+ SFBM（bit2，sync 帧不足，
  对端未起）+ CCF（bit4，时钟校正失败，集成期瞬态），随后 `EIR=0` 不再出现。
* `fr test 3`：**PASS（3 轮，0 fail）**（`temp/fr_test9.log`）。
* `fr test 10`：**PASS（10 轮 20 向，0 fail）**（`temp/fr_test10.log`）：
  `txOk=13 txBusy=0 rxErr=0`，`CERA/CERB` 恒 0（物理层干净），
  数据重复发送（continuous，见 §8.4），字节级比对全对。
* `fr send/recv` 手动路径验证 OK（`temp/fr_send1.log`）。
* `fr pins`：复用与电平符合预期（§4）。
* `fr txact`：AOA=1（总线活动），CERA/CERB 0→0 无增长。

---

## 8 移植与调试笔记（坑位汇总，必读）

### 8.1 letter-shell `shellGetCurrent()` 在本镜像不可用（已绕过）

* 现象：启动死在 `Shell_PrintBanner()` 内第一行 `shellPrint`；`help` 等内建命令无响应；
  而直接 `IfxAsclin_Asc_write` 与（不依赖 GetCurrent 的）命令派发正常。
* 根因未完全定位（疑为新增段与 BSS/命令表共同作用，`shellList` 状态异常；
  同一份 letter-shell 在 uart 基线工作正常）。
* 处理：`Shell/shell_port.c` 新增 `Shell_Get()`（`Shell_Init` 置 `sShellSelf=&gShell`），
  本工程全部自有命令（`fr/*` + `mcu/temp/led/...`）改用它；
  `Cpu0_Main.c` 用直接打印代替 `Shell_PrintBanner()`。
  **后果**：`help` 等 letter-shell 内建命令仍可能无响应——用 `fr status`/`mcu` 代替。

### 8.2 SUCC1 POC 命令：LCK 全字写 + CMD 回读语义

* CONFIG/READY 下 plain 位写 SUCC1.CMD 被静默忽略；必须
  `读 SUCC1 → 改 CMD → LCK=0xCE/0x31 → 全字写回`（参考工程已证实唯一有效路径）。
* `SUCC1.CMD` 是 sticky 的：显示**最后下发的命令**；iLLD `changePocState`
  以“回读 == 下发值”为接受（回读 0 = 被拒）。本驱动 `frd_try` 照此实现，
  且写后先等 `PBSY` 清零再判（否则误判拒绝，血泪史）。
* POC 状态值用 iLLD 枚举（CONFIG=15，WAKEUP_LISTEN=17/SEND=18/DETECT=19，
  STARTUP=32…，NORMAL=2/3），不要按 0–13 顺排臆测。

### 8.3 接收头 NFI 极性（最贵的一个教训）

* `IfxEray_ReceivedHeader.nullFrameIndicator`：**1 = 数据帧，0 = 空帧**
  （iLLD 原文：`0: no data frame received; 1: at least one data frame received`）。
* 而 `MBS.NFIS`（bit27）语义相反：1 = 空帧。两者不要混用。
* 本工程曾因此把双向数据流全部判成空帧，`fr test` 全 FAIL；
  修正后 20/20 OK。请以后人以 iLLD 注释为准，不要望文生义。

### 8.4 发送与空帧行为

* 稳态发送只刷数据 + TXRQ（`LHSH=0`）：CONFIG 外传 header（`LHSH=1`）直接
  `EIR.IIBA` 非法（已验证 `0x216`）。TXRQ 状态可读 `TXRQ1`（bit0=buf0）。
* TX buffer（continuous）在无 TXR 时每周期自动发**空 startup/sync 帧**
  （SFI/SYN 置位，NFI 空，payload 为 RAM 残留—— peer 的 `rejD0` 曾抓到）。
  收到空帧属正常（时钟同步维持），`fr test` 只比对数据帧。
* `MLST`（message lost，被覆盖） benign：轮询慢于周期时必然出现，
  当前内容仍是最新帧，只做统计不做拒绝；真错（SEOA/CEOA/SVOA/TCIA）才拒绝。
  本 bench `rxErr`（真错）恒 0，`CERA/CERB` 恒 0。
* 主循环 `frd_poll()` 常开轮询（无 NDAT 门控，只计内容变化），
  避免 NDAT 清除语义坑。

### 8.5 环境坑

* `ModemManager` 抢串口：已 `sudo systemctl disable ModemManager`。
* `aurix_flasher -read <addr>` 热挂接会复位/挂起 CPU（`g_TickCount` 回落/冻结皆为此所致），
  不要拿它做运行态诊断；`build.sh reset/download` 尾部的 `-read 0x80000000` 是官方复位路径。
* 烧录后 ~15 s 乱码风暴：等自清除；与 bench EMI/CH340 毛刺同源（tlf 工程有相同记录）。
* 串口被占用时（本机另一进程或他人会话）表现为零字节/截断——先 `fuser -v /dev/ttyACM0`
  与独占打开（`exclusive=True`）确认。

---

## 9 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
