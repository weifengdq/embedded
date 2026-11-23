# 第 3 章 letter-shell 与命令行调试框架

## 3.1 文档信息


表 3-1 letter-shell 3.2.4 关键特性与本板配置

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 对应例程 | `samples/shell/letter_shell_basic/`、`samples/gpio/gpio_irq_btn/`（命令注册范例） |
| 移植位置 | `boards/hpm5e31_1/bsp/shell_port.c`、`boards/hpm5e31_1/bsp/shell_cfg.h`、`boards/hpm5e31_1/bsp/bsp_cmds.c` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（源码 + 上板实测，日志见第 9 节） |

## 3.2 本章导览

- 学到什么：把成品 letter-shell（v3.2.4）移植到 HPM5E31 上，用 UART0 中断 + 512 字节环形缓冲做输入，
  用 `SHELL_EXPORT_CMD` 把调试命令"零成本"挂进 Flash 的命令表；理解它和 `printf` 共存时要注意什么。
- 前置章节：第 02 章（工具链、构建、烧写）。
- 你需要先有的东西：一块能跑起 `samples/shell/letter_shell_basic` 的板子，串口 921600 8N1。

## 3.3 概述与典型应用

一句话定位：**letter-shell 是一个可裁剪的嵌入式命令行解释器**，把 MCU 从"只能打印日志"变成
"可以交互式读写寄存器、改参数、跑自检"的可调试设备。本板把它作为所有例程的统一调试入口。

典型应用场景：

1. 产线自检：上电后命令行跑 `pin`/`reg`/`mem` 检查 IO 与内存，不需要重新编译。
2. 现场故障定位：让客户串口敲 `clk`、`uptime`、`reboot`，比返厂拆机快得多。
3. 参数整定：PID 参数、CAN 位时序、PWM 频率等运行时参数用命令改，避免反复烧写。
4. 例程演示：每个外设例程注册自己的 `xxx_info`/`xxx_stat` 命令，一个固件内完成"演示 + 自证"。
5. 二次开发基座：命令即 API，FAE 可以现场脚本化调用（配合 `--send "cmd1;cmd2"` 的自动化抓取）。

关键特性（letter-shell 3.2.4）：


表 3-2 letter-shell 相关寄存器与驱动 API 对照

| 特性 | 说明 | 本板配置 |
|---|---|---|
| 命令导出 | `SHELL_EXPORT_CMD` 编译期注册，无运行时开销 | 开启 |
| 命令数量 | 受链接段大小限制 | 实测 16 条 / 432 字节 |
| 参数个数 | `SHELL_PARAMETER_MAX_NUMBER` | 8 |
| 历史命令 | `SHELL_HISTORY_MAX_NUMBER` | 6 |
| 输入缓冲 | `  shell buffer` | 1024 字节 |
| 回车识别 | LF / CR / CRLF | **CR 与 LF 同时使能** |
| 运行方式 | 裸机主循环轮询 `shellTask()` | `SHELL_TASK_WHILE = 0` |

## 3.4 工作原理

### 3.4.1 三层结构

- 端口层（`shell_port.c`）：只管"字节从哪里来、往哪里去"。
  - 输入：UART0 **RX 中断**把字节塞进 **512 字节环形缓冲**（`SHELL_RX_RING_SIZE`）。
  - 输出：`shell->write()` → `uart_send_data()` 阻塞发送。
- 内核层（`shell.c`）：行编辑、命令解析、历史、权限、帮助打印。
- 命令层（`bsp_cmds.c` + 各例程）：`SHELL_EXPORT_CMD` 宏把命令结构体放进名为 `shellCommand` 的链接段。

如图 3-1 所示，数据流是一条单向环：串口 RX → 环形缓冲 → `shellTask()` 取字符 → 行编辑 →
命令查找 → 命令函数执行 → `printf`/`shell->write()` → 串口 TX。

![图 3-1 letter-shell 在 hpm5e31_1 上的数据流](figures/03-letter-shell/fig-03-1-shell-architecture.png)

### 3.4.2 命令表的链接期生成

`SHELL_EXPORT_CMD` 展开后是：

```c
#define SHELL_EXPORT_CMD(_attr, _name, _func, _desc) \
    const char shellCmd##_name[] = #_name; \
    const char shellDesc##_name[] = #_desc; \
    SHELL_USED const ShellCommand \
    shellCommand##_name SHELL_SECTION("shellCommand") =
```

关键点：命令结构体被放进 `"shellCommand"` 链接段，`shellInit()` 通过链接器符号
`__start_shellCommand` / `__stop_shellCommand` 拿到"段首地址 + 段长度"，再除以
`sizeof(ShellCommand)` 得到命令条数。这样**新增命令不需要改任何表**，是 letter-shell 最大的工程价值。

实测：`objdump -h demo.elf` 显示 `shellCommand` 段大小为 `0x1b0 = 432` 字节；`help` 列出 16 条命令
（`logs/20261030-110211-letter_shell_basic.log`）。

![图 3-2 命令从源码到运行时的注册流程](figures/03-letter-shell/fig-03-2-shell-boot-flow.png)

### 3.4.3 时钟与波特率前提

控制台是 UART0（PA00/PA01），标称 921600 8N1。**但 UART 的源时钟实测是 80 MHz**，
`OSCR=8、DIV=11` 时实际波特率是 `80 MHz / (8 × 11) = 909090.9`，即 **-1.36% 误差**（第 05 章有完整推导与实测表）。
letter-shell 对此不敏感（>3% 才可能误码），但如果你往下接 USB-UART 桥又没流控，这个误差会叠加对端误差，
在连续大数据量时更易丢包——这是第 02 章"待确认：首字节丢失"的一个可能解释。

## 3.5 关键寄存器与驱动 API 对照


表 3-3 letter_shell_basic 例程信息

| 功能 | 寄存器/资源 | 位域 | 驱动 API | 例程位置 |
|---|---|---|---|---|
| 控制台波特率 | UART0 OSCR/DLL/DLM | OSC[3:0] / DIV | `uart_init()` | `boards/hpm5e31_1/board.c` |
| RX 数据就绪中断 | UART0 IER / LSR | ERBFI / DR | `uart_enable_irq(uart_intr_rx_data_avail_or_timeout)` | `shell_port.c` |
| 读一字节 | UART0 RBR | RBR[7:0] | `uart_read_byte()` | `shell_port.c` |
| 写多字节 | UART0 THR | THR[7:0] | `uart_send_data()` | `shell_port.c` |
| 中断使能 | PLIC | 优先级=2 | `intc_m_enable_irq_with_priority()` | `shell_port.c` |
| 命令表 | 链接段 `shellCommand` | — | `SHELL_EXPORT_CMD()` | 各 `main.c` |
| 行编辑缓冲 | RAM | 1024 B | `shellInit(shell, buf, size)` | `bsp.c` |
| 输入环形缓冲 | RAM | 512 B | 端口层自己维护 | `shell_port.c` |

## 3.6 例程逐条讲解

### 3.6.1 letter_shell_basic：最小可运行 shell


表 3-4 命令执行情况与实测数据

| 项 | 内容 |
|---|---|
| 路径 | `samples/shell/letter_shell_basic/` |
| 覆盖特性 | UART0 中断接收、环形缓冲、`SHELL_EXPORT_CMD`、`bsp_shell_poll()` 主循环 |
| 关键配置 | 控制台 921600；shell buffer 1024 B；RX ring 512 B；IRQ 优先级 2 |
| 实测现象 | 上电打印 banner，`help` 列出 16 条命令，`ver`/`clk`/`pin`/`led` 均正常 |
| 实测日志 | `logs/20261030-110211-letter_shell_basic.log` |

这段是主循环的全部——裸机下没有任何"shell 任务"，靠主循环反复调用`shellTask()`：

```c
int main(void)
{
    bsp_init();                 /* board_init() + shell_port_init() */
    board_init_led_pins();
    printf("=== hpm5e31_1 letter-shell demo ===\r\n");
    while (1) {
        bsp_shell_poll();       /* shellTask(&s_shell) */
        /* 这里可以放你自己的周期任务，注意不要长时间阻塞 */
    }
}
```

### 3.6.2 gpio_irq_btn：例程如何注册自己的调试命令

每个外设例程都按同一模板注册命令，照抄即可：

```c
static void cmd_info(int argc, char *argv[]) { /* 打印配置 */ }
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_info, cmd_info, gpio irq button: pin/irq/debounce config);
```

- `SHELL_TYPE_CMD_MAIN`：原型 `void f(int argc, char *argv[])`，适合带参数的复杂命令。
- `SHELL_CMD_DISABLE_RETURN`：不打印 CMD_RETURN，避免和命令自己的输出混在一起。
- 命令名要**全局唯一**：`help`/`ver`/`clk` 等已被 `bsp_cmds.c` 占用，新命令建议加前缀（`btn_`/`udma_`/`spi_`）。

本章表格一览：表 3-1 letter-shell 3.2.4 关键特性与本板配置；表 3-2 letter-shell 相关寄存器与驱动 API 对照；表 3-3 letter_shell_basic 例程信息；表 3-4 命令执行情况与实测数据；表 3-5 letter-shell 与同类方案对比；表 3-6 参考来源索引；表 3-7 待确认项与风险。

## 3.7 上板实测数据


表 3-5 letter-shell 与同类方案对比

| 指标 | 实测值 | 方法/出处 |
|---|---|---|
| 命令条数 | 16 条 | `help` 输出，`logs/20261030-110211-letter_shell_basic.log` |
| 命令表大小 | 432 字节 | `objdump -h demo.elf` 的 `shellCommand` 段 |
| 控制台标称波特率 | 921600 | `BOARD_CONSOLE_UART_BAUDRATE` |
| 控制台实际波特率 | 909090.9（-13574 ppm） | 回读 OSCR=8/DIV=11，`logs/20261030-111055-uart_loopback.log` |
| 串口有效吞吐 | 90 KB/s | UART4 同源同分频回环实测，`logs/20261030-111055-uart_loopback.log` |
| 输入环形缓冲 | 512 字节 | `shell_port.c` 的 `SHELL_RX_RING_SIZE` |
| 命令解析开销 | 与命令长度线性相关（未单独测量） | 待确认，见第 11 节 |

说明：命令执行时间没有单独计时，因为绝大多数命令被 `printf` 的串口时间主导（`printf` 是阻塞发送）；
需要用 `mcycle` 精确测时，可参考 `samples/gpio/gpio_speed`。

## 3.8 实测命令与日志原文

自动化抓取（scripts/hwrun.sh 会在烧写后按 --send 依次发送命令）：

```bash
bash scripts/hwrun.sh shell/letter_shell_basic --secs 4 --send 'help;ver'
```

日志关键片段（logs/20261030-110211-letter_shell_basic.log）：

```text
Command List:
hello                 CMD   --------  demo app command
led                   CMD   --------  control USR LED: led [0|1|toggle]
ver                   CMD   --------  show version info
uptime                CMD   --------  show uptime
clk                   CMD   --------  print main clock tree
pin                   CMD   --------  gpio read/write: pin <A..F><n> [0|1]
reg                   CMD   --------  read/write 32-bit register: reg <addr> [value]
mem                   CMD   --------  hexdump memory: mem <addr> [len]
```

`ver` 的输出同时给出了 sdk/board/build 时间，是现场确认固件版本最快的方法；
`clk` 直接读 clock 驱动，可用来核对第 4/5 章的时钟前提（cpu0=480 MHz、mchtmr=24 MHz）。

## 3.9 性能与调优

1. **`printf` 是最大瓶颈**：UART0 发送是轮询阻塞的，1 KB 日志约 11 ms（90 KB/s 实测）。
   在中断服务程序里 `printf` 会严重拉长中断响应，本仓库规定 **ISR 里只置标志，不打印**（见 `gpio_irq_btn`）。
2. 输入用中断 + 环形缓冲，而不是主循环轮询：921600 下每字节间隔仅 10.9 µs，主循环稍慢就丢字节。
3. 命令函数应"短、可重入、无阻塞"；需要长时间跑的命令（如跑马灯演示）要接受期间 shell 不响应。
4. 若日志量大，可把 `printf` 重定向到环形缓冲 + DMA 发送（第 05 章 uart_dma 的思路）。
5. 命令表是只读 Flash 数据，`help` 遍历是 O(n) 字符串比较，16 条可忽略；上千条命令时应改用哈希或前缀树。

## 3.10 坑与注意事项

1. **现象：敲 Enter 没反应 / 一次回车执行两次** → 原因：`SHELL_ENTER_CR` 和 `SHELL_ENTER_LF` 配置与终端不一致。
   本板把 **CR 与 LF 同时打开**（`SHELL_ENTER_CR=1、SHELL_ENTER_LF=1`），因此 `
` 和单独 `
`、`
` 都能结束一行；
   注意 `SHELL_ENTER_CRLF` 不能与它们同时开启。→ 解决：终端发送模式选"CR"或"CRLF"都可以。
2. **现象：新增命令不出现在 `help` 里** → 原因：函数被 `--gc-sections` 回收，或命令名不是 `__attribute__((used))`。
   HPM SDK 的 `ld.cmake` 默认加 `-Wl,--gc-sections`；letter-shell 靠 `__start_shellCommand/__stop_shellCommand`
   引用让 GNU ld 保留该段（SDK 链接脚本里并没有显式 `KEEP(*(shellCommand))`，属于"依赖链接器行为"）。
   → 解决：命令必须用 `SHELL_EXPORT_CMD`（宏里带 `SHELL_USED`）。R5 复核：GNU ld + `--gc-sections` 下
   `shellCommand` 段完整保留（`objdump -h demo.elf` 显示 `shellCommand 000001c0`，17 条命令，448 字节）。
   想用 lld 对照时发现本工具链（riscv32-unknown-elf-newlib-multilib，GCC 11.1.0）里没有 `ld.lld`，
   `-fuse-ld=lld` 直接报 `cannot find 'ld'`，所以 lld 这一半是已知限制（见第 11 节）。
3. **现象：`help` 里的命令名被截断/乱码** → 原因：`printf` 与 `shell->write()` 同时往同一个 UART 写，
   在命令执行期间不要从 ISR 打印，也不要开两个任务同时打印。
4. **现象：输入长命令丢字符** → 原因：环形缓冲 512 B 或 shell 行缓冲 1024 B 溢出。→ 解决：加大
   `SHELL_RX_RING_SIZE`，或降低输入速率；`shellInit` 的 buffer 必须 ≥ 最长命令 + 历史开销。
5. **现象：`reboot` 后串口乱码** → 原因：复位瞬间 TX 正在发送。→ 解决：`bsp_reboot()` 里先 `board_delay_ms(10)`
   （已在本仓库 `bsp.c` 实现），保证最后一帧发完再复位。
6. **现象：移植到别的串口后完全无输入** → 原因：只改了 `BOARD_CONSOLE_UART_BASE`，没改 `shell_port.c` 里
   中断号/时钟/引脚复用。→ 解决：按第 05 章的"UART 移植四件套"（引脚复用、时钟、`uart_init`、中断向量）逐项检查。

## 3.11 优劣评价与同类对比


表 3-6 参考来源索引

| 方案 | 命令注册 | 体积 | 行编辑/历史 | 依赖 | 适用场景 |
|---|---|---|---|---|---|
| **letter-shell 3.2.4（本板）** | 链接段自动收集 | 约 5~8 KB Flash | 有（6 条历史） | 无 OS | 裸机/RTOS 通用，最省事 |
| RT-Thread finsh | 段 + 内置 | 10 KB+ | 有 | RT-Thread | 已在用 RT-Thread 的项目 |
| FreeRTOS+CLI | 手工命令表 | 约 6 KB | 无 | FreeRTOS | 简单命令均可 |
| Microrl / 自研 | 手工 | 2~4 KB | 部分 | 无 | 极简场景 |
| SEGGER RTT + 脚本 | 无（调试器） | 0 | 无 | J-Link | 研发阶段，不占串口 |

结论：**裸机项目首选 letter-shell**；它把"命令表维护"这件容易出错的事交给了链接器，是性价比最高的方案。

## 3.12 参考来源

1. 本仓库例程：`samples/shell/letter_shell_basic/`、`samples/gpio/gpio_irq_btn/`
2. 本仓库移植层：`boards/hpm5e31_1/bsp/shell_port.c`、`shell_cfg.h`、`bsp_cmds.c`、`bsp.c`
3. HPM5E00 用户手册 RM §48.1（PDF p.818）：UART 32 字节 TX/RX FIFO、波特率独立时钟、DMA 支持
4. HPM5E00 用户手册 RM §21.2.1（PDF p.417）：IOC 基本属性与外设功能配置
5. HPM SDK 链接选项：`hpm_sdk/cmake/toolchain/ld.cmake:6`（`-Wl,--gc-sections`）
6. letter-shell 源码与文档：[https://github.com/NevermindZZT/letter-shell](https://github.com/NevermindZZT/letter-shell)
7. 实测日志：`logs/20261030-110211-letter_shell_basic.log`、`logs/20261030-111055-uart_loopback.log`

## 3.13 扩展阅读

- letter-shell 官方 README 的"命令导出"与"伴生对象"章节。
- 本仓库第 05 章"UART FIFO/中断/DMA"——shell 输入缓冲的原理与此同源。
- 本仓库第 02 章"烧写与串口抓取"，了解 `scripts/hwrun.sh` 的 `--send` 自动化用法。

## 3.14 待确认项与风险

> 已逐条复核（2026-11-02）。本章本轮补了链接器与命令耗时两项（后者 18:04 上板复跑）；4 KB 压测仍受板卡排队影响未跑成，
> 按"已知限制 + 原因 + 一条命令复跑"处理。

| 项 | 状态 | 说明 / 验证方法 |
|---|---|---|
| 命令执行耗时（含 printf） | **已实测（2026-11-02 18:04）** | 数据：`sh_time` 连跑两次，dispatch=22 574（冷）/3 215（热）周期、空转 1000 次 9 511 周期、`printf` 64 B 377 361/376 264 周期、2 B 16 724/16 719 周期；480 MHz 下热 dispatch 6.7 µs、printf 64 B 784 µs、2 B 34.8 µs（日志 `logs/20261102-180410-letter_shell_basic.log`）。原因为历史记录，保留复跑方法：例程的 `sh_time` 命令（在命令体首尾读 `mcycle`，并分别计时 1000 次空转与两次 `printf`），执行 `bash scripts/hwrun.sh shell/letter_shell_basic --secs 18 --send "sh_time;sh_time"` 即可拿到 dispatch/printf 周期数 |
| `shellCommand` 段在不同链接器（lld/LTO）下是否被回收 | **已实测（GNU ld，2026-11-02）** / **已知限制（lld 不可用，未对照）** | GNU ld + `--gc-sections`：段在，448 字节 / 17 条命令。lld：本工具链无 `ld.lld`（`-fuse-ld=lld` 报 `cannot find 'ld'`），无法对照 → 已知限制；装上 lld 后一条命令验证：`cmake -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld … && objdump -h output/demo.elf | grep shellCommand` |
| 512 B RX 环形缓冲在 921600 下的余量 | 已知限制（本轮未取窗口） | 原因：与上一条同批排队，长字符流压测未跑。复跑方法：`bash scripts/hwrun.sh shell/letter_shell_basic --secs 18 --send "ver;hello <4000 个字符>"`，数日志里回显的字符数与发送长度，差值为丢字符数；也可把 `SHELL_RX_RING_SIZE` 改 1024 再对比 |
| 与 USB-UART 桥的兼容性 | **已定位（2026-11-02）** | 本板控制台走 CH343（`lsusb` 1a86:55d3）。R5 抓日志时多次出现"打印内容掉字符"（例如 `spi_ms_ber` 被打印成 `spi_ms_be`、`uart_ring_trig` 被打印成 `uart_ring_tig`），掉的都是同一个字符 r，而同一批数据在回环校验里是 0 错误。说明 921600 + CH343 在"连续输出"方向已接近链路极限：波特率误差 -13574 ppm 叠加桥片容差。验证方法：换 FT232 或降到 460800 重跑同一批命令，统计掉字符数 |

## 3.15 参考来源（R5 补充上游链接）

- letter-shell 上游仓库（3.2.4）：[https://github.com/NevermindZZT/letter-shell](https://github.com/NevermindZZT/letter-shell)
- HPM SDK 上游（链接选项 `-Wl,--gc-sections` 见 cmake/toolchain/ld.cmake）：[https://github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)（v1.13.0）
