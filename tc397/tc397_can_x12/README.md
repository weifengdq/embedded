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

## 8 Windows 11 + TASKING 构建（2026-09-18 已验证）

* 工具链：`C:\z\app\TASKING\TriCore_v6.3r1`，Studio 1.10.36，串口 COM165。
  `build.sh`（Ubuntu/GCC）不受影响：

```powershell
.\build.ps1 -Compiler tasking -Action download     # Tasking 编译并烧录
```

* 本工程 Tasking 实测：编译 302 obj 0 error；`canpair` 12 路双向全 PASS
 （CAN0-CAN1 … CAN10-CAN11 两两相连，终端电阻已接；首轮 can0 首包偶发超时 1 次，
  重跑 ALL PASS，见 §7.2 同类现象）。
* 通用兼容改动见 `tc397/temp/tasking_porting_log.md`（GCC 行为不变）。

---

## 9 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* can-utils 语法参考（GPL-2.0/BSD）：仅借鉴帧字符串格式，实现为自研代码
* 其余移植代码内部许可
