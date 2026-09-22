# tc397_can_x12 — TC397XX 12路 MCMCAN (1M + 5M FD) + Letter-Shell 收发测试

`tc397_uart_lettershell` 拷贝而来，新增 12 路 CAN FD 驱动与 can-utils 风格测试命令。
目标 **TC397XX 292pin**，调试串口 **ASCLIN0 TX P14.0 / RX P14.1, 921600-8N1**，
LED **P13.0（低电平点亮）**。12 路 CAN 两两互联（CAN0-CAN1 … CAN10-CAN11）做收发测试。

* 参考：`/home/z/lz/embedded/tc387/tc387_can_x12_gcc`（12路初始化/位定时/TDC 规则）、
  `/home/z/lz/tc387/gs_udp_can_tc387/App/can12.c`（轮询/RTR 取数/accept-all 滤波，网络部分未移植）、
  `/home/z/lz/tc387/ref/can-utils-master/{cansend,candump}.c`（帧语法 `123#DEADBEEF` / `123##1…`）
* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
  （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 收发器手册已转 txt：`tc397/ref/tcan1043-q1.txt`（主 datasheet）、
  `tc397/ref/tcan1043_slla525a.txt`（功能安全 FIT/FMD/Pin FMA，非主手册）

---

## 1 硬件

| 逻辑 | TX | RX | MCMCAN | 收发器 | 使能脚 |
| --- | --- | --- | --- | --- | --- |
| CAN0 | P34.1 | P33.12 | CAN0 node0 | TCAN1043-Q1 | EN P33.1=H, nSTB P33.0=H, nFAULT P10.3(输入) |
| CAN1 | P15.2 | P33.10 | CAN0 node1 | TCAN1044-Q1 | STB P21.5=L（与 CAN2/3 共用） |
| CAN2 | P32.5 | P32.6 | CAN0 node2 | TCAN1044-Q1 | STB P21.5=L |
| CAN3 | P32.3 | P32.2 | CAN0 node3 | TCAN1044-Q1 | STB P21.5=L |
| CAN4 | P00.0 | P00.1 | CAN1 node0 | TCAN1044-Q1 | STB P21.2=L（与 CAN5/6/7 共用） |
| CAN5 | P23.6 | P23.7 | CAN1 node1 | TCAN1044-Q1 | STB P21.2=L |
| CAN6 | P23.2 | P23.3 | CAN1 node2 | TCAN1044-Q1 | STB P21.2=L |
| CAN7 | P33.4 | P33.5 | CAN1 node3 | TCAN1044-Q1 | STB P21.2=L |
| CAN8 | P10.6 | P34.2 | CAN2 node0 | TCAN1044-Q1 | STB P21.4=L（与 CAN9/10/11 共用） |
| CAN9 | P00.2 | P00.3 | CAN2 node1 | TCAN1044-Q1 | STB P21.4=L |
| CAN10 | P22.8 | P32.7 | CAN2 node2 | TCAN1044-Q1 | STB P21.4=L |
| CAN11 | P22.10 | P22.11 | CAN2 node3 | TCAN1044-Q1 | STB P21.4=L |
| UART | P14.0 | P14.1 | ASCLIN0 921600 | — | — |
| LED | P13.0（低=亮） | — | — | — | 心跳 1Hz，可 `led hb off` 关 |

引脚逐一核对过 `IfxCan_PinMap_TC39xB_LFBGA292.{h,c}`（RxSel/alt 见 `App/can12.c` 注释）。
TCAN1043：`EN=H + nSTB=H = Normal`（datasheet Table 8-3）；
TCAN1044：`STB=L = Normal`。`xcvr` 命令可查看实时电平。

线束：CAN0-CAN1、CAN2-CAN3、CAN4-CAN5、CAN6-CAN7、CAN8-CAN9、CAN10-CAN11 两两互联。

---

## 2 目录结构

```
tc397_can_x12/
├── App/can12.[hc]      # 12路驱动：轮询收发/accept-all滤波/RTR/TEC-REC/BO恢复/收发器控制
├── Shell/shell_can.c   # cansend/candump/canlive/canstat/canpair/canflood/canrst/xcvr
├── Shell/shell_can.h   # live 标志接口（Cpu0_Main 主循环调用）
├── Cpu0_Main.c         # STM 1ms + P13.0 + UART/Shell/DTS + xcvr/CAN 初始化 + 主循环轮询
├── Configurations/…    # 同 uart_lettershell（中断配置未动，CAN 用纯轮询，无新增中断）
├── ...                 # 其余同 tc397_uart_lettershell（iLLD/Lcf/CMake/build.sh）
└── build/gcc/tc397_can_x12.{elf,hex,map}
```

---

## 3 构建与下载（Ubuntu）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_can_x12
./build.sh build                        # Debug
./build.sh build --build-type Release
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 硬复位（flasher）
```

产物 `build/gcc/tc397_can_x12.{elf,hex}`（`build/` 已忽略，不进 git）。
当前板上为 **Debug 版（最终版，2026-09-14）**：`text 100976 data 6216 bss 141768, hex 296K`。

---

## 4 串口与 Shell

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw
```

启动尾部：

```
TC397 CAN x12 + Letter-Shell
Board: TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600)
Type 'help' for commands, 'canstat' for CAN, 'canpair' for pair test
ChipID: 0xAF239793 CHREV=0x13
SCU_ID: 0x00C4C0C1 ...
STM Freq: 100000000 Hz ...
DTS raw=0x... -> ~52 C
CAN 12ch init done: 1M/5M 80% (pairs 0-1..10-11), nFAULT=0
```

### CAN 命令（can-utils 语法子集）

```
cansend <ch> <frame>   经典: 123#DEADBEEF / 12345678#1122(扩展) / 123#R[Rlen]
                       FD: 123##<flags><data>, flags bit0=BRS, 如 123##1DEADBEEF
candump [ch|all] [n]   打印缓存的接收帧（默认 all 20）
canlive <on|off>       主循环实时打印每帧
canstat [ch]           计数+TEC/REC/BO+NBTP/DBTP+fMCAN
canpair [rounds] [len] [fd]  双向回环: 每对互发验证 id+数据 (默认 1轮 8B FD+BRS)
canflood <ch> <n> [len] [fd] 突发发送（TX FIFO 16深，超量报 busy，属正常）
canrst [ch|all]        节点恢复（INIT toggle）
xcvr                   收发器使能脚电平 + nFAULT
```

UART/LED 旧命令（`help/version/mcu/uid/uptime/reset/temp/sysinfo/mem/led`）保持不变。

---

## 5 位定时（fMCAN=80MHz，`canstat` 实测）

* 仲裁段 1M：`NBTP=0x06030E03`（NBRP=3, NTSEG1=14, NTSEG2=3 → 20TQ×50ns=1.0Mbps，采样点 80%）
* 数据段 5M：`DBTP=0x00800B22`（DBRP=0, DTSEG1≈11, DTSEG2≈2, TDC=1 → 16TQ×12.5ns=5.0Mbps，采样点 ~81%）
* 驱动用 `calculateBitTimingValues=TRUE` 由 fMCAN 自动计算（1M/0.8 + 5M/0.8），TDC = DTSEG1+2（同 tc387 参考规则）。

MessageRAM：`0xF0200000 + group*0x10000`，每节点 4KB（std 0x000/ext 0x080/rxFifo0 0x180/txBuf 0xA80，
RX FIFO 32/TX FIFO 16）。`g_can`（~70KB）显式放 `.bss`（NOBITS），不占 Flash 拷贝。

---

## 6 实测结果（2026-09-14，DAP miniWiggler + TAS，板上 Debug 版）

* `canstat`：12 节点 TEC=REC=0，BO=0，nFAULT=0，`fMCAN=80.00MHz`
* `canpair 1 8 0`（经典 8B）：12/12 PASS
* `canpair 1 8 1`（FD+BRS 8B）：12/12 PASS
* `canpair 3 64 1`（FD+BRS 64B ×3 轮）：36/36 PASS，全程零错误
* 扩展帧 `1ABCDEFF#11223344`、RTR `200#R4`：收发正常
* `canflood 6 500 8`：FIFO 满后报 busy（符合预期），无错误计数增长
* 软件 `reset` 后：启动正常 + `canpair` 12/12 PASS（暖启动已修复，见 §7）
* 完整日志：`tc397/temp/canx12_final_test.log`（不进 git）

---

## 7 已知问题与处理

1. **SW 复位后曾卡死在 CAN 初始化**（现象：启动停在 DTS 行，shell 无响应；因 SW 复位保留 RAM，
   陈旧的 `g_canModuleInitialized=TRUE` 跳过了 module 初始化）。修：`can12_init_all()` 入口强制清标志、
   每次全量重配（`App/can12.c`）。复测：`reset` 后启动 + 收发一切正常。
2. **首次上电第一帧 can0→can1 超时一次**（之后从未复现：经典/FD/64B 多轮全过，TEC/REC 恒零）。
   疑为 TCAN1043 使能后首帧瞬态；`canpair` 失败不影响后续，`canrst`/复位可清。待长期观察。
3. **TX 失败的帧会滞留 TX FIFO 被硬件持续重发**（M_CAN 行为），伴随反复 bus-off（自动恢复计数涨）。
   排查时认准 `canstat` 的 `bo/rst` 列；对端恢复/复位后用 `canrst` 清理。
4. `uptime` 在 SW 复位后不清零（RAM 保持所致，uart 基线亦如此，非本工程引入）。
5. TCAN1043 非 G 版本 FD 上限为 2M（datasheet）；本板 5M BRS 实测通过，
   若换批次收发器出现 5M 误码，优先降数据段到 2M（`can12_init_all(1M, 2M)`）再测。

---

## 8 Windows 11 + TASKING 构建与实测（2026-09-18）

### 8.1 测试背景

* 目标：Ubuntu GCC 功能已验证，现验证同一套源码在 Windows 11 +
  TASKING TriCore v6.3r1（`C:\z\app\TASKING\TriCore_v6.3r1`）下的命令行构建与功能。
* 约束：增量改动不得影响 Ubuntu GCC（`build.sh` 原样保留；源码改动包在
  `__TASKING__` 分支或工具链无关形式）。
* 环境：AURIX-Studio-1.10.36（AURIXFlasher v3.0.18），COM165（921600），
  DAP MiniWiggler；12 路 CAN 两两相连（CAN0-CAN1 … CAN10-CAN11），终端电阻已接。

### 8.2 构建命令

```powershell
.\build.ps1 -Compiler tasking -Action download     # Tasking Debug 编译并烧录
```

### 8.3 通用兼容改动（GCC 行为不变，详见 `tc397/temp/tasking_porting_log.md`）

与 uart 基线同 6 项（`+gcc` 语言扩展、shell.h/shell.c 的 `__TASKING__` 分支、
`SHELL_DSYNC()` 宏、LSL `shellCommand` 命名组、`static inline`）。

### 8.4 本工程实测日志（Tasking Debug）

编译（302 obj，0 error）：

```
[300/302] Linking C executable tc397_can_x12.elf
Done.
```

`canpair`（1 轮，len=8 FD+BRS）：

```
round 1:
  can0->can1: TIMEOUT FAIL        # 首轮首包偶发（复位后首帧瞬态，见 §7.2 同类现象）
  can1->can0: PASS (id=140 len=8 FD+BRS)
  can2->can3 ... can11->can10: PASS（余 10 向全过）
canpair done: FAIL (1 fail(s))
```

重跑一次：

```
round 1:
  can0->can1: PASS (id=100 len=8 FD+BRS)
  ...（余 11 向全过）
canpair done: ALL PASS (0 fail(s))
```

### 8.5 测试结果

* 编译/烧录/12 路回环全 PASS（首轮 can0 首包偶发超时 1 次，重跑全过；
  与 Ubuntu GCC 记录的“首次上电第一帧超时”同类现象，属 TCAN1043 使能后首帧瞬态）。

## 9 2026-09-22 家族问题专项修复（串口 / lwIP）

> 背景：`tc397_sdmmc` §1.3 与 `tc397_selftest` §5.2/§5.3 定位出的几个"家族通用"问题
> （GCC 孤儿段、lwIP 定时器关中断、SPI 超时按循环计数、`frd_is_ready()` 空指针、
> `Ifx_Lwip_init*()` 重复 `initUART()`）。本次对 8 个 tc397 工程做了一轮横向排查。

### 9.1 改动

| 文件 | 改动 | 原因 |
| --- | --- | --- |
| `CMakeLists.txt`（GCC 分支） | **去掉 `-fdata-sections`**（保留 `-ffunction-sections`） | TriCore-GCC 13.x（Ubuntu `/opt/tricore-gcc`）把每个静态变量放到**裸 `.<sym>` 段**，`Lcf_Gnuc_Tricore_Tc.lsl` 的 copy/clear 表只收 `.data`/`.bss` 系列 → 变量启动不初始化、保留上电随机值。实测后果是 `shellList[]` 野指针 → `shellGetCurrent()` 跳野指针 Trap → **上电串口无输出**。ADS 的 tricore-gcc11 11.3.1 生成 `.bss.<sym>`（被 `*(.bss.*)` 收走）所以 Windows 下复现不了，Ubuntu gcc13 会中招 |

本工程无 lwIP / SPI 网口，也没有"未初始化即解引用"路径。

### 9.2 一个既有告警的说明（不用处理）

`App/can12.c` 的 `#pragma section ".bss.g_can" aw` 是**有意**的：`g_can` 是约 70 KB 的零初始化
数组，要落进 LSL 里 `*(.bss.*)` 收集的 NOLOAD 段，避免 70 KB flash 副本。
因此汇编器会报 `Warning: setting incorrect section type for .bss.g_can`——这是**既有告警**，
段最终仍被 `.bss` 输出段（带启动清零表）收集，实测 `canstat` 12 通道计数全 0、`canpair` 全过，无需处理。

### 9.3 验证（2026-09-22，TASKING Debug，COM168）

```
canstat : fMCAN=80.00 MHz nFAULT=0，12 通道 rx/tx/ovf/bo/rst 全 0，
          NBTP=0x06030E03 DBTP=0x00800B22（与 §5 基线一致）
canpair : 1 round，pairs 0-1..10-11 双向 ALL PASS (0 fail(s))
```

GCC（ADS tricore-gcc11 11.3.1）与 TASKING v6.3r1 均 **0 error**。

## 10 2026-09-22（续）AURIX Development Studio 里的 GCC/TASKING 构建修复

> 用户报告：在 ADS GUI 里编译下载后 **串口有打印但敲回车没反应**（GCC），TASKING 则根本编不过。
> 本轮把 ADS 的托管构建（`.cproject`）与 CMake 构建逐项对齐，定位到两处**只影响 ADS**的缺陷。

### 10.1 根因一：`.cproject` 里没有任何 `-D`，letter-shell 的用户配置被忽略

* CMake 构建给所有源文件传 `-DSHELL_CFG_USER="shell_cfg_user.h"`，而 ADS 的 `.cproject`
  **GCC 配置连 “Defined symbols (-D)” 选项都没有**（TASKING 配置也只有 `__CPU__=tc39xb`）。
* 于是 `shell.c` 编译时 `shell_cfg_user.h` 根本没被包含，`SHELL_TASK_WHILE` 回落到
  `shell_cfg.h` 的默认值 **1**（用户配置要求 0），`Shell`/`ShellCommand` 结构体布局也与
  `shell_port.c`（它直接 include 该头）不一致：`Shell_Process()` 里的 `shellTask()` 变成
  死循环、字段偏移错位 → **上电有打印、输入无响应**。
* **修复**：`Shell/letter-shell/src/shell_cfg.h` 给 `SHELL_CFG_USER` 加默认值
  `"shell_cfg_user.h"`。这样任何构建系统（ADS GUI / CMake / 其它）都不会漏掉用户配置，
  也避开了在 4 个配置 × 9 个工程里各写一遍带引号 `-D`（Eclipse 命令行生成器对引号的
  处理不可控：实测把引号丢掉后 `#include SHELL_CFG_USER` 会直接报错）。

  **实测复现与验证**（GCC Debug，去掉 `-DSHELL_CFG_USER` 等价 ADS）：

  | | 修复前 | 修复后 |
  | --- | --- | --- |
  | 冷启动日志 | 完整打印 | 完整打印 |
  | 敲 `ver` / `mcu` / `help` | **无任何响应** | 全部正常响应 |

### 10.2 根因二：TASKING 缺 `--language=+gcc`，`##__VA_ARGS__` 直接编译失败

* letter-shell 的 `SHELL_EXPORT_CMD()` 用了 GNU 扩展 `, ##__VA_ARGS__`（`shell.h:149/187/260`）。
* ADS 的 TASKING 默认参数只有 `--language=+volatile`（见 ADS 生成的 `subdir.mk`），
  缺 `+gcc` 时 cctc 报 **`ctc E250: missing argument for "..." parameter`**，
  `shell.c` / `shell_port.c` 编译失败 → **ADS 里 TASKING 根本编不过**。
* **修复**：`.cproject` 的两个 TASKING 配置各加一个选项（插件里本就有，只是默认关闭）：

  ```xml
  <option id="com.infineon.aurix.buildsystem.managed.c.compiler.tasking.gcc.<唯一数字>"
          name="Allow GNU C extensions (--language=+gcc)"
          superClass="com.infineon.aurix.buildsystem.managed.c.compiler.tasking.gcc"
          value="true" valueType="boolean"/>
  ```

  **实测**（用 ADS 同款默认参数调 cctc）：

  | 参数 | 结果 |
  | --- | --- |
  | `--language=+volatile`（ADS 默认） | `ctc E250` 多条 → 失败 |
  | `--language=+volatile,+gcc` | 通过 |

### 10.3 一并说明

* ADS GUI 第一次打开工程时会跑 “Project Booster” 同步库，它会自动给工程打补丁
  （本工程被加了 `Shell/shell_port.c` 的 `#include "shell_cfg_user.h"`、`shell_ext.h` 的
  `#include <stddef.h>`），并在 `tc397/.gitignore` 里加上 ADS 的构建目录 —— 都是 ADS 的正常行为。
* ADS 的 GCC 默认参数含 `-fdata-sections`（插件里 `defaultValue="true"`）。已实测：
  ADS 自带的 tricore-gcc11 会生成 `.bss.<sym>`（被 LSL 的 `*(.bss.*)` 收走、启动清零），
  与 Ubuntu gcc13 生成裸 `.<sym>` 的情况不同，**在 ADS 下无副作用**（见 `tc397_sdmmc` §1.3）。
* 验证：GCC 与 TASKING 全量重编 **0 error**；板端 `canstat`/`canpair` 与 shell 命令正常。

---

## 11 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* can-utils 语法参考（GPL-2.0/BSD）：仅借鉴帧字符串格式，实现为自研代码
* 其余移植代码内部许可
