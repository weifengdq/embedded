# gptmr_capture_measure —— GPTMR 输入捕获测频率/占空比/抖动

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用 GPTMR0 一个通道产生方波、另一个通道**输入捕获**测量它，形成片内闭环，
**不需要飞线也不需要示波器**：

```text
GPTMR0 CH2 输出比较(默认 1 kHz / 25%, 高 250 us 低 750 us)
  -> TRGM: GPTMR0_OUT2 -> GPTMR0_CAPT_3
  -> GPTMR0 CH3 输入捕获(measure_width 模式, 硬件自动填 CAPPRD/CAPDTY)
  -> 中断里统计周期/高电平 -> 主循环打印频率、占空比、抖动、误差
```

CH3 用 `gptmr_work_mode_measure_width`，硬件直接给出 `CAPPRD`（周期）与
`CAPDTY`（高电平时间），CPU 只做累加，因此测的是**硬件捕获结果**，不受主循环节奏影响。

## 硬件要求 / 接线

- 串口：`/dev/ttyACM0` 921600 8N1。
- 默认片内自环，无飞线。
- 想测外部信号：接到 **PC00**（复用为 `TRGM_P_00`），执行 `csrc 1`，
  例程会把 PC00 → TRGM → CAP3 接通（`board_timer_init_trgm_gptmr_p00_to_cap()`）。

## 编译与运行

```bash
bash scripts/build.sh gptmr/gptmr_capture_measure
bash scripts/hwrun.sh gptmr/gptmr_capture_measure --secs 10 --lock-wait 900 \
  --send "cstat;crate 2000 500;cstat;crate 5000 200;cstat"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `cstat` | 打印实测频率/占空比/周期抖动/高电平抖动与误差 |
| `crate <hz> <permille>` | 改被测方波频率与占空比（如 `crate 2000 500`） |
| `csrc <0\|1>` | 捕获源：0=片内 GPTMR_OUT2（默认），1=外部 PC00 |

## 实测现象（串口输出）

```text
=== gptmr_capture_measure (board hpm5e31_1, ext 1.0.0) ===
src: GPTMR0 CH2 output-compare 1000 Hz / 250 permille -> TRGM -> CH3 capture(measure_width)
gptmr clk = 100000000 Hz (1 tick = 10.00 ns)
commands: cstat | crate <hz> <permille> | csrc <0|1>

letter:/$ cstat
[cap] n=... period=1000.xxx us (1000.x Hz) high=250.xxx us duty=25.0x%
      jitter: period pp=... 
      target: 1000 Hz / 250 permille -> freq err ... ppm, duty err ...%  src=int GPTMR_OUT2
```

实测日志：`20261030-122956-gptmr_capture_measure.log`

**实测结论（重要限制）**：
- ✅ 捕获通道配置成功（`measure_width` 模式、CAPPRD/CAPDTY 读取路径、1 tick=10 ns 回读）；
- ❌ **片内自环没有闭环**：GPTMR CH3 的比较输出经 TRGM 送 CH2 捕获，`cstat` 始终打印
  "还没有捕获事件"（cap=0）。同一现象也出现在 PWM TRGO7→CAPT_3 与 PLB→CAPT_2 上，
  而 PWM TRGO6→CAPT_2 是能送达的——已作为**易错点 15-7 / 待确认 15-F、17-F** 记录；
- 因此本例程的**主用法是外部输入**：把信号接到 PC00 后执行 `csrc 1`，
  即可用 `measure_width` 硬件测周期/占空比。

## 关键点 / 易错点

1. **被测源别用 PWM 的 TRGO**：TRGO 是 1 个 PWM 时钟（200 MHz 下 5 ns）窄脉冲，
   跨到 100 MHz 的 GPTMR 会大量漏采（第 14 章易错点 14-3 实测 50 kHz 只捕到 1/30）。
   GPTMR 自己的输出比较是 500 us 宽脉冲，100% 可靠。
2. `measure_width` 模式要**两个边沿都捕获**才能算周期/高电平，配置时不要再限制成单边沿。
3. `CAPPRD`/`CAPDTY` 是**上一次测量的结果**，中断里读才对应本次边沿，
   主循环直接读会拿到旧值（本示例在 ISR 里读）。
4. 捕获通道 2/3 才能从 TRGM 取信号；通道 0/1 的捕获输入只来自 IOC 引脚。
5. 分辨率 = 1 个 GPTMR 时钟（本板 10 ns），1000 us 周期对应 100 个计数单位，
   量化误差约 10 ppm 量级——所以 `freq err` 主要是测量量化，不是信号源误差。