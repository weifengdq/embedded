# gpt_periodic —— AUTOSAR Gpt 1 ms 周期 tick：10 s 计数误差 + ISR 抖动

用 Gpt 驱动的连续模式通道（GPTMR0 CH0）产生 1 ms 周期通知，实测 10 s 计数误差、
中断抖动与 ISR 入口延迟；同时用 mcycle 标定 GPTMR 实际时钟，并把 Gpt Predef Timer
（GPTMR0 CH3 自由运行）的 1 µs / 100 µs 时基一起验证。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31IPB1） |
| 外部接线 | 无 |
| 串口 | USR 串口 CH343 `/dev/ttyACM0` @921600 8N1 |
| 使用资源 | GPTMR0 CH0（逻辑通道 0，1 ms tick）、GPTMR0 CH3（Predef Timer 自由运行时基）|
| 不占用 | GPTMR3 归 BSP `board_delay_us`；GPTMR1/GPTMR2/PWM/EWDG 本示例不碰 |

## 编译与运行

```bash
cd hpm5e31_1 && source scripts/env.sh
bash scripts/build.sh samples/mcal/gpt_periodic
bash scripts/hwrun.sh mcal/gpt_periodic --secs 22
```

开机自动跑 13 s 测试套件（10 s 计数窗口 + 时钟标定 + API 开销 + Predef 一致性 + DET 错误路径），
套件执行期间不轮询 shell，避免命令改定时器状态污染统计。

## 命令

| 命令 | 说明 |
|---|---|
| `gpt_info` | 打印 Gpt 版本/通道/时钟/周期/当前 elapsed/remaining |
| `gpt_bench [sec]` | 跑 sec 秒（默认 10）计数误差窗口，打印 ticks vs expect 与 ppm |
| `gpt_stat` | 打印 ISR 抖动（min/max/pp/stddev）与 ISR 入口延迟 |
| `gpt_api` | 测 Gpt_GetTimeElapsed / GetTimeRemaining / StartTimer 调用开销（cycles/ns） |
| `gpt_predef` | 读 4 个 Predef Timer 并核对 1µs 与 100µs 时基比例 |
| `gpt_stop` / `gpt_start` | 停表（读回 latched elapsed） / 重新启表 |
| `gpt_err` | 错误路径：非法通道 / 0 值 / 空指针 → Det 计数 +3 |

## 实测现象

日志：`logs/20261102-134707-gpt_periodic.log`

```text
[bench] window=10000 ms ticks=9999 expect=10000 error=-100.000 ppm
[bench] 1 ms tick actual = 1000.100010 us, freq = 999.900 Hz
[isr] n=10000 period=1000.000 us (expect 1000.000 us)
[isr] jitter: min=999.746 max=1000.256 pp=0.510 us stddev=0.095 us (46 cycles)
[isr] entry latency: min=68 max=379 ticks (1 tick = 10.0 ns) mean=753.2 ns
[cal] window: dmcycle=480000438 = 1.000001 s @ f_cpu=480000000 Hz, predef d=1000005 us
[cal] GPTMR0 actual = 100000409 Hz, clock_get_frequency = 100000000 Hz, deviation = +4.1 ppm
[api] Gpt_GetTimeElapsed: mean=142.2 cycles (296.3 ns) min=137 max=2364
[api] Gpt_GetTimeRemaining: mean=159.3 cycles (331.9 ns) min=153 max=3294
[api] Gpt_StartTimer: mean=1427.1 cycles (2973.1 ns) min=1392 max=3666
[det] before=0 after=3 delta=3
[det] last: mod=100 inst=0 api=0x00 err=0x16
```

要点：
- 10 s 计数 -100 ppm 是**整数计数 1 个 tick 的量化极限**（1/10000），不是频率误差；
- GPTMR0 实测输入 100.000409 MHz（+4.1 ppm），比 clock_get_frequency() 的标称 100 MHz 更准；
- ISR 抖动 pp 0.51 µs / stddev 0.095 µs；ISR 入口延迟（硬件到期 → 进入 ISR）均值 753 ns；
- Gpt_GetTimeElapsed 296 ns / StartTimer 2.97 µs（含 GPTMR 重配置与 NVIC 使能）。

## 关键点

1. AUTOSAR 的 `Gpt_ValueType` 是**定时器 tick 数**（这里 = GPTMR 100 MHz tick，10 ns），
   不是微秒；1 ms 对应 reload=100000。换时钟必须重算。
2. one-shot 通道即使没使能 notification 也必须开 RLD 中断，否则状态机永远停在 running
   （见第 44 章易错点 45-2）。
3. Predef Timer 用 GPTMR0 CH3 自由运行 + 软件整数除法换算 1µs/100µs，不额外占硬件定时器；
   GPTMR3 被 BSP 延时占用，本模块不碰。
4. 测量窗口内不要轮询 shell：任何 Stop/Start 命令都会让 tick 计数归零。

## 待确认项

- 已知限制：10 s 窗口的量化极限是 ±100 ppm，想看更小误差需把窗口拉到 100 s 或用
  GPTMR 另一通道做硬件比较；复跑命令 `bash scripts/hwrun.sh mcal/gpt_periodic --secs 22`。
- 已实测：GPTMR0 时钟 +4.1 ppm、ISR 抖动、API 开销（见上表）。
