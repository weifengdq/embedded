# gptmr_timer_irq —— GPTMR 周期中断与中断抖动统计

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用 GPTMR0 做两件事，全部在片内完成，**不需要任何外部接线**：

| 通道 | 作用 | 关键配置 |
|---|---|---|
| CH0 | 周期中断源 | `reload = f_gptmr / rate`，默认 1000 Hz |
| CH3 | 32 位自由运行时基 | `reload = 0xFFFFFFFF`，不产生比较事件 |

CH0 每次重载中断里读 CH3 的 `CNT` 寄存器作为时间戳，把相邻两次中断的时间差
换算成 ns，统计周期均值 / 最小 / 最大 / 峰峰抖动 / 标准差 / 频率误差。

**测的是"软件可见的中断到达时刻抖动"**（含 PLIC 响应、ISR 入口开销），
不是定时器硬件输出引脚的抖动——后者要用示波器，见本章坑 15-2。

## 硬件要求 / 接线

无。只需要 USB 转串口（板上 CH343，`/dev/ttyACM0`，921600 8N1）。

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh gptmr/gptmr_timer_irq
bash scripts/hwrun.sh gptmr/gptmr_timer_irq --secs 8 --send "ginfo;gstat"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `gstat` | 打印统计并清零（每次打印后重新开始累计） |
| `grate <hz>` | 修改中断频率（100 Hz ~ 200 kHz 量级） |
| `gclr` | 清零统计 |
| `ginfo` | 打印 GPTMR 时钟与通道配置 |

## 实测现象（串口输出）

```text
=== gptmr_timer_irq (board hpm5e31_1, ext 1.0.0) ===
gptmr0 clock = 100000000 Hz, irq rate = 1000 Hz, reload = 100000
commands: gstat | grate <hz> | gclr | ginfo

letter:/$ ginfo
gptmr0: base=0xf0000000 clk=100000000 Hz
  CH0 rx reload=100000, CH3 free-run
  CMP count per channel = 2, reload/cmp 位宽见第 15 章表 15-2

letter:/$ gstat
[gptmr irq] f_clk=100000000 Hz rate=1000 Hz cnt=683
  period : mean=1000.000 us  min=999.520 us  max=1000.530 us  (expect 1000.000 us)
  jitter : peak-peak=1.010 us  stddev=0.194 us  (=19.4 ticks)
  error  : +0.23 ppm
```

实测日志：`logs/20261030-110408-gptmr_timer_irq.log`

## 实测数据解读

| 指标 | 实测值 | 说明 |
|---|---|---|
| GPTMR0 时钟 | 100 000 000 Hz | `clock_get_frequency(clock_gptmr0)`，AHB 200 MHz 的一半 |
| 1 tick | 10 ns | 抖动统计的最小量化单位 |
| 中断周期均值 | 1000.000 us | 与设定 1 kHz 一致 |
| 周期误差 | +0.23 ppm | 均值 1000.00023 us，属于 tick 量化误差 |
| 峰峰抖动 | 1.010 us = 101 tick | 单次最长/最短间隔之差 |
| 标准差 | 0.194 us = 19.4 tick | 抖动主要来自 CPU 流水线/中断入口可变开销 |

## 关键点 / 坑

1. **CH3 必须单独配置成 0xFFFFFFFF 重载**：如果复用 CH0 的计数器当时间戳，
   每次重载都会回零，算出来的 `dt` 是相位而不是周期。
2. **在 ISR 里不要做 64 位乘除**：本示例只做 4 个标量累加；若在 ISR 里算
   `dt*dt`，ISR 开销本身会进入下一次抖动统计，读数偏大。本示例的 `dt*dt`
   是 32 位乘 32 位送 64 位累加，可接受，但频率上到 200 kHz 时误差会明显。
3. **`gptmr_channel_config(..., enable=false)` 的最后一个参数是"是否立刻使能通道"**，
   传 false 之后还要 `gptmr_start_counter()`，否则计数器不动、中断不来。
4. **时钟必须先加入组**：`init_gptmr0_clock()`（`boards/hpm5e31_1/clock.c`）把
   `clock_gptmr0` 挂到 group 0，否则 `clock_get_frequency` 返回 0，reload 会算成 0。
5. **本板 GPTMR 时钟是 100 MHz 不是 AHB 200 MHz**：`clk` 命令可复核；
   写死 200 MHz 会让实测频率差一倍。
