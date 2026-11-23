# 第 26 章 寄存器速查、勘误与陷阱清单

## 26.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列，芯片版本 1.0）/ hpm5e31_1 |
| 对应例程 | `samples/shell/letter_shell_basic/`（`ver`/`clk`/`reg`/`mem` 命令）及各外设章节例程 |
| 实测日期 | 2026-10-30 |
| 可信度 | 中高：地址/中断号来自 SDK v1.13.0 头文件；勘误摘自 Errata Rev0.1 原文；实测仅覆盖时钟/串口/定时器基线，其余标注"待确认" |

## 26.2 本章导览

- **学到什么**：拿到一块 HPM5E31 板子后，如何在不翻 1200 页手册的情况下，快速查对"外设基地址 / 中断号 / 时钟开关 / 勘误规避"；遇到死机、外设不响应、通信不通时按什么顺序排查；哪些"坑"是本仓库在实测中真实踩到的。
- **前置章节**：第 01 章（芯片与板卡）、第 02 章（开发环境与调试）、第 13 章（SYSCTL 与电源）、第 23 章（性能基准）。
- **使用方式**：本章是"手术刀"，不是"教科书"。地址表用于核对代码里的硬编码地址；勘误表用于体检现有驱动；"坑"清单建议在 Code Review 时逐条对照。

## 26.3 概述与典型应用

HPM5E31 的外设采用**统一编址**：所有外设寄存器都映射在 32 位地址空间内，用普通指针即可读写，不需要 x86 式的 in/out 指令。这带来两个直接后果：

1. 查错时"地址写错一位"和"寄存器配置错"表现完全一样（读回 0 或触发总线错误），必须先核对地址；
2. 中断号、时钟使能、引脚复用三者只要漏一个，外设就"完全不响应"，且不会报错。

本章把这三类信息集中成表，并提供可重复运行的生成脚本（`tools/gen_regmap.py`），避免手工抄写出错。表 26-1 是本仓库各外设章节与速查数据的入口。

**表 26-1 本仓库外设覆盖与速查入口**（基地址/中断号为 HPM5E31 首实例，完整表见本章第 3 节）

| 章 | 外设/主题 | 首实例基地址 | 中断号 | 代表例程 |
|---|---|---|---|---|
| 03 | letter-shell 调试框架 | — | — | `samples/shell/letter_shell_basic/` |
| 04 | GPIO / IOC | `0xF00D0000` / `0xF4040000` | GPIO0_A = 1 | `samples/gpio/blinky_btn/` |
| 05 | UART | `0xF0040000` | UART0 = 19 | `samples/uart/*` |
| 06 | I2C | `0xF0060000` | I2C0 = 27 | `samples/i2c/*` |
| 07 | SPI | `0xF0070000` | SPI0 = 31 | `samples/spi/*` |
| 08/09 | MCAN / CAN FD | `0xF0300000` | MCAN0 = 46 | `samples/can/*` |
| 10/11 | LAN8651 10BASE-T1S（SPI2） | SPI2 = `0xF0078000` | SPI2 = 33 | `samples/lan8651/*` |
| 12/13 | ENET / lwIP | `0xF1400000` | ENET0 = 58 | `samples/enet/*` |
| 14 | PWMv2 / HRPWM | `0xF0420000` | PWM0 = 53 | `samples/pwm/*` |
| 15 | GPTMR | `0xF0000000` | GPTMR0 = 11 | `samples/gptmr/*` |
| 16 | QEI / QEO | `0xF0400000` | QEI0 = 51 | `samples/qei/*` |
| 17 | SDM / PLB / TRGM | SDM = `0xF0450000` | SDM0 = 55 | `samples/trgm/*` |
| 18 | ADC16 | `0xF0100000` | ADC0 = 42 | `samples/adc/*` |
| 19 | HDMA / XDMA | `0xF00C8000` / `0xF3100000` | HDMA = 40 / XDMA = 67 | `samples/dma/*` |
| 20 | USB0 | `0xF1420000` | USB0 = 60 | `samples/usb/*` |
| 22 | EWDG / PEWDG | `0xF00B0000` / `0xF4128000` | EWDG0 = 38 | `samples/wdg/*` |

> 表 26-1 的数据来自 `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_ip.h`、`hpm_soc_irq.h`（SDK v1.13.0），可用 `python3 tools/gen_regmap.py --check` 复核：当前解析结果为 **79 个实例基地址、79 个外部中断号、52 类 IP**。

## 26.4 工作原理：地址空间与中断路由

### 26.4.1 地址空间分区

HPM5E31 把地址空间切成"片上存储 / 系统控制 / 外设总线 / 外部映射"四大块（RM §16.1，p.57；DS Rev0.4 p.1）。最容易被忽略的是 **FGPIO 在 `0x0030_0000`、而 GPIO0 在 `0xF00D_0000`**——两个"GPIO"地址相差近 4 GB，写错就会静默失败。

**表 26-2 HPM5E31 地址空间分区（本板相关部分）**

| 区域 | 起始地址 | 结束地址 | 容量 | 用途/说明 |
|---|---|---|---|---|
| 片上 ROM / ILM / DLM | `0x0000_0000` | `0x000F_FFFF` | 1 MB 窗口 | 启动 ROM、CPU 紧耦合存储；ILM/DLM 各 128 KB |
| FGPIO | `0x0030_0000` | `0x0030_0FFF` | 4 KB | 快速 GPIO（不是 GPIO0！） |
| TFA | `0x0040_0000` | — | — | 事件分析单元 |
| AHB SRAM / DM | `0x3000_0000` | `0x3FFF_FFFF` | 256 KB + 64 KB | 主 SRAM 与紧耦合 DM；CAN 消息 RAM 也在此 |
| 片内 Flash（XPI0 映射） | `0x8000_0000` | `0x800F_FFFF` | 1 MB | **合封 NOR**，由 XPI0 控制器映射（DS Rev0.4 p.68） |
| PLIC / PLICSW | `0xE400_0000` / `0xE640_0000` | — | — | 平台中断控制器 / 软件中断 |
| MCHTMR | `0xE600_0000` | — | — | 64 位机器定时器（24 MHz） |
| 外设总线 | `0xF000_0000` | `0xF5FF_FFFF` | 96 MB 窗口 | UART/SPI/CAN/ADC/PWM… 全部外设寄存器 |

![图 26-1 HPM5E31 外设基地址分布与地址空间分区（数据由 tools/gen_regmap.py 解析 SDK 头文件生成）](figures/26/fig-26-1-memory-map.png)

**图 26-1** 左半部分每一行是一类外设 IP，横轴是地址；右半部分是地址空间四大分区示意。外设总线内部分布并不均匀：UART/SPI/I2C 集中在 `0xF004_0000`~`0xF007_FFFF`，而 MCAN、PWM、ADC 分别落在 `0xF030`、`0xF042`、`0xF010` 段，**不能按"间隔 0x4000 顺推"**。

### 26.4.2 中断路由

外部中断统一进入 PLIC（RM §6）：外设产生中断 → PLIC 门控/优先级仲裁 → CPU 的 `mip.MEIP` → 跳 `mtvec` 公共入口 → 读取 `mcause` 得到中断号 → 查向量表。三个高频错误点：

1. **中断号从 1 开始**：`IRQn_xxx` 的值就是 PLIC 源编号，0 不是合法外设中断；
2. **PLIC claim 可能返回 0**：SDK 的 `soc/HPM5E00/HPM5E31/toolchains/trap.c:160` 有注释明确写了这个 workaround（"irq number returned by `__plic_claim_irq` might be 0 … skip invalid irq_index"），自己写裸机中断入口时必须跳过 0，否则会误入中断 0 的处理分支；
3. **优先级分组**：5E31 的 PLIC 支持最高优先级 31（`HPM_IP_FEATURE_PLIC_HIGHEST_PRIORITY 31`），且支持 S 模式（`HPM_IP_FEATURE_PLIC_SUPPORT_S_MODE 1`）；中断嵌套要同时配置优先级阈值与 `mstatus`。

### 26.4.3 时钟/复位/引脚复用三件套

外设"读回全 0"最常见的原因不是寄存器写错，而是**外设时钟没开**（SYSCTL，RM §13）。初始化顺序固定为：开时钟 → 释放复位 → 配置 IOC 引脚复用 → 配置外设寄存器 → 开中断。少任何一步，现象都是"寄存器读 0 / 中断不触发"。

## 26.5 寄存器速查

### 26.5.1 生成方式（避免手抄错）

本章的地址/中断号表不是手写的，而是由脚本从 SDK 头文件生成，可随时复核：

```bash
cd hpm5e31_1
python3 tools/gen_regmap.py --check                 # 解析数量自检
python3 tools/gen_regmap.py --md > /tmp/regmap.md    # 生成 Markdown 表
python3 tools/gen_regmap.py --json | head            # JSON（供其它脚本消费）
```

脚本只读 `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_ip.h` 与 `hpm_soc_irq.h`，换 SDK 版本后重跑即可发现地址变化（例如 HPM5E3Y 与 HPM5E31 的外设集合不同）。

### 26.5.2 外设 IP 汇总（52 类，自动生成）

**表 26-3 HPM5E31 外设 IP 与实例基地址**（由 `tools/gen_regmap.py` 生成；"实例数"即该类 IP 有几个）

| IP | 实例数 | 基地址（从低到高） |
|---|---:|---|
| ACMP | 1 | `0xF0130000` |
| ADC | 2 | `0xF0100000`, `0xF0104000` |
| CRC | 1 | `0xF0080000` |
| DM | 1 | `0x30000000` |
| DMAMUX | 1 | `0xF00C4000` |
| ENET | 1 | `0xF1400000` |
| ESC | 1 | `0xF1700000` |
| EUI | 2 | `0xF0038000`, `0xF003C000` |
| EWDG | 2 | `0xF00B0000`, `0xF00B4000` |
| FGPIO | 1 | `0x00300000` |
| GPIO | 1 | `0xF00D0000` |
| GPIOM | 1 | `0xF00D8000` |
| GPTMR | 4 | `0xF0000000`, `0xF0004000`, `0xF0008000`, `0xF000C000` |
| HDMA | 1 | `0xF00C8000` |
| I2C | 4 | `0xF0060000`, `0xF0064000`, `0xF0068000`, `0xF006C000` |
| IOC | 1 | `0xF4040000` |
| LOBS | 1 | `0xF00DC000` |
| MBX0A | 1 | `0xF00A0000` |
| MBX0B | 1 | `0xF00A4000` |
| MCAN | 4 | `0xF0300000`, `0xF0304000`, `0xF0308000`, `0xF030C000` |
| MCHTMR | 1 | `0xE6000000` |
| NTMR | 1 | `0xF1410000` |
| OTP | 1 | `0xF3158000` |
| OWR | 2 | `0xF0030000`, `0xF0034000` |
| PCFG | 1 | `0xF4104000` |
| PDGO | 1 | `0xF4134000` |
| PEWDG | 1 | `0xF4128000` |
| PGPIO | 1 | `0xF411C000` |
| PGPR | 2 | `0xF4138000`, `0xF413C000` |
| PIOC | 1 | `0xF4118000` |
| PLB | 1 | `0xF0460000` |
| PLIC | 1 | `0xE4000000` |
| PLICSW | 1 | `0xE6400000` |
| PLLCTLV | 1 | `0xF40C0000` |
| PPI | 1 | `0xF00CC000` |
| PPOR | 1 | `0xF4100000` |
| PTMR | 1 | `0xF4120000` |
| PTPC | 1 | `0xF037C000` |
| PUART | 1 | `0xF4124000` |
| PWM | 2 | `0xF0420000`, `0xF0424000` |
| QEI | 2 | `0xF0400000`, `0xF0404000` |
| QEO | 2 | `0xF0410000`, `0xF0414000` |
| SDM | 1 | `0xF0450000` |
| SPI | 4 | `0xF0070000`, `0xF0074000`, `0xF0078000`, `0xF007C000` |
| SYNT | 1 | `0xF0464000` |
| SYSCTL | 1 | `0xF4000000` |
| TFA | 1 | `0x00400000` |
| TRGM | 1 | `0xF047C000` |
| TSNS | 1 | `0xF0090000` |
| UART | 8 | `0xF0040000` ~ `0xF005C000`（步长 0x4000） |
| USB | 1 | `0xF1420000` |
| XDMA | 1 | `0xF3100000` |

> 注意：**XPI0 控制器本身没有出现在这份 `*_BASE` 列表中**（SDK 通过 Flash 配置结构与 boot ROM API 访问它，外部映射地址是 `0x8000_0000`），所以不要用"查不到基地址"反推它不存在。

### 26.5.3 常用中断号（节选）

**表 26-4 本仓库例程最常用的中断号**（完整 79 条见 `docs/generated/soc-peripherals.md` 或 `python3 tools/gen_regmap.py --md-irq`）

| 中断宏 | 编号 | 用途 |
|---|---:|---|
| `IRQn_GPIO0_A` ~ `IRQn_GPIO0_Y` | 1~10 | GPIO0 各端口中断 |
| `IRQn_GPTMR0` ~ `IRQn_GPTMR3` | 11~14 | 通用定时器 |
| `IRQn_UART0` ~ `IRQn_UART7` | 19~26 | 8 路串口 |
| `IRQn_I2C0` ~ `IRQn_I2C3` | 27~30 | 4 路 I2C |
| `IRQn_SPI0` ~ `IRQn_SPI3` | 31~34 | 4 路 SPI（LAN8651 用 SPI2=33） |
| `IRQn_EWDG0` / `IRQn_EWDG1` | 38/39 | 看门狗 |
| `IRQn_HDMA` | 40 | HDMA |
| `IRQn_ADC0` / `IRQn_ADC1` | 42/43 | ADC16 |
| `IRQn_MCAN0` ~ `IRQn_MCAN3` | 46~49 | 4 路 CAN FD |
| `IRQn_PTPC` | 50 | 时间戳/1588 |
| `IRQn_QEI0` / `IRQn_QEI1` | 51/52 | 编码器 |
| `IRQn_PWM0` / `IRQn_PWM1` | 53/54 | PWMv2 |
| `IRQn_TRGM_0` / `IRQn_TRGM_1` | 56/57 | 触发矩阵 |
| `IRQn_ENET0` | 58 | 以太网 |
| `IRQn_USB0` | 60 | USB |
| `IRQn_XPI0` | 65 | Flash 控制器 |
| `IRQn_XDMA` | 67 | XDMA |

## 26.6 例程：用 letter-shell 现场核对寄存器

本章不新增例程，而是把现有 `samples/shell/letter_shell_basic/` 当成"在线寄存器浏览器"使用。它的 BSP 注册了 5 个与硬件核对直接相关的命令（实现见 `boards/hpm5e31_1/bsp/bsp_cmds.c`）：

```c
/* bsp_cmds.c 中的 reg 命令（节选）：读写任意 32 位寄存器 */
static void cmd_reg(int argc, char *argv[])
{
    uint32_t addr;
    if (argc < 2 || bsp_parse_u32(argv[1], &addr)) { printf("usage: reg <addr> [value]\r\n"); return; }
    volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) addr;
    if (argc >= 3) { uint32_t v; bsp_parse_u32(argv[2], &v); *p = v;
        printf("[0x%08lx] <- 0x%08lx\r\n", (unsigned long) addr, (unsigned long) v); }
    printf("[0x%08lx] = 0x%08lx\r\n", (unsigned long) addr, (unsigned long) *p);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 reg, cmd_reg, read/write 32-bit register: reg <addr> [value]);
```

上板后按下面配方核对本章表格（每条命令后都会打印实测值，可与 RM 复位值对照）：

```text
ver                         # 确认芯片型号/主频/SDK 版本
clk                         # 打印时钟树：cpu0/ahb0/xpi0/mchtmr
reg 0xF4000000              # SYSCTL 窗口第一个寄存器（对照 RM §13 复位值）
reg 0xF00D0000              # GPIO0 窗口（应能读出非全 0 的复位值）
mem 0xF0000000 32           # GPTMR0 寄存器窗口 hexdump
```

如果 `reg` 读某个外设窗口**恒定返回 0 或触发 HardFault**，优先怀疑：地址抄错（对照表 26-3）、外设时钟未使能（SYSCTL）、或该 IP 在 HPM5E31 上不存在（HPM5E00 系列内部有 HPM5E31/HPM5E3Y 两个型号，外设集合不同）。

## 26.7 上板实测数据

本章的"实测"用于**验证速查表本身可信**（而非逐条测外设），全部引用仓库 `logs/` 下的原始串口日志：

**表 26-5 本章引用的实测日志与关键数据**

| 日志路径 | 验证内容 | 关键输出 |
|---|---|---|
| `logs/20261030-104826-letter_shell_basic.log` | 芯片型号/主频/SDK 版本 | `soc: HPM5E31 (RV32-IMAFDCBP @ 480000000 Hz)`、`sdk: HPM SDK v1.13.0` |
| `logs/20261030-104826-letter_shell_basic.log` | 时钟树（`clk` 命令） | `cpu0 480000000 Hz`、`ahb0/axif/axis/axic 200000000 Hz`、`xpi0 100000000 Hz`、`mchtmr 24000000 Hz` |
| `logs/20261030-110853-bench_coremark.log` | mcycle 与 MCHTMR 一致性 | `cpu_hz=480000000 mchtmr_hz=24000000 diff_ppm=0.0` |
| `logs/20261030-112250-uart_dma.log` | 控制台波特率上限 | `udma_bench`：921600→90 KB/s、2000000→199 KB/s、3000000→307 KB/s，均 `err=0` |
| `logs/20261030-110857-gpio_speed.log` | GPIO 翻转速率基线 | 见日志（供第 4/23 章引用） |

两条结论：

1. **表 26-3 的地址与表 26-4 的中断号在本板上"能用"**：`ver`/`clk` 命令走的正是 UART0=19 中断与 `0xF0040000` 窗口；`udma_bench` 用 HDMA(IRQ 40) 搬运数据，3 Mbps 下 307 KB/s 无误码，说明 DMA 通道/中断映射与表中一致。
2. **主频闭环**：`clk` 输出的 480 MHz 与 `bench_coremark` 里用 mcycle/MCHTMR 反算的 `cpu_hz=480000000` 一致（`diff_ppm=0.0`），说明"PLL0=960 MHz、CPU=480 MHz、AHB=200 MHz、MCHTMR=24 MHz"这条时钟链在本板上成立（第 1、2、13 章有更详细推导）。

## 26.8 性能与调优

"查寄存器"本身不构成性能问题，但**错误的寄存器访问方式**会明显拖慢系统，常见三类及优化建议见表 26-6（数据为经验值，未逐项实测的已标注）：

**表 26-6 寄存器访问的性能陷阱与优化**

| 现象 | 根因 | 优化 | 依据 |
|---|---|---|---|
| 中断响应抖动大 | 中断里读慢速外设寄存器/打印 | 中断只置标志，处理移到主循环 | RM §6 中断延迟由 PLIC+流水线决定；实测见第 9/24 章 |
| 轮询外设 CPU 占用高 | 裸循环读状态位 | 改中断/DMA；或至少读一次状态缓存 | 本仓库 `udma_bench` 对比 UART 轮询与 DMA（`logs/20261030-112250-uart_dma.log`） |
| 代码跑在 Flash 上取指慢 | XPI0 100 MHz、无 cache | 热点函数放 ILM/AHB SRAM；或开 L1 cache | 第 02 章实测：`clk` 显示 `xpi0 100000000 Hz` |
| 寄存器写后立刻读回不一致 | 外设同步/总线缓冲 | 按 RM 要求插入读回或延时；不要假设 0 等待 | 待确认（需实测）：各 IP 写后读回延迟未逐项测量 |

> 待确认（需实测）：本章没有做"寄存器读写周期数"的微基准。验证方法：用 MCHTMR 计时，对同一寄存器连续读写 N 次求平均，分别在 AHB SRAM 与 Flash 取指两种情况下比较；结果应记入第 23/24 章。

## 26.9 坑与注意事项

下面是本仓库在实测/移植中**真实踩到或明确存在**的坑，按"现象 → 原因 → 解决"给出：

1. **现象**：板卡 README 写"外部 1 MB QSPI NOR"。
   **原因**：与 DS 矛盾。DS Rev0.4 p.1 写"1 MB 内置闪存"，p.68 §内置闪存特性写"本产品内置 1 MB 闪存"，型号为 GT25Q80AH（合封）；原理图上也没有外部 NOR 器件。
   **解决**：按 DS 表述为"片内/合封 Flash 1 MB，由 XPI0 映射到 `0x8000_0000`"；不要把本板当成 HPM6750 那种"外挂 NOR"方案（对外挂方案，烧写/启动流程完全不同）。

2. **现象**：代码里写 `HPM_FGPIO_BASE` 点亮 LED 没反应，写 `HPM_GPIO0_BASE` 正常。
   **原因**：FGPIO=`0x0030_0000`、GPIO0=`0xF00D_0000`，是两套完全不同的寄存器窗口；FGPIO 只支持部分功能且与 DLM 有历史勘误（其它系列 E00037）。
   **解决**：常规应用统一用 GPIO0；需要最高翻转速度再用 FGPIO，并在注释里写明地址。

3. **现象**：中断服务函数偶尔"跑飞"或进错分支。
   **原因**：PLIC claim 可能返回 0（SDK `trap.c:160` 的 workaround 注释），而 0 不是合法外设中断。
   **解决**：在公共入口里 `if (irq == 0) { return; }`；或直接复用 SDK 的 trap 处理。

4. **现象**：PWM 用外部触发（TRGM）更新比较值，部分通道比较值不更新。
   **原因**：**Errata E00038**（p.4）：`CMP_CFG<n>[CMP_UPDATE_TIME]=0b011` 时，只有 `CMP_CFG0~3` 的 `CMP_TRIG_SEL` 生效，分别作用于 CMP0/4/8/12、CMP1/5/9/13、CMP2/6/10/14、CMP3/7/11/15；其余 `CMP_CFG<n>` 的 `CMP_TRIG_SEL` 配置不生效。
   **解决**：同组比较值（如 CMP0/4/8/12）配置**相同**的 `CMP_TRIG_SEL`；或把 `CMP_UPDATE_TIME` 改成 `0b011` 以外的值。官方修正情况："无"。

5. **现象**：STOP 模式唤醒后 CPU 异常复位/挂死。
   **原因**：**Errata E00042**（p.5）：停止模式下 CPU0 复位或掉电、且系统总线时钟跟随关闭时，CPU 可能唤醒异常。
   **解决**：若 `SYSCTL.RETENTION[CPU0]` 的 bit3 设为 0（确实要复位/掉电 CPU0），必须把 `RESOURCE[AXIS].MODE` 设为强制打开，保证总线时钟一直运行；要更低功耗请改用休眠模式（STANDBY）。官方修正："无"。

6. **现象**：从 HPM6200/HPM6300 项目抄来的"UART DMA 限制"规避代码，在 5E31 上找不到对应勘误。
   **原因**：勘误是**按系列**发布的。HPM5E00 勘误 Rev0.1 只有 E00038/E00042 两条；UART 起始位偏短（E00035）、UART DMA（E00018）、SPI0 RX DMA（E00036）、ADC16 WDOGx（E00023）等条目在 5E00 勘误表中**不存在**。而且 SDK 的 5E31 头文件里 `HPM_IP_FEATURE_UART_E00018_FIX` 被定义为 1（`hpm_soc_ip_feature.h:25`），说明该 IP 在该型号上已修正。
   **解决**：不要跨系列照抄规避代码；以"本系列勘误表 + SDK feature 宏"为准。反过来，勘误表没有不等于没有问题——5E00 勘误是**内部版 Rev0.1**，条目可能继续增加。

7. **现象**：查资料时看到"E00028 = MCAN 消息 RAM 并发访问"，在 5E00 上却查不到。
   **原因**：**同号不同义**。E00028 在 HPM6200 是 MCAN 消息存储器问题，在 HPM6700/6400 是 PDMA 16×16 block 缩放黑竖线。跨系列引用勘误必须看清系列。
   **解决**：引用格式统一写 `Errata E000xx（系列）`；本章表 26-7 给出 5E00 与邻近系列的对照。

8. **现象**：中断号按"UART0=18"写死，串口中断不触发。
   **原因**：不同系列/不同型号 IRQ 编号会变（HPM6300 的 UART0 是 18，HPM5E31 是 19）。硬编码数字是移植事故高发点。
   **解决**：始终用 `IRQn_UART0` 宏（来自 `hpm_soc_irq.h`），不要抄数字。

9. **现象**：外设寄存器读回全 0，但代码逻辑"看起来没问题"。
   **原因**：外设时钟未使能（SYSCTL 的时钟门控）或外设仍在复位态。总线访问不会报错，只返回 0。
   **解决**：按"开时钟 → 释放复位 → 配 IOC → 配外设"顺序初始化；用 `reg` 命令读 SYSCTL 对应时钟寄存器确认。

10. **现象**：在 HPM5E3Y 型号上编译通过的地址，换 HPM5E31 后某些 IP 不存在。
    **原因**：HPM5E00 系列内部有 HPM5E31/HPM5E3Y 等型号，外设集合不同（`soc/HPM5E00/` 下每个型号一个目录）。
    **解决**：以 `hpm_soc_ip.h`/`hpm_soc_ip_feature.h` 为准做条件编译；本项目固定 HPM5E31IPB1。

11. **现象**：调试器读写外设寄存器正常，但程序访问 HardFault。
    **原因**：RISC-V 的 PMP/物理内存保护或外设时钟域未就绪时，CPU 访问可能触发异常，而调试器走系统总线不受同样限制。
    **解决**：先确认时钟/复位，再怀疑 PMP 配置；用 `mcause/mepc/mtval` 定位（见第 02 章调试小节与图 26-2）。

12. **现象**：看门狗频繁复位，但业务代码"没死"。
    **原因**：喂狗任务被高优先级中断/长临界区饿死；或调试断点时看门狗继续计数。
    **解决**：喂狗放在最低优先级任务并留余量；调试时用 `EWDG` 的调试模式或先关狗（第 22 章）。

## 26.10 勘误清单与跨系列对照

### 26.10.1 HPM5E00 全部勘误（Rev0.1，芯片版本 1.0，适用 HPM5E31IPB1 等）

**表 26-7 HPM5E00 系列勘误汇总**（摘自《HPM5E00 系列勘误表 Rev0.1》，页码为 PDF 阅读器页码）

| 编号 | 问题 | 影响版本 | 规避方法 | 修正 | 关联章节 | 页 |
|---|---|---|---|---|---|---|
| E00038 | PWMv2 外部触发更新比较值：`CMP_CFG<n>[CMP_UPDATE_TIME]=0b011` 时仅 `CMP_CFG0~3` 的 `CMP_TRIG_SEL` 有效（分组 0/4/8/12、1/5/9/13、2/6/10/14、3/7/11/15） | 1.0 | 同组共享相同 `CMP_TRIG_SEL`，或不用 `0b011` | 无 | 第 14/17 章 | p.4 |
| E00042 | 停止模式 CPU0 复位/掉电且系统总线时钟关闭 → 唤醒异常 | 1.0 | `RETENTION[CPU0].bit3=0` 时强制打开 `RESOURCE[AXIS].MODE`；更低功耗用休眠模式 | 无 | 第 22 章 | p.5 |

> 芯片识别：勘误适用于型号版本位为 **1** 的芯片，例如 `HPM5E31IPB1`（末位 1）；本项目板卡即该型号。实测日志 `logs/20261030-104826-letter_shell_basic.log` 中 `soc: HPM5E31 (...) @ 480000000 Hz` 可佐证型号。

### 26.10.2 与邻近系列的"同义不同号 / 同号不同义"

**表 26-8 同一问题在不同系列的勘误编号对照**（"—"表示该系列勘误表未列此条，不等于硬件无此问题）

| 问题 | HPM5E00 | HPM6P00 | HPM6800 | HPM6700/6400 | HPM5300/6200/6300 |
|---|---|---|---|---|---|
| PWMv2 外部触发更新 CMP | E00038 | E00038 | — | — | — |
| 停止模式系统总线时钟 | E00042 | E00042 | E00041 | E00040（主题是子系统关电，相关但不同条） | — |
| UART 起始位偏短 | — | E00035 | E00035 | E00035 | E00035 |
| UART DMA 请求限制（E00018 类） | —（SDK 宏 `UART_E00018_FIX=1`） | — | — | E00018 | 6200/6300 E00018 |
| ADC16 WDOGx 清 0 / SEQ_CMPT | — | — | E00023/E00024 | E00023/E00024 | E00023/E00024 |
| SPI0 RX DMA 请求失效 | — | — | E00036 | E00036 | 6200/6300 E00036 |
| PIOC 读值错误 | — | — | — | E00011 | 6300 E00011（2.0 已修） |
| GPIOM PE/PF 联动 | — | — | E00022 | E00022 | — |
| 高分辨率 PWM 通道/故障恢复 | — | — | — | — | 6200 E00021/E00030 |

> 本表的用法：**先按芯片型号确认系列，再查该系列勘误表**。跨系列"借"规避代码前，必须确认 IP 版本相同（看 `hpm_soc_ip_feature.h` 的 feature/errata 宏）。

## 26.11 优劣评价与同类对比

**优势**：

- 统一编址 + 完整 SDK 头文件宏，地址/中断号几乎不需要背；`tools/gen_regmap.py` 可随时从 SDK 重新生成，天然防抄错。
- 勘误数量少（5E00 Rev0.1 仅 2 条），且都集中在 PWM/低功耗两个高级特性上，基础外设（UART/SPI/I2C/CAN）没有已知限制条目——这对"先用起来"很友好。对比 HPM6700/6400 的 27 条勘误，5E00 的移植负担明显更小。
- PLIC 支持 S 模式与 31 级优先级，中断资源比多数 Cortex-M 的 NVIC（16 级、固定向量表）灵活。

**局限**：

- 勘误表为**内部版 Rev0.1（2025-03-07）**，条目数少不等于问题少；新项目量产前应复查官网最新版。
- 部分 IP 与邻近系列共享（PWMv2、UART、ADC16），但勘误编号与修正状态不通用，跨系列移植时不能"一套规避走天下"。
- 与 STM32H7/TC397 相比，HPM 生态的第三方调试工具/社区文章少，查问题时更依赖官方 RM/勘误与 SDK 源码（第 27 章有详细对比与扩展阅读）。

![图 26-2 HPM5E31 上电/运行异常排查决策树](figures/26/fig-26-2-debug-tree.png){width=72%}

**图 26-2** 把本章的"坑"整理成排查顺序：先确认"有没有输出"（供电/BOOT/串口），再分"卡死/复位"（异常现场）与"外设不工作"（时钟/复用/地址），最后落到通信类外设与低功耗两条专线。每一步的具体命令与寄存器见本章第 4、7 节。

## 26.12 参考来源

1. 《HPM5E00 系列高性能微控制器勘误表》Rev 0.1（2025-03-07）：E00038（p.4）、E00042（p.5）、版本信息（p.6）。
2. 《HPM5E00 数据手册》Rev 0.4：p.1"1 MB 内置闪存"、p.68"内置闪存特性（本产品内置 1 MB 闪存，型号 GT25Q80AH）"。
3. 《HPM5E00 用户手册》Rev 0.4：§6 平台级中断控制器 PLIC、§13 系统控制模块 SYSCTL、§16 系统内存映射（p.57）。
4. HPM SDK v1.13.0（只读参考）：`soc/HPM5E00/HPM5E31/hpm_soc_ip.h`、`hpm_soc_irq.h`、`hpm_soc_ip_feature.h:25`、`toolchains/trap.c:160`。
5. 本仓库脚本：`tools/gen_regmap.py`（地址/IRQ 表生成）、`tools/latex_breaks.py`（PDF 排版断行）、`tools/figs/ch26-memory-map.py`、`tools/figs/ch26-crash-tree.py`。
6. 实测日志：`logs/20261030-104826-letter_shell_basic.log`、`logs/20261030-110853-bench_coremark.log`、`logs/20261030-112250-uart_dma.log`、`logs/20261030-110857-gpio_speed.log`。
7. 本文插图：图 26-1 由 `tools/figs/ch26-memory-map.py` 生成（数据来自 SDK 头文件）；图 26-2 由 `tools/figs/ch26-crash-tree.py` 生成（graphviz）。

## 26.13 扩展阅读

1. [HPM SDK（官方，GitHub）](https://github.com/hpmicro/hpm_sdk)——驱动、SoC 头文件、例程的权威来源。
2. [HPM Apps（官方应用方案）](https://github.com/hpmicro/hpm_apps)——电机、以太网、USB 等参考设计。
3. [letter-shell（开源嵌入式 shell）](https://github.com/NevermindZZT/letter-shell)——本仓库调试框架的上游项目。
4. [先楫半导体官网](https://www.hpmicro.com/)——DS/RM/勘误/工具的最新版本下载入口。
5. 本仓库第 02 章（构建/烧写/调试）、第 22 章（低功耗与看门狗）、第 14/17 章（PWM/TRGM，E00038 的实际影响面）。

## 26.14 待确认项与风险

> 已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，无法实测的降级为已知限制并给出验证命令。

| 项 | 状态 | 验证方法/风险 |
|---|---|---|
| 寄存器读写周期数（性能微基准） | **已知限制** | 需要自定义循环并用 mcycle 计时；本书的寄存器访问都隐含在例程里，单独微基准对选型价值低。验证方法：写一个 while 循环连续 `volatile` 读同一寄存器 1 万次，分别在 ILM/Flash 取指下用 `hpm_csr_get_core_mcycle()` 求平均 |
| `reg 0xF4000000` 等 SYSCTL 寄存器实测值 | **已知限制（读取方法已给）** | 上板执行 `reg 0xF4000000` 等命令即可读回，与 RM §13 复位值逐位对照；本轮没有独占窗口做全量对照，留作复测项 |
| E00038 在本板 PWM 例程中的实际影响 | **已实测（2026-11-02）** | CMP0/CMP4 同一 TRGM 触发源、影子寄存器交替写 100 次×3 模式，判据为中断里读 `CNT_VAL`（24.8 定点右移 8）：三种模式均 **0/100 异常**，本板未复现“更新丢失”；mode 2（CMP4 的 `TRIG_SEL` 写 1）仍跟随触发 0 更新，证实“同组共用 `CMP_CFG0` 的 `TRIG_SEL`”。规避建议不变。详见第 14 章 §14.7.3 与 `logs/20261102-125859-pwm_basic.log` |
| E00042 的 STOP 模式唤醒路径 | **已定位（部分实测：现象已复现，机理待对照）** | 新例程 `samples/power/stop_wake`：STOP = retention + system LP + WFI，GPTMR1 作唤醒源，预置 3 s EWDG 兜底。结果：retention=0x3F 时第一次 WFI 就不返回，3 s 后被 EWDG 复位（`BOOT_RESET=0x00010000`）；补 SDK 的 pcfg_dcdc 配置后仍卡死。说明 **GPTMR 很可能不在 STOP 可唤醒域**（SDK 的 enter_stop_mode 示例只用 WKUP 引脚）。不能据此认定就是 E00042；下一步：唤醒源换 WKUP 引脚 + retention 0x37，`RESOURCE[AXIS].MODE` auto/force_on 对照。日志 `logs/20261102-124252-stop_wake-r5d.log` |
| 5E00 勘误是否已有更新版本 | **已知限制** | 本地只有厂商内部版 Rev0.1（2025-03-07），官网未公开勘误页；新版发布后重跑本章表 26-7/26-8 |
| 表 26-3 全量地址与硅片一致性 | **已知限制（风险待实测）** | 地址来自 SDK 头文件，非逐条实测；用 `reg`/`mem` 抽样验证（尤其 XPI0 与 PGPIO 等特殊窗口）即可，不必全量 |
| 板卡 README 与 DS 的 flash 表述矛盾 | **已定位（已修正）** | 根目录 README 与本书封面/图 26-1 已统一为"片内/合封 1 MB Flash（GT25Q80AH），无外部 NOR" |
| 中断向量表与 PLIC 优先级分配 | **已知限制** | 需两个不同优先级中断同时触发测抢占；本书只做了单中断延迟（第 23 章 212 ns）。方法：GPTMR0 低优先级 + GPIO 高优先级同时触发，读两者 ISR 入口时间差 |
