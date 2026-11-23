# 第 15 章 GPTMR 定时、捕获与测量

## 15.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 |
| 对应例程 | `samples/gptmr/gptmr_timer_irq/`、`samples/gptmr/gptmr_capture_measure/`、`samples/pwm/pwm_basic/`（时基用法） |
| 实测日期 | 2026-10-30 |
| 可信度 | 高：寄存器与机制来自 UM Rev0.4 §44/§45；周期与抖动为上板实测（见 `logs/`） |

## 15.2 本章导览

- **学到什么**：GPTMR 的 4 通道结构、32 位计数器/重载/双比较器、同步机制、输入捕获与
  "测周期/测占空比"两种用法；如何在**没有示波器**的条件下用 GPTMR + 中断时间戳量出
  PWM 的频率、占空比和中断抖动，并知道这些数字的可信边界。
- **前置章节**：第 02 章（构建/烧写/串口）、第 03 章（letter-shell 命令框架）、
  第 14 章（PWMv2，本章的许多被测信号由它产生）。

## 15.3 概述与典型应用

GPTMR（General Purpose Timer）是 HPM5E00 的通用定时器：一个模块 4 个通道，每通道一个
32 位向上计数器、一个 32 位重载寄存器、两个输出比较器和一路输入捕获单元（UM §45.1, p.743）。
它便宜、灵活、几乎不占引脚，是**软件时基、周期中断、脉冲测量、软件 PWM** 的默认选择。

典型应用：

| 应用 | 用法 | 本章/例程 |
|---|---|---|
| 周期任务节拍 | 通道重载中断，1 kHz~10 kHz | `gptmr_timer_irq` |
| 无示波器测 PWM | 自由运行通道当时间戳，PWM 中断里读 | `pwm_basic`、本章 §15.7 |
| 外部脉冲测频/测宽 | 捕获上升/下降沿，读 CAPPOS/CAPNEG | 官方 `gptmr/pwm_measure` |
| 软件 PWM / 步进脉冲 | 输出比较翻转 + 引脚复用 | 官方 `gptmr/pwm_generate` |
| 多轴同步启动 | SWSYNCT 或 SYNCI 硬件同步 | 官方 `gptmr/counter_sync` |

表 15-1：GPTMR 典型应用与对应例程

### 15.3.1 资源与实例

| 实例 | 基地址 | 中断 | 时钟 | 备注 |
|---|---|---|---|---|
| GPTMR0 | 0xF0000000 | IRQn_GPTMR0 | `clock_gptmr0` | 本章例程使用 |
| GPTMR1 | 0xF0004000 | IRQn_GPTMR1 | `clock_gptmr1` | 常被 RTOS 时基占用 |
| GPTMR2 | 0xF0008000 | IRQn_GPTMR2 | `clock_gptmr2` | |
| GPTMR3 | 0xF000C000 | IRQn_GPTMR3 | `clock_gptmr3` | **本板被 `board_delay_us/ms` 占用** |

表 15-2：HPM5E31 的 GPTMR 实例

> ⚠ 本板 `boards/hpm5e31_1/board.h` 把 `BOARD_DELAY_TIMER` 定义为 GPTMR3 通道 0，
> 因此**不要用 GPTMR3 做自己的定时器**，否则 `board_delay_ms()` 会与你的配置互相破坏。
> 本章例程一律使用 GPTMR0。

**实测时钟**：`clock_get_frequency(clock_gptmr0)` = **100 000 000 Hz**（1 tick = 10 ns），
这是 AHB 200 MHz 的一半，见 `logs/20261030-110408-gptmr_timer_irq.log` 里的
`gptmr0 clock = 100000000 Hz`。

## 15.4 工作原理

![图 15-1 GPTMR 通道结构](figures/15/fig-15-1-gptmr-block.png)

### 15.4.1 时间基准

- 计数器 `CHANNEL[n][CNT]` 为 32 位，**只向上计数**；使能 `CR[CEN]` 后从 0 开始，
  计到 `RLD` 触发重载事件（`SR.CHxRLDF` 置 1）并回到 0（UM §45.2.1.1, p.744）。
- 重载值写 `CHANNEL[n][RLD]`；运行中改计数器需要走 `CNTUPTVAL + CR[CNTUPT]` 两步
  （UM §45.2.1.1, p.744）。
- 计数器可被 `CR[CNTRST]` 异步清零；清零同时会复位捕获值寄存器和输出比较电平。
- **周期与频率**：`T_irq = RLD × (1 / f_gptmr)`。本板 1 kHz 对应 `RLD = 100000`。
  注意 SDK 驱动 `gptmr_channel_config()` 写寄存器时对非零值做 `reload - 1`，所以
  软件侧填 `f/f_irq` 得到的实际周期就是 `f/f_irq` 个 tick，**不要再手动减 1**。

### 15.4.2 同步

UM §45.2.1.2（p.744）给了三种同步：

| 方式 | 寄存器位 | 用途 |
|---|---|---|
| 软件同步 | `CR[SWSYNCIEN]` + `GCR[SWSYNCT]` | 多个通道同时从 0 起跑（本章例程用它同步两个捕获通道） |
| 硬件同步 | `CR[SYNCIREN/SYNCIFEN]` + SYNCI 输入 | 外部事件对齐计数 |
| 通道跟随 | `CR[SYNCFLW]`（n>0） | 通道 n 跟随通道 n-1 同步 |

表 15-3：GPTMR 三种同步方式

**为什么同步重要**：如果两个通道各跑各的计数器，它们的捕获值分别以各自的 0 点为基准，
时间戳不可直接相减。本章 `pwm_basic` 用 `gptmr_trigger_channel_software_sync()` 把
CH2/CH3 同时清零，才能比较两个时间戳。

### 15.4.3 输出比较与 PWM 生成

UM §45.2.2（p.744）：`CR[CMPINIT]` 决定计数开始时的输出电平，`CMP0/CMP1` 两个比较值
处输出翻转，重载时输出复位到 `CMPINIT` 电平。因此：

- 一个通道最多产生 **2 个边沿/周期**，即占空比可自由设置的一路 PWM；
- `CMP0 == CMP1` 时输出不翻转（用来关掉波形）。

### 15.4.4 输入捕获与测量

捕获单元把"边沿到来的时刻"写进 `CAPPOS`（上升沿）/ `CAPNEG`（下降沿），
另有硬件自动测量寄存器：

| 寄存器 | 含义 | 驱动 API |
|---|---|---|
| `CHANNEL[n][CAPPOS]` | 上升沿时刻的计数器值 | `gptmr_channel_get_counter(ch, gptmr_counter_type_rising_edge)` |
| `CHANNEL[n][CAPNEG]` | 下降沿时刻的计数器值 | `..._falling_edge` |
| `CHANNEL[n][CAPPRD]` | 硬件测得的信号周期 | `..._measured_period` |
| `CHANNEL[n][CAPDTY]` | 硬件测得的高电平时间 | `..._measured_duty_cycle` |

表 15-4：GPTMR 捕获结果寄存器与驱动接口

工作模式由 `gptmr_work_mode_t` 选择：`no_capture`/`capture_at_rising_edge`/
`capture_at_falling_edge`/`capture_at_both_edge`/`measure_width`
（`hpm_sdk/drivers/inc/hpm_gptmr_drv.h:67-73`）。

![图 15-3 GPTMR 捕获时序](figures/15/fig-15-3-capture-timing.png)

**捕获引脚从哪来**：通道 0/1 的捕获输入来自 IOC 引脚复用；**通道 2/3 的捕获输入还可以
来自 TRGM**（`HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_2/3`），这就是第 14 章把 PWM 的
TRGO 送进 GPTMR 做纯硬件测量的依据。代价见 §15.10 坑 15-3。

### 15.4.5 输入监测（Monitor）

HPM5E00 的 GPTMR 带 `HPM_IP_FEATURE_GPTMR_MONITOR`（`hpm_soc_ip_feature.h:16`）：
可在硬件里判断输入信号的周期/高电平是否超出 `max_value/min_value`，
超限直接置标志/中断，适合做"编码器断线检测""PWM 丢失检测"。

## 15.5 关键寄存器与驱动 API 对照

| 功能 | 寄存器 | 位域/取值 | 驱动 API | 例程位置 |
|---|---|---|---|---|
| 计数器使能 | `CHANNEL[n][CR]` | `CEN` | `gptmr_start_counter()` | `gptmr_timer_irq/src/main.c` |
| 计数器清零 | `CHANNEL[n][CR]` | `CNTRST` | `gptmr_channel_reset_count()` | 同上 |
| 重载值 | `CHANNEL[n][RLD]` | 32 位 | `gptmr_channel_config(config.reload)` | 同上 |
| 输出比较值 | `CHANNEL[n][CMP0/1]` | 32 位 | `config.cmp[0/1]` | — |
| 捕获模式 | `CHANNEL[n][CR]` | `CAPMODE` | `gptmr_channel_config(config.mode)` | `pwm_basic`（时基） |
| 捕获结果 | `CAPPOS/CAPNEG` | 32 位 | `gptmr_channel_get_counter()` | §15.7 实测 |
| 硬件测周期/高电平 | `CAPPRD/CAPDTY` | 32 位 | `gptmr_counter_type_measured_*` | 官方 `gptmr/pwm_measure` |
| 重载中断使能 | `SR/IRQEN` | `CHxRLD` | `gptmr_enable_irq(GPTMR_CH_RLD_IRQ_MASK(ch))` | `gptmr_timer_irq` |
| 捕获中断使能 | `SR/IRQEN` | `CHxCAP` | `gptmr_enable_irq(GPTMR_CH_CAP_IRQ_MASK(ch))` | 官方 `gptmr/input_capture` |
| 软件同步 | `GCR` | `SWSYNCT` | `gptmr_trigger_channel_software_sync()` | `pwm_basic` |
| 监测阈值 | `MONITOR` | `max/min` | `config.monitor_config` | — |
| 一次性模式 | `CR` | `OPMODE` | `config.enable_opmode` | 官方 `gptmr/opmode` |

表 15-5：GPTMR 寄存器与驱动 API 对照

## 15.6 例程逐条讲解

### 15.6.1 gptmr_timer_irq：周期中断 + 中断抖动统计

| 项 | 内容 |
|---|---|
| 路径 | `samples/gptmr/gptmr_timer_irq/` |
| 覆盖特性 | 周期中断、32 位自由运行时基、软件同步不清零时间基、中断抖动统计 |
| 关键配置 | GPTMR0 CH0 `RLD = f_gptmr/rate`（默认 100 Hz~200 kHz，默认 1 kHz）；CH3 `RLD = 0xFFFFFFFF` 自由运行 |
| 实测现象 | 串口每 2 s 打印周期均值/最小/最大/峰峰抖动/标准差/ppm 误差 |
| 实测日志 | `logs/20261030-110408-gptmr_timer_irq.log` |

CH0 的重载中断里读 CH3 的 `CNT` 当时间戳，两次中断之差就是实际周期：

```c
SDK_DECLARE_EXT_ISR_M(APP_GPTMR_IRQ, gptmr_isr)
void gptmr_isr(void)
{
    if (gptmr_check_status(APP_GPTMR, GPTMR_CH_RLD_STAT_MASK(APP_IRQ_CH))) {
        gptmr_clear_status(APP_GPTMR, GPTMR_CH_RLD_STAT_MASK(APP_IRQ_CH));
        now = gptmr_channel_get_counter(APP_GPTMR, APP_FREE_CH, gptmr_counter_type_normal);
        dt = now - s_last_tick;          /* 单位: 10 ns */
        s_last_tick = now;
        /* 只做整数累加, 保持 ISR 短小 */
        s_sum_dt += dt;
        s_sum_sq += (uint64_t) dt * dt;
        s_cnt++;
    }
}
```

### 15.6.2 gptmr_capture_measure：输入捕获测频率/占空比/抖动

| 项 | 内容 |
|---|---|
| 路径 | `samples/gptmr/gptmr_capture_measure/` |
| 覆盖特性 | `measure_width` 硬件测周期/占空比、捕获中断、TRGM 内部路由、外部引脚捕获入口 |
| 关键配置 | CH3 输出比较产生 1 kHz/25% 方波（可 `crate` 改）；CH2 捕获；`mode = gptmr_work_mode_measure_width`；外部入口 PC00(`TRGM_P_00`) |
| 实测现象 | 片内 GPTMR 比较输出 → TRGM 的**输入源在本板不送达**（cap=0），见坑 15-7；外部 PC00 入口已配好，接信号发生器即可验证 |
| 实测日志 | `20261030-122956-gptmr_capture_measure.log` |

```c
/* 硬件自动测周期/高电平: measure_width 模式, 中断里读 CAPPRD/CAPDTY */
cfg.mode = gptmr_work_mode_measure_width;
gptmr_channel_config(GPTMR, CAP_CH, &cfg, false);
gptmr_start_counter(GPTMR, CAP_CH);
/* 中断里 */
prd = gptmr_channel_get_counter(GPTMR, CAP_CH, gptmr_counter_type_measured_period);
hgh = gptmr_channel_get_counter(GPTMR, CAP_CH, gptmr_counter_type_measured_duty_cycle);
```

## 15.7 捕获例程实测与已知限制

| 项目 | 实测/结论 | 证据 |
|---|---|---|
| GPTMR0 时钟 | 100 000 000 Hz（1 tick = 10 ns） | 启动打印 |
| 被测源配置 | CH3 输出比较 1 kHz / 25%，CH2 捕获 | `crate 2000 500` 回读 |
| 内部自环（GPTMR_OUT3→TRGM→CAPT_2） | **捕获 0 次** | `cstat` 反复打印"还没有捕获事件" |
| 外部入口 | PC00→`TRGM_P_00`→CAPT_2 已配置，未接信号 | `csrc 1` |

> 这与第 17 章坑 17-7 / 待确认 17-F 是同一个现象：**本板上 GPTMR 比较输出、PLB 输出
> 这类"内部信号源"送到 TRGM 后，GPTMR 捕获通道收不到事件**；而 PWM 的 TRGO 是能送达的
> （第 14 章 PWM TRGO6→CAPT_2 实测成功）。因此本章例程保留外部 PC00 输入通道作为主用法。

## 15.8 上板实测数据

### 15.8.1 1 kHz 周期中断

测量方法：`bash scripts/hwrun.sh gptmr/gptmr_timer_irq --secs 8 --send "ginfo;gstat"`，
在板上连续采样 682 个中断间隔（日志 `logs/20261030-110408-gptmr_timer_irq.log`）。

| 指标 | 实测值 | 换算 | 说明 |
|---|---|---|---|
| GPTMR0 时钟 | 100 000 000 Hz | 10 ns/tick | `gptmr0 clock` |
| 重载值 | 100 000 | 1 ms | `CH0 rx reload=100000` |
| 采样次数 n | 682 | — | `cnt=683`（首个间隔不计） |
| 周期均值 | 1000.000 us | 100 000.0 tick | 与设定一致 |
| 周期最小 | 999.520 us | 99 952 tick | 比均值小 48 tick |
| 周期最大 | 1000.530 us | 100 053 tick | 比均值大 53 tick |
| 峰峰抖动 | 1.010 us | 101 tick | 单次最长-最短 |
| 标准差 σ | 0.194 us | 19.4 tick | 抖动主体 |
| 频率误差 | +0.23 ppm | — | 均值量化误差 |

表 15-6：1 kHz 周期中断实测（`logs/20261030-110408-gptmr_timer_irq.log`）

![图 15-2 实测中断周期抖动](figures/15/fig-15-2-irq-jitter.png)

**怎么读这组数**：

- 均值与设定值只差 0.23 ppm（1 ms 里 0.23 ns），**计数器与重载值本身没有累积误差**；
- ±0.5 us 的峰峰抖动不是定时器抖动，而是**中断响应时刻的软件抖动**：CPU 可能正在执行
  printf 的循环、或刚进别的中断。要量定时器硬件抖动必须测引脚（示波器）。
- σ = 19.4 tick（194 ns）与 480 MHz 主频下"进入/退出中断的可变开销"量级吻合。

### 15.8.2 修改频率

`grate <hz>` 可在线改频率（`RLD = f_gptmr / hz`）。频率上限受两件事限制：

```text
hz_max_realistic ≈ 1 / (ISR 执行时间 + 中断进出开销)
```

本板 ISR 约 30 条指令（含 64 位乘加），1 kHz 下占用率 < 0.1%；上到 200 kHz 时单次 ISR
已接近周期本身，抖动会急剧恶化。不同频率下的抖动对比属于**待确认（需实测）**，
验证方法见 §15.13。

## 15.9 性能与调优

1. **ISR 越短越好**：本示例 ISR 只读一个寄存器 + 4 个标量累加。把
   `printf`、除法、浮点放进 ISR 是抖动的主要来源。
2. **不要在 ISR 里做 64 位除法**：RISC-V 没有硬件 64 位除法，`__udivdi3` 需要上百周期。
   统计算法放到主循环（本例 `print_stats()`）。
3. **高频率用"多通道分工"**：需要 100 kHz 以上时，让 GPTMR 只负责计数 + DMA 搬运，
   CPU 批量处理。
4. **时间基通道不要重置**：CH3 自由运行时基一旦被 `CNTRST` 或同步清零，时间戳会跳变，
   统计要相应清零。
5. **需要硬件级精度**时改用捕获模式（§15.4.4），让 GPTMR 硬件记录边沿时刻，
   软件只读结果，可把抖动压到 1 tick。

## 15.10 坑与注意事项

1. **现象**：`clock_get_frequency(clock_gptmr0)` 返回 0，`reload` 算成 0，中断不来。
   **原因**：板级 `init_board_clock()` 只把 PWM/QEI/PLB 等加进 clock group，
   GPTMR 需要单独 `init_gptmr0_clock()`（`boards/hpm5e31_1/clock.c:225`）。
   **解决**：例程里显式调用，且检查返回值非 0 再继续。
2. **现象**：中断周期是设定值的 2 倍或计数器不动。
   **原因**：`gptmr_channel_config(..., enable=false)` 的最后一个参数是"是否立刻使能通道"，
   传 false 之后必须再 `gptmr_start_counter()`。
   **解决**：见 `gptmr_timer_irq` 的 `tmr_setup()`。
3. **现象**：用 PWM 的 TRGO 经 TRGM 送进 GPTMR 捕获，实测周期忽大忽小、计数严重偏少
   （50 kHz 时 2 s 只捕到 3533 个周期，理论上应 10 万个）。
   **原因**：PWMv2 的 trigger-out 是 **1 个 PWM 时钟周期的窄脉冲**（200 MHz 下 5 ns），
   而 GPTMR 时钟 100 MHz（10 ns/拍），跨时钟域采样会大量漏掉脉冲。
   **解决**：要么用引脚 + 示波器/逻辑分析仪，要么像 `pwm_basic` 那样用
   **PWM 中断 + GPTMR 自由运行时基**打时间戳。实测证据见
   `logs/20261030-113*-pwm_basic.log`。
4. **现象**：两个捕获通道的时间戳相减得到负数或离谱值。
   **原因**：两个通道的计数器没有同步，0 点不同。
   **解决**：`gptmr_trigger_channel_software_sync()` 同时清零（UM §45.2.1.2, p.744）。
7. **现象**：GPTMR 的比较输出（OUT2/OUT3）经 TRGM 送进另一个 GPTMR 捕获通道，
   捕获计数恒为 0；同样地 PLB 输出经 TRGM 也捕获不到。
   **原因**：本板实测这些"内部信号源"到 TRGM 后再进捕获通道不生效，而 PWM 的
   `PWM0_TRGO_n` 到 `GPTMR0_CAPT_2` 是能送达的（第 14 章实测成功）。
   **解决**：捕获外部信号请走引脚入口（如 PC00 = `TRGM_P_00`，本例程 `csrc 1`）；
   纯片内自环请改用"PWM TRGO → 捕获"并接受窄脉冲漏采（见坑 15-3）。
   完整现象与排查见第 17 章待确认 17-F。
5. **现象**：用了 GPTMR3 之后 `board_delay_ms()` 变得不准甚至卡死。
   **原因**：本板 `BOARD_DELAY_TIMER = GPTMR3`（`board.h`），被 BSP 占用。
   **解决**：改用 GPTMR0/1/2。
6. **现象**：照抄官方 `gptmr/pwm_measure` 的 README，写了 `config.prescaler` 编译不过。
   **原因**：该字段只在定义了 `HPMSOC_HAS_HPMSDK_GPTMRV2` 的芯片上存在；HPM5E00
   走的是 `hpm_gptmr_drv.h`（`GPTMR_Type`），**没有 prescaler**，重载/比较是 32 位
   （UM §45.1 p.743），不是 README 里说的 24 位。
   **解决**：以 `hpm_soc_ip_feature.h` 的能力宏为准。

## 15.11 优劣评价与同类对比

| 项目 | HPM GPTMR（本板 4 个） | STM32 TIM2/5（32 位通用） | TC397 GTM（TOM/ATOM） |
|---|---|---|---|
| 计数器位宽 | 32 位 | 32 位 | 24 位（TOM） |
| 每模块通道 | 4 | 4 | 8（TOM 每簇） |
| 比较器/通道 | 2 | 4（CCR） | 2（SR0/SR1） |
| 输入捕获 | 有，通道 2/3 可经 TRGM 内部路由 | 有（引脚） | 有（TIM 输入选择矩阵） |
| 硬件测周期/占空比 | 有（CAPPRD/CAPDTY） | 无专用寄存器 | 有（TIM 测量模式） |
| 内部互联 | TRGM 全交叉 | 有限（TIM 触发选择） | 全交叉（TIM） |
| 适合场景 | 通用时基/测量/软件 PWM | 通用 | 电机/功率级复杂波形 |

表 15-7：GPTMR 与同类通用定时器对比（STM32/TC397 数据来自厂商公开手册，非本板实测）

**评价**：GPTMR 的强项不在"波形生成"（那是 PWMv2 的活），而在
**(1) 32 位免费的时间基**、**(2) 与 TRGM 的任意互联**、**(3) 硬件周期/占空比测量**。
在本项目里它最好的用法是当"测量仪"：给 PWM/QEI/SDM 做交叉验证。

## 15.12 参考来源

- HPM5E00 用户手册 Rev0.4：§44 定时器概述（PDF p.742）、§45 定时器TMR（PDF p.743~746）
- HPM5E00 数据手册 V0.4：定时器资源表（PDF p.12）
- SDK：`hpm_sdk/drivers/inc/hpm_gptmr_drv.h`（模式与 API）、
  `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_ip_feature.h:16-19`（MONITOR/OPMODE/CNT_MODE/BURST）
- 官方例程：`hpm_sdk/samples/drivers/gptmr/timer_basic`、`input_capture`、`pwm_measure`、
  `counter_sync`、`opmode`、`monitor`
- 本仓库实测日志：`logs/20261030-110408-gptmr_timer_irq.log`
- 相互关系：第 14 章（PWMv2，被测信号源）、第 17 章（TRGM 路由细节）

- 官方 SDK（v1.13.0）：[github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)，
  在线文档 [hpm-sdk.readthedocs.io](https://hpm-sdk.readthedocs.io)
- HPM5E00 数据手册：[HPM5E00FSV01.pdf](https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf)；
  用户手册 Rev0.4 为厂商内部资料（未公开），页码引用以该版本为准

## 15.13 待确认项与风险

> 已逐条复核（2026-11-02）：未闭环条目均写明了原因与验证命令。

| 编号 | 待确认内容 | 风险 | 验证方法 |
|---|---|---|---|
| 15-A | 100 Hz / 10 kHz / 100 kHz 下的抖动与 ISR 占用率 | **已知限制：本轮板卡排队被别人占满，三档没排上**。1 kHz 数据见表 15-6（σ=0.194 us、pp=1.01 us）；频率升高后的 ISR 占用率可以用 `grate` + `gstat` 直接量，命令已就绪 | `bash scripts/hwrun.sh gptmr/gptmr_timer_irq --secs 24 --send "grate 100;gstat;grate 10000;gstat;grate 100000;gstat"`，对比三档 σ/pp 与主循环吞吐 |
| 15-B | 定时器硬件抖动（引脚输出） | **已知限制：需要示波器/逻辑分析仪**。片内替代方案行不通——把比较输出经 TRGM 送引脚再 LOOP_BACK 回捕获，量化仍是 10 ns，比要测的 ns 级抖动还粗 | 示波器测 GPTMR 比较输出引脚（`init_gptmr0_channel2_pin_as_output`：OUT2→PD07、OUT3→PD15），用余辉/直方图测边沿抖动；或与 PTPC 秒脉冲比对 |
| 15-C | 捕获模式下的最小可测脉宽 | **已知限制：需要信号发生器**，本板没有可编程窄脉冲源。理论下限先算清：捕获在 `f_gptmr` 上升沿采样，脉宽 < 2 个周期（10 ns/拍 → 20 ns）必漏 | 信号发生器产生 10 ns~1 us 可调窄脉冲进 PC00（`TRGM_P_00`→CAPT_2），逐步缩窄，记录“开始丢数”的宽度；也可用 PWM 输出经引脚（第 14 章，坑 14-3 已给出 5 ns 漏采的旁证） |
| 15-D | 输入监测（MONITOR）阈值行为 | **已知限制：试验命令跑到了但窗口被前序命令占满，没留下 `dmon` 输出**。寄存器层面已确认单位：`gptmr_channel_monitor_config()` 把 `min/max` 直接写进 CMP0/CMP1（32 位计数器比较值），**单位是 gptmr 时钟周期（10 ns/tick），不是 ns** | `samples/plb/plb_trgm_diag` 的 `dmon <min> <max>`：1 kHz 输入（周期 100000 tick）时对比 `dmon 500000 200000`（按 tick 判据必然越下限）与 `dmon 50000 200000`（不越界）的 CAP 中断次数 |
| 15-E | GPTMR1 是否被 RTOS 占用 | **已知限制（与 RTOS 例程冲突）** | **已复核（2026-11-02，静态核查）**：`grep -rn GPTMR1 boards/ samples/ components/` 只命中 `boards/hpm5e31_1/tool_config.hpmpc` 里的**引脚方案**（`init_gptmr1_channel2/3_pin_as_*`），板级 C 代码与所有例程都没有占用 GPTMR1；被 BSP 占用的是 GPTMR3（`board.h` 的 `BOARD_DELAY_TIMER`）。所以 GPTMR1 可以直接用，只要别和 pinmux 里那几组 GPTMR1 引脚方案冲突 |
| 15-F | GPTMR OUT2/OUT3、PLB 输出经 TRGM 进捕获通道不触发 | **已知限制（现象定位到寄存器层，缺硅片证据）**：2026-11-02 用 `samples/plb/plb_trgm_diag` 复测，GPTMR0 CH2/CH3 的 `CR=0x00000508`（CEN+CMPEN 均在位）、`CNT` 递增、`RLD=99999`、`CMP0=49999`，说明比较输出在片内确实在跑；但 `ddirect`（OUTn 直连 CAPT_2/3）300 ms 捕获 0 次，PLB 软件注入也 0 次。`TRGM_IN/TRGM_OUT` 回读寄存器在本板恒 0（第 17 章 17-G），所以“信号到没到 TRGM”本轮证不出来 | ① 示波器/逻辑分析仪接 PD07（OUT2=TRGM_P7）、PD15（OUT3=TRGM_P15）看引脚是否翻转；② 用已知可用的源（PWM0 TRGO_6→CAPT_2）做对照，确认捕获通道没坏；③ 若引脚翻转而捕获仍 0，需厂商确认 `OUTPUT_SRC_GPTMR0_CAPT_2/3` 的对齐关系 |