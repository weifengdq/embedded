# tc397_lin_x11 — TC397XX 11路 ASCLIN LIN (19200, 主从) + Letter-Shell 收发测试

`tc397_uart_lettershell` 拷贝而来，新增 11 路 LIN 驱动与主从测试命令。
目标 **TC397XX 292pin**，调试串口 **ASCLIN0 TX P14.0 / RX P14.1, 921600-8N1**，
LED **P13.0（低电平点亮）**。LIN11 为 commander（master），LIN1~LIN10 为
responder（slave），11 路 LIN 总线连在一起做主从收发测试。

* 驱动：iLLD `IfxAsclin_Lin`（`Asclin/Lin/IfxAsclin_Lin.[hc]`），**纯轮询、无新增中断**
  （`Configurations/ConfigurationIsr.h` 未动）；iLLD 阻塞式 API 不适用于单核多节点
  脚本测试，`App/lin12.c` 在 `IfxAsclin_*` 标志操作上自研了 tick 超时保护的事务引擎
* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
  （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 收发器手册已转 txt（不进 git）：`tc397/ref/tlin1024a-q1.txt`（主 datasheet，2106 行）、
  `tc397/ref/tlin1024a_sffs084.txt`（功能安全件，430 行）

---

## 1 硬件

| 逻辑 | TX | RX | ASCLIN | 收发器使能 |
| --- | --- | --- | --- | --- |
| LIN0 | P14.0 | P14.1 | ASCLIN0 | SLP_N P02.8 — **仅占位**：与 UART0 调试串口引脚冲突，不初始化、不测试 |
| LIN1 | P15.4 | P15.5 | ASCLIN1 | SLP_N P02.8=H（与 LIN0/2/3 共用） |
| LIN2 | P10.5 | P02.10 | ASCLIN2 | SLP_N P02.8=H |
| LIN3 | P20.0 | P20.3 | ASCLIN3 | SLP_N P02.8=H |
| LIN4 | P00.9 | P00.12 | ASCLIN4 | SLP_N P00.11=H（与 LIN5/6/7 共用） |
| LIN5 | P00.7 | P00.6 | ASCLIN5 | SLP_N P00.11=H |
| LIN6 | P23.5 | P23.1 | ASCLIN6 | SLP_N P00.11=H |
| LIN7 | P22.1 | P22.4 | ASCLIN7 | SLP_N P00.11=H |
| LIN8 | P34.5 | P33.6 | ASCLIN8 | SLP_N P00.10=H（与 LIN9/10/11 共用） |
| LIN9 | P01.7 | P20.6 | ASCLIN9 | SLP_N P00.10=H |
| LIN10 | P00.8 | P00.4 | ASCLIN10 | SLP_N P00.10=H |
| LIN11 | P21.0 | P21.1 | ASCLIN11 | SLP_N P00.10=H — **commander (master)** |
| UART | P14.0 | P14.1 | ASCLIN0 921600 | — |
| LED | P13.0（低=亮） | — | — | 心跳 1Hz，可 `led hb off` 关 |

引脚逐一核对过 `IfxAsclin_PinMap_TC39xB_LFBGA292.{h,c}`（见 `App/lin12.c` 注释；
RxSel a~g 即 ALTI 0~6，驱动里由 pin 结构 `select` 字段直接推导，不手写硬编码表）。
3 片 TLIN1024A-Q1（Quad LIN 收发器，每片 4 通道）：
芯片 1 = LIN0~3（VSUP1/GND1 供 LIN1/2，VSUP2/GND2 供 LIN3/4，按 datasheet §9.1 分组），
芯片 2 = LIN4~7，芯片 3 = LIN8~11。
板上 SLP_N 即收发器 EN（高=Normal，低=Sleep；datasheet §9.3.6/§9.4），4 通道 EN
连在一起由 1 个 MCU GPIO 控制。`lin_xcvr_init()` 上电即置 3 组 SLP_N 为高。

线束：LIN1~LIN11 总线短接在一起（LIN0 不接）。

> 注意（commander 上拉）：按 TLIN1024A-Q1 §9.3.1/§9.3.1.2.1，responder 靠内部
> 45kΩ 上拉即可，**commander 节点必须外加 1kΩ + 串联二极管到 VSUP**。
> 本测试要求 LIN11 通道具备该 1k 上拉，否则隐性电平/边沿可能不达标；
> 若 `linpair` 在 commander 上拉缺失时仍通过（10 路 45k 并联约 4.5k 也可维持隐性），
> 属容限内通过，正式 LIN 一致性测试前仍应补上 1k。

---

## 2 目录结构

```
tc397_lin_x11/
├── App/lin12.[hc]      # 11路驱动：tick超时事务引擎/master-broadcast/slave-tx/PID/EN控制
├── Shell/shell_lin.c   # linsend/linreq/linpair/linslv/linstat/lindump/linbaud/linslp/linwake/linerr
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
当前：Debug `text 96115 data 7352 bss 72076`（基线 uart 版为 `text 82718`）。

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
ChipID: 0x... CHREV=0x...
...
LIN 11ch init done: 19200 master=LIN11 slaves=LIN1..10 (LIN0 placeholder)
```

### LIN 命令

```
linsend <id 0..63> [hexdata] [classic]  master广播: LIN11发header+response, LIN1~10校验
  e.g. linsend 0x12 A0A1A2A3A4A5A6A7    (0x3C/0x3D默认classic, 其余默认enhanced)
linreq <id> <slave 1..10> [len] [hex]   从机应答: master发header, 指定slave发response
  e.g. linreq 0x20 3 8 1122334455667788 (master校验 + 其余9路snoop计数)
linpair [rounds] [len]                  master广播循环: id=0x10..0x19, 默认1轮8B
linslv [rounds] [len]                   从机轮流应答: LIN1..10逐个发, 默认1轮8B
linstat                                 计数+错误列+EN电平+波特率 (LIN0显示placeholder)
lindump [ch|all]                        每个通道最后一次校验通过的帧
linbaud <9600|10417|19200>              全通道重配波特率 (默认19200)
linslp <group 0..2|all> <0|1>           EN控制: 1=normal(H), 0=sleep(L)
linwake                                 3组EN全部回normal
linerr <parity|cksum|timeout>           错误注入: PID奇偶错 / 校验和模式失配 / 无应答超时
```

UART/LED 旧命令（`help/version/mcu/uid/uptime/reset/temp/sysinfo/mem/led`）保持不变。

---

## 5 LIN 特性（本工程覆盖点）

* 帧结构：Break（master 发 13 bit 显性，`breakLength=13`）+ Sync `0x55` +
  PID（含 P0/P1 奇偶，`lin_pid()` 按 LIN 规范生成，HW 在 RX 端校验，错则 LP 标志）+
  Data 1~8B + Checksum（HW 自动生成/校验，`csEnable=TRUE`）。
* 校验和：ID 0x00~0x3B 用 enhanced（含 PID），0x3C/0x3D 用 classic（不含 PID）；
  驱动默认按此规则，`linsend … [classic]` 可强制；`linerr cksum` 演示失配时的 LC 标志。
* 波特率：默认 19200（iLLD LIN 默认值；TLIN1024 收 ≤100k，规范发送 ≤20k）；
  `linbaud` 支持 9600/10417/19200（从机 ABD 关闭，定波特率；`prescaler=4/OS16` 由 FDR 自动算）。
* 响应空间/超时：header 与 response 之间为亚 ms 级（单核顺序执行），远小于
  response-timeout 窗口（`DATCON.RESPONSE=255`，frame-timeout 模式）；
  无应答时 master 报 response-timeout（`linerr timeout`）。
* 收发器：EN 高=Normal（TXD→LIN，LIN→RXD），EN 低=Sleep（驱动关、RXD 浮空，
  弱上拉；datasheet Table 9-1）；TXD 显性超时 DTO 防总线 stuck-dominant；
  bus stuck-dominant 上电进 sleep 时有误唤醒锁止（§9.3.9）。
* 诊断帧：0x3C（master-req）/0x3D（slave-resp）按 classic 校验，可用
  `linsend 0x3C …` / `linreq 0x3D …` 走一遍（本工程不实现 NAD/服务层）。
* 休眠/唤醒：`linslp <g> 0` 进 sleep（对应组收发器关断），`linwake` 回 normal；
  LIN 总线唤醒（显性 250µs~5ms）由收发器硬件完成，MCU 侧表现为恢复通信（见 §6 步骤 5）。

---

## 6 实测步骤与结果（DAP miniWiggler + TAS，板上 Debug 版）

> 状态（2026-09-15）：功能已编译通过并烧录（Pass，3334ms）；串口实测待补——
> 当时 `/dev/ttyACM0`（CH340 调试串口）未接入（`lsusb` 无 `1a86:55d3`），shell
> 交互无法进行。下表为标准测试流程，接上调试串口后按序执行即得结果。

| # | 命令 | 期望 | 说明 |
| --- | --- | --- | --- |
| 1 | 上电启动日志 | `LIN 11ch init done: 19200 …` | EN 已置高，11 通道初始化完成 |
| 2 | `linstat` | 11 通道计数全 0，EN=1/1/1 | 基线 |
| 3 | `linsend 0x12 A0A1A2A3A4A5A6A7` | ALL PASS，mask=0x7FE | master 广播，10 从机全校验 |
| 4 | `linpair 2 8` | 20/20 PASS | 不同 ID/数据的广播循环 |
| 5 | `linreq 0x20 3` | PASS，snoop 9/9 | 从机应答 + master 校验 + 余部监听 |
| 6 | `linslv 1 8` | 10/10 PASS | 10 个从机逐个应答 |
| 7 | `linbaud 9600` → `linpair 1 8` → `linbaud 19200` | 均 ALL PASS | 波特率切换（每次切完重测） |
| 8 | `linslp 2 0` → `linpair 1 8`（应 FAIL，timeout 列涨）→ `linwake` → `linpair 1 8`（PASS） | FAIL→PASS | sleep/wake（含 master 睡眠组） |
| 9 | `linerr parity` | 10/10 从机 parity 指示 | PID 奇偶错 |
| 10 | `linerr cksum` | 10/10 从机 LC 标志 | 校验和模式失配 |
| 11 | `linerr timeout` | master response-timeout | 无应答超时 |
| 12 | `linsend 0x3C 0102030405060708` + `linreq 0x3D 5 8` | PASS（classic） | 诊断帧 0x3C/0x3D |
| 13 | `lindump all` + `linstat` | 末帧/计数与上述一致 | 汇总 |

注意事项：

* LIN 是单主总线——同一时刻只能有一个事务在跑；`linsend/linreq/linpair/linslv`
  均为同步阻塞命令（最长约 1s，超时有 tick 保护，不会卡死 shell）。
* `linpair`/`linslv` 的 ID 从 0x10/0x20 起按序分配，避免与诊断帧混淆；
  每个事务内 header 与 response 间隔为亚 ms 级，符合 LIN 响应空间要求。
* sleep 组含 master（group2：LIN8~11）时全 FAIL 属正常：master 自身收发器已关断。
* 若某一路持续 FAIL，先看 `linstat` 的错误列定位：
  hdrE/timeout=总线无应答或 EN 未开；par=PID 奇偶；ck=校验和模式；
  fe=帧/冲突（ wiring 短路/断路重点查该路 TX/RX）。
* SW 复位后行为：`lin12_init_all()` 每次全量重配（不清历史计数外的状态），
  无 CAN 版曾遇到的 RAM 保持跳初始化问题；`uptime` 在 SW 复位后不清零
  （RAM 保持，uart 基线亦如此，非本工程引入）。
* LIN0（ASCLIN0）与 UART0 调试串口复用引脚，硬件上 LIN0 通道不可用；
  `linstat` 中 LIN0 恒显示 placeholder，任何 `lin*` 命令都不触碰 ASCLIN0。

---

## 7 已知问题与处理

1. **iLLD 阻塞 API 不适用于单核多节点测试**：`IfxAsclin_Lin_receiveHeader` 等
   内部死等 HW 标志，无超时。已自研 tick 超时事务引擎（50ms header / 100ms
   response），超时即报错返回，shell 不卡死。
2. **从机 header 轮询是顺序的**：10 路 RHE 同时置位，顺序读回无时序风险；
   总线异常时每路最多等 50ms，单命令最坏 ~1s 后必返回。
3. **commander 1k 上拉**：见 §1 注意框；测试前确认 LIN11 通道有此外部上拉。
4. **RXD 开漏上拉**：TLIN1024 RXD 为开漏（§9.3.3），MCU 侧需上拉到 I/O 电平；
   若某路 header 恒超时，优先量该路上拉与 EN 电平。

---

## 8 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
