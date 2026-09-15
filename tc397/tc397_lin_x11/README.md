# tc397_lin_x11 — TC397XX 11路 ASCLIN LIN (19200, 成对主从) + Letter-Shell 收发测试

`tc397_uart_lettershell` 拷贝而来，新增 11 路 LIN 驱动与主从测试命令。
目标 **TC397XX 292pin**，调试串口 **ASCLIN0 TX P14.0 / RX P14.1, 921600-8N1**，
LED **P13.0（低电平点亮）**。LIN 通道两两成对组成 LIN 总线（见 §1），
任意一侧可配成 master，另一侧为 slave，做双向收发测试。

* 驱动：iLLD `IfxAsclin_Lin`（`Asclin/Lin/IfxAsclin_Lin.[hc]`），**纯轮询、无新增中断**
  （`Configurations/ConfigurationIsr.h` 未动）；iLLD 阻塞式 API 不适用于单核多节点
  脚本测试，`App/lin12.c` 在 `IfxAsclin_*` 标志操作上自研了 tick 超时保护的事务引擎
* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
  （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 收发器手册已转 txt（不进 git）：`tc397/ref/tlin1024a-q1.txt`（主 datasheet，2106 行）、
  `tc397/ref/tlin1024a_sffs084.txt`（功能安全件，430 行）
* 横向参考（不进 git）：`tc397/ref/uart8lin`（TC387 8路 LIN 网关，测试通过的版本；
  其 `lin_enable_polling_flags`（轮询模式下显式使能 LIN 事件标志）与
  `disableModule`-before-reinit 已被本工程采纳）

---

## 1 硬件

| 逻辑 | TX | RX | ASCLIN | 收发器使能 | 总线 |
| --- | --- | --- | --- | --- | --- |
| LIN0 | P14.0 | P14.1 | ASCLIN0 | SLP_N P02.8 — **仅占位**：与 UART0 调试串口引脚冲突，不初始化、不测试 | — |
| LIN1 | P15.4 | P15.5 | ASCLIN1 | SLP_N P02.8=H（与 LIN0/2/3 共用） | 单独（见下） |
| LIN2 | P10.5 | P02.10 | ASCLIN2 | SLP_N P02.8=H | 对线 B |
| LIN3 | P20.0 | P20.3 | ASCLIN3 | SLP_N P02.8=H | 对线 B |
| LIN4 | P00.9 | P00.12 | ASCLIN4 | SLP_N P00.11=H（与 LIN5/6/7 共用） | 对线 C |
| LIN5 | P00.7 | P00.6 | ASCLIN5 | SLP_N P00.11=H | 对线 C |
| LIN6 | P23.5 | P23.1 | ASCLIN6 | SLP_N P00.11=H | 对线 D |
| LIN7 | P22.1 | P22.4 | ASCLIN7 | SLP_N P00.11=H | 对线 D |
| LIN8 | P34.5 | P33.6 | ASCLIN8 | SLP_N P00.10=H（与 LIN9/10/11 共用） | 对线 E |
| LIN9 | P01.7 | P20.6 | ASCLIN9 | SLP_N P00.10=H | 对线 E |
| LIN10 | P00.8 | P00.4 | ASCLIN10 | SLP_N P00.10=H | 对线 F |
| LIN11 | P21.0 | P21.1 | ASCLIN11 | SLP_N P00.10=H | 对线 F |
| UART | P14.0 | P14.1 | ASCLIN0 921600 | — | — |
| LED | P13.0（低=亮） | — | — | 心跳 1Hz，可 `led hb off` 关 | — |

引脚逐一核对过 `IfxAsclin_PinMap_TC39xB_LFBGA292.{h,c}`（见 `App/lin12.c` 注释；
RxSel a~g 即 ALTI 0~6，驱动里由 pin 结构 `select` 字段直接推导，不手写硬编码表；
TX 的 ALT（P00.8/alt3、P00.9/alt5 等）亦逐个核对，`linreg` 可回读 PCR 确认）。
3 片 TLIN1024A-Q1（Quad LIN，每片 4 通道）：芯片 1 = LIN0~3，芯片 2 = LIN4~7，
芯片 3 = LIN8~11。板上 SLP_N 即收发器 EN（高=Normal，低=Sleep；datasheet §9.3.6/§9.4），
4 通道 EN 连在一起由 1 个 MCU GPIO 控制。`lin_xcvr_init()` 上电即置 3 组 SLP_N 为高。

**总线拓扑（2026-09-15 用户改为 3 总线；`lindomf` 实测结论）：**
期望：A=(1,2,3,m3)、B=(4,5,6,7,m7)、C=(8,9,10,11,m11)。
实测：新跳线**时通时断**（同一命令不同时间结果不同，固件未变）——(2,3)、(4,5)、
(8,9)、(10,11) 基本稳定，(6,7) 曾断开又恢复，A/B/C 的跨组连接基本不通。
`linpair` 结果与实测拓扑完全吻合。**跳线接触不良是当前第一问题**（杜邦线/面包板
类连接在 TLIN 总线侧不可靠；万用表静态量电平正常不代表接触良好）。
LIN1 之前落单（对端 LIN0 与 UART0 冲突）；LIN11 不再是唯一的 master——
每条总线各配一个 master（3/7/11），测试侧用 `linrole`/`linsend` 动态指定。

> 注意（commander 上拉）：按 TLIN1024A-Q1 §9.3.1/§9.3.1.2.1，responder 靠内部
> 45kΩ 上拉即可，**commander 节点必须外加 1kΩ + 串联二极管到 VSUP**。
> 本板对线均无 commander（全 responder 上拉），测试通过，但隐性上升沿偏缓；
> 正式 LIN 一致性测试前应按规范补上。

---

## 2 目录结构

```
tc397_lin_x11/
├── App/lin12.[hc]      # 驱动：tick超时事务引擎/成对事务(m2s/s2m)/PID/EN控制/角色切换/诊断探针
├── Shell/shell_lin.c   # linsend/linreq/linpair/linslv/linrole/linstat/lindump/linbaud/...
├── Shell/shell_lin.h   # 空头文件占位（命令由 LETTER-SHELL 自注册）
├── Cpu0_Main.c         # STM 1ms + P13.0 + UART/Shell/DTS + EN/LIN 初始化 + 主循环
├── Configurations/…    # 同 uart_lettershell（中断配置未动，LIN 用纯轮询，无新增中断）
├── ...                 # 其余同 tc397_uart_lettershell（iLLD/Lcf/CMake/build.sh）
└── build/gcc/tc397_lin_x11.{elf,hex,map}
```

---

## 3 构建与下载（Ubuntu）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_lin_x11
./build.sh build                        # Debug
./build.sh build --build-type Release
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 硬复位（flasher）
```

产物 `build/gcc/tc397_lin_x11.{elf,hex}`（`build/` 已忽略，不进 git）。
当前：Debug `text ~97k data ~7k bss ~72k`（基线 uart 版为 `text 82718`）。

下载说明：DAP miniWiggler `058b:0043`；若 TAS 未启动，先
`sudo systemctl start tas-server`（`ss -tlnp | grep 24817`，
`./aurix_flasher -id list` 应识别 `TC39x … TriBoard TC2XX V2.0`）。

---

## 4 串口与 Shell

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw
```

启动尾部：

```
TC397 LIN x11 + Letter-Shell
Board: TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600)
Type 'help' for commands, 'linstat' for LIN, 'linpair' for master/slave test
ChipID: 0xAF239793 CHREV=0x13
...
LIN 11ch init done: 19200 master=LIN11 slaves=LIN1..10 (LIN0 placeholder)
```

### LIN 命令（测试类）

```
linsend <txch> <id> [hex] [classic]  成对发送：txch自动升master，对端校验header+response
  e.g. linsend 11 0x12 A0A1A2A3A4A5A6A7
linreq <mch> <sch> <id> [len] [hex]  从机应答：mch发header（master），sch发response
  e.g. linreq 11 10 0x20 8 1122334455667788
linpair [rounds] [len]               5对线双向循环（默认1轮8B）
linslv [rounds] [len]                每对由后者向前者应答（一轮5次）
linrole <ch 1..11> <M|S>             单通道重配角色（重配后用linstat确认MS位）
linstat                              计数+错误列+EN电平+波特率（LIN0显示placeholder）
lindump [ch|all]                     每个通道最后一次校验通过的帧
linbaud <2400|4800|9600|10417|19200> 全通道重配波特率（默认19200）
linslp <group 0..2|all> <0|1>        EN控制：1=normal(H)，0=sleep(L)
linwake                              3组EN全部回normal
linerr <parity|cksum|timeout>        错误注入（以对线F即11→10为例）
linana [rounds] [timeoutMs]        外部分析仪全接收：LIN1~11全从，嗅探外部主帧
  e.g. linana 4  (分析仪1帧/s：ID0x17/2B经典22 33 + ID0x31/8B增强11..88)
linpas [ms]                        被动普查：GPIO采样全部RX电平/边沿（与波特率无关）
  e.g. linpas 4000
```

### LIN 命令（诊断类，无需对端配合）

```
linbb <ch> [id]     STM精确定时bit-bang header灌进某通道RX脚，验证ASCLIN收通路
lindom <ch>         TXD拉显性，采样全部RX电平（判断哪些芯片共线；300ms看DTO释放）
lindomf <ch>        快速版（5ms窗口）+ TX自回读 + 保持期IOCR/IN/OUT快照
linbusact [pid]     master发header同时采样TX/RX引脚（判定MCU翻转/总线跟随）
linact [master]     真header期间11路RX边沿计数（AC普查）
linpulse <tx> [rx]  RX脉冲时序（STM时间戳，100MHz），对比两路波形
linforen [tx] [rx]  RHE-only轮询 + FLAGS快照（取证：FED/RED/BD/HT/LC…）
lintrace [master]   THRQS后FLAGS/TXFIFO时间序列（定位THE不起等发射故障）
linloop [id]        LIN11自环回（TXD→TLIN→总线→TLIN→RXD，header+response）
linresptst          无header的response上总线测试（注意：TRRQS需header先行状态）
linreg              PCR/IN/OUT + ASCLIN LINCON.MS/FRAMECON/BRG/BITCON/FEN回读
```

UART/LED 旧命令（`help/version/mcu/uid/uptime/reset/temp/sysinfo/mem/led`）保持不变。

---

## 5 LIN 特性（本工程覆盖点）

* 帧结构：Break（master 发 **16 bit** 显性，见 §7.1）+ Sync `0x55` +
  PID（含 P0/P1 奇偶，`lin_pid()` 按 LIN 规范生成，HW 在 RX 端校验，错则 LP 标志）+
  Data 1~8B + Checksum（HW 自动生成/校验，`csEnable=TRUE`）。
* 校验和：ID 0x00~0x3B 用 enhanced（含 PID），0x3C/0x3D 用 classic（不含 PID）；
  `linsend … [classic]` 可强制；`linerr cksum` 演示失配时的 LC 标志。
* 波特率：默认 19200（iLLD LIN 默认值；TLIN1024 收 ≤100k，规范发送 ≤20k）；
  `linbaud` 支持 2400/4800/9600/10417/19200（从机 ABD 关闭，定波特率；
  `prescaler=4/OS16` 由 FDR 自动算）。4800/9600/19200 下通过项完全一致。
* 响应空间/超时：header 与 response 之间为亚 ms 级（单核顺序执行），远小于
  response-timeout 窗口（`DATCON.RESPONSE=255`，frame-timeout 模式）；
  无应答时报 response-timeout（`linerr timeout`）。
* 收发器：EN 高=Normal（TXD→LIN，LIN→RXD），EN 低=Sleep（驱动关、RXD 浮空，
  弱上拉；datasheet Table 9-1）；TXD 显性超时 DTO（tDST=20~80ms，`lindom`
  300ms 采样可观察释放）；bus stuck-dominant 上电进 sleep 时有误唤醒锁止（§9.3.9）。
* 诊断帧：0x3C（master-req）/0x3D（slave-resp）按 classic 校验，可用
  `linsend 11 0x3C …` / `linreq 11 10 0x3D …` 走一遍（本工程不实现 NAD/服务层）。
* 休眠/唤醒：`linslp <g> 0` 进 sleep（对应组收发器关断），`linwake` 回 normal；
  LIN 总线唤醒（显性 250µs~5ms）由收发器硬件完成，MCU 侧表现为恢复通信（见 §6）。
* 中断/轮询：纯轮询（CONFIGURATIONISR 未动）；但按 uart8lin 参考经验，
  **轮询模式也必须显式使能 LIN 事件标志**（`FLAGSENABLE` 的 RHE/RRE/THE/TRE
  及各错误位），否则从机 RHE/RRE 永不锁存——本工程 `lin_enable_polling_flags()`
  已处理（无此修复时全员 header 超时，见 §7.2）。
* 全接收（外部主）：`linana` 把 LIN1~11 全置 slave，用 **raw-monitor 方式**
  嗅探外部主帧（照抄已通过测试的 TC387 `uart8lin` 参考：`DATLEN=9` + 硬件校验关 +
  response-timeout 模式 + FIFO 排空收数，不预设长度/校验；`lin_sniff_ext_frame()`，
  退出前恢复常规寄存器）。已知分析仪帧：ID `0x17`/PID `0x97`/classic/2B `22 33`
  （线字节含校验共 3B：`22 33 AA`）；ID `0x31`/PID `0xB1`/enhanced/8B
  `11 22 33 44 55 66 77 88`（线字节共 9B，校验 `E7`）；校验用 `lin_checksum()`
  软件核对。`linpas` 为波特率无关的 GPIO 被动普查（分析仪帧若上总线则必有边沿）。

---

## 6 实测步骤与结果（DAP miniWiggler + TAS，板上 Debug 版，2026-09-15）

标准测试流程（一键脚本 `tc397/temp/test_lin.py`，日志 `tc397/temp/linx11_test.log`，不进 git）：

| # | 命令 | 结果（2026-09-15，新拓扑） | 说明 |
| --- | --- | --- | --- |
| 1 | 上电启动日志 | 通过 | `masters=3/7/11`；ChipID `0xAF239793`，DTS ~51℃ |
| 2 | `linpair 1 8` | **4/16 通过** | 通过：3→2、7→6、7←6、11→10；其余见 §7（跳线时断 + 硬故障） |
| 3 | `linreq` 系列 | 同 pair 结论 | 从机应答方向取决于对端 TX（10/4/5 的 TX 不发射） |
| 4 | `linslv 1 8` | 0/5（本次） | 均卡在 header-TX 侧；历史对线拓扑下 6←7、8←9 曾通过 |
| 5 | `linbaud` 4800/9600/19200 | pattern 一致 | 波特率无关性（排除 slew 主因，指向开路/器件） |
| 6 | `linslp/linwake` + `linsend` | 通过 | sleep 组 FAIL（timeout 涨）→ wake 后 PASS，符合预期 |
| 7 | `linerr parity/cksum/timeout` | 通过（11→10 定向） | 坏 PID/LC 失配/无应答超时 |
| 8 | `linsend 11 10 0x3C` | 通过（classic） | 诊断帧 master-req（classic 默认已修） |
| 9 | `linbb 1/3/4/5/10/11` | 通过 | GPIO bit-bang 自证全部从机 ASCLIN 收通路（含 11 切 slave 后） |
| 10 | `lindomf` 全扫 + `linact/linpulse` | 通过 | 总线分组/DTO/边沿/脉冲时序（LIN10 与 LIN11 波形一致） |
| 11 | `linana 2`（外部分析仪全接收，2026-09-15） | **未收到：TIMEOUT** | 分析仪标称19200/1帧/s发送0x17+0x31；11路全从raw嗅探6s无header（`hdr=0`）；见下 |
| 12 | `linpas 4000`（被动普查，同上） | **总线空闲** | 11路RX零边沿、everLow全0（与波特率/ASCLIN配置无关的GPIO实测） |
| 13 | `linsend 11 10 0x12 …`（同窗口对照） | 通过 | 板级收发通路正常（raw改动未破坏成对事务） |

**2026-09-15 外部分析仪全接收结论（原始日志 `tc397/temp/linana_test.log`，不进 git）：**
固件已对齐已通过测试的 TC387 `uart8lin` 参考（raw-monitor：DATLEN=9/硬件校验关/
response-timeout 128/软件核对校验），`linsend 11→10` 同窗口 PASS 自证板级通路。
但 `linpas 4000`（4s ≈ 4 帧）11 路 RX **零边沿** + `linana` 2 轮 **TIMEOUT** +
`linact 11` 仅 (10,11) 有边沿（其余 9 路 0）：分析仪帧在此窗口内未到达 MCU 引脚。
GPIO 采样绕过 ASCLIN（`linact` 自证采样有效），故与从机配置/波特率/校验模式无关。
请检查分析仪实际驱动的物理总线与 11 路 LIN 网的连接点（哪路/哪个连接器、共地、
分析仪侧 TX 指示/ACK 报错），确认后再跑 `linana 4`（期望每轮 `hdr=11/11 resp=11/11`
+ `22 33 AA` / `11..88 E7` MATCH）。

注意事项：

* LIN 是单主总线——同一时刻只能有一个事务在跑；`linsend/linreq/linpair/linslv`
  均为同步阻塞命令（最长约 1s，超时有 tick 保护，不会卡死 shell）。
* `linpair`/`linslv` 的 ID 从 0x10/0x20 起按序分配，避免与诊断帧混淆；
  每个事务内 header 与 response 间隔为亚 ms 级，符合 LIN 响应空间要求。
* sleep 组含 master（group2：LIN8~11）时全 FAIL 属正常：master 自身收发器已关断。
* SW 复位后行为：应用复位保持 RAM（`uptime`/tick 不清零，uart 基线亦如此）；
  `lin12_init_all()` 每次全量重配（含计数清零），无 CAN 版曾遇到的陈旧标志问题。
* LIN0（ASCLIN0）与 UART0 调试串口复用引脚，硬件上 LIN0 通道不可用；
  `linstat` 中 LIN0 恒显示 placeholder，任何 `lin*` 命令都不触碰 ASCLIN0。
* 计数器跨命令累积（不清零）；要干净基线请 `reset` 重启后测，或对比 `linstat` 差值。

---

## 7 已知问题与处理（重点，含根因分析过程）

### 7.1 iLLD 默认 13-bit break 在本板处于检测门限边上（已修复，根因）

现象：最初全员 header 超时、从机零错误标志。`linforen` 取证：`FLAGS=0x60`
（仅 FED/RED 边沿，无 BD/RHE/HT/FE/LP），而 14-bit 的 GPIO bit-bang（`linbb`）
一次通过。结论：master 的 13-bit break 处在从机检测门限边上（FDR 取整误差 +
TLIN 边沿整形）。
修：master break 生成 16 bit，从机检测门限 11 bit（均在 LIN 规范窗口内），
`App/lin12.c::lin_init_channel`（`cfg.lin.breakLength`）。修复后对线 F 立即 PASS。

### 7.2 轮询模式必须显式使能 LIN 事件标志（已修复，根因）

iLLD `initModule` 仅在中断模式下写 `FLAGSENABLE`，轮询模式保持全 0，
此时 RHE/RRE 等永不锁存（THE/TRE 不受影响——这曾误导排查）。
修：`lin_enable_polling_flags()`（照抄 TC387 uart8lin 参考实现），init 后调用。

### 7.3 总线拓扑：3 总线目标，实测跳线时通时断（重点）

2026-09-15 用户改为 3 总线：A=(1,2,3,m3)、B=(4,5,6,7,m7)、C=(8,9,10,11,m11)，
固件已按分组重构（`linGroups`/组播 `linpair`/`linslv`，master 3/7/11）。
但 `lindomf` 显性跟随实测：同一命令在固件未变的相邻时间多次运行结果不同——
`lindomf 7` 曾只跟随 LIN7 自身，数分钟后跟随 LIN6+LIN7；
`lindomf 10` 在 {10,11}→{}→{11} 之间翻转。
结论：新跳线**接触不良（时通时断）**，(6,7) 的旧跳线也被拆掉了；
万用表静态电平正常不代表接触可靠。`linpair` 的 4/16 通过与实测拓扑完全吻合。
请硬件侧：重插/更换全部 LIN 总线跳线（杜邦线/面包板触点），用 `lindomf <m>`
（期望跟随该总线全部成员）逐条验收，验收通过后再跑 `linpair`（期望全 PASS）。
此期间历史结论（对线时代 6/10 方向 PASS）依然有效，见 git 历史与 temp 日志。

### 7.4 ASCLIN4/5/10 的 TX 不发射（硬件故障候选，固件侧已穷尽）

现象：这三个模块 header TX 的 THE 永不置位（50ms tick 超时），无任何 HW 错误标志；
TXFIFOCON 显示 outlet 已开、FIFO 有数（`lintrace`：fill=1 恒定，HT 在 ~3ms  firing）。
已排除：引脚（22 个符号逐个核对 + `linreg` PCR 回读 ALT 正确：P00.8/alt3=0x98、
P00.9/alt5=0xA8、P00.7/alt2=0x90）、SFR（CLC/TXFIFO/BRG/BITCON/DATCON/LINCON/CSR/
FRAMECON/FEN 与正常模块逐字相同）、时钟（同模块 RX 经 `linbb` 与真实业务验证正常）、
GPIO（同引脚推挽翻转+自回读正常）、初始化（`disableModule`-before-reinit、
全量重配、角色 MS 位硬件回读翻转正常）、复位（应用/系统复位 + **彻底断电冷启动
（Tick: 0）后依旧**）。
OSC/电源/接线经用户确认无问题、不便再用示波器。请硬件侧核查：P00.7/P00.8/P00.9
三线（是否桥接/虚焊/被钳位）与对应 TLIN 通道 TX 通路；必要时换片验证。
 topology 影响：凡以 4/5/10 为 header-发送侧的方向均 FAIL
（2→3 除外，见 7.5；10→11、4→5、5→4、4/5 参与的 s2m）。

### 7.5 LIN3/LIN4/LIN5 接收侧模拟失真（硬件故障候选）

`linpulse`/`linact` 量化：LIN2 发 header 时 LIN2 RXD 得 18 个边沿（完美时序），
LIN3 RXD 得 0~2 个（仅 break 量级能过）；LIN5 发时 LIN5 RXD 得 2 个、LIN4 RXD 得
38 个（抖动）；DC 显性钳位三者皆跟随。签名 = RXD 网络低通（τ≈1ms 量级），
4800/9600/19200 波特率无关（已三档验证 pattern 一致），固件无法补偿。
请硬件侧核查这三路 RXD 上拉（开漏 RXD 需上拉到 I/O 电平；弱上拉 + 长线电容 =
慢上升）与走线。注意 `linbb`（GPIO 直灌）在这三路均 PASS，即 ASCLIN 收通路无辜。

### 7.6 LIN1 与 LIN0

LIN1 单节点验证通过（`linbb 1` RHE+PID、`lindomf 1` TXD 显性跟随本通道、
`linact` 本通道边沿正常）。LIN0 收发器通道 EN 与 LIN1~3 共组常开，但其 TXD
接的是 UART TX——若 LIN0 与 LIN1 共线，UART 流量会污染 LIN1；bus 侧按对线
使用时 LIN1 无可用对端，仅作单体自检。

### 7.7 调试探针自身的坑（已修复，教训）

`IfxPort_getPinState()` 返回 boolean（0/1），绝不能拿它跟 `IfxPort_State_low`
（0x10000）比——恒为假。本工程早期所有 GPIO 采样（`linbusact`/`lindom`初版）
因此全读高，误导了排查；反汇编定位后已改为与 `FALSE` 比较。
另：`linstat` 计数器跨命令累积，复位（`reset`）重启才清零；`uptime`/tick 在
应用复位后不清零（RAM 保持，基线行为）。

---

## 8 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* can-utils/LIN 参考（uart8lin）：仅借鉴驱动做法，实现为自研代码
* 其余移植代码内部许可
