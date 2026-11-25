# icu_capture —— AUTOSAR Icu 输入捕获：1/10/100 kHz 周期误差 + 占空比误差

PWM0 P2（PD02）经板载 I2C2↔I2C3 杜邦线送到 PD07，PD07 配成 TRGM_P_07 → TRGM →
GPTMR2 CAPT_3，由 Icu 驱动捕获。测周期误差（ppm）、占空比误差、边沿计数与时间戳缓冲。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1 |
| 必须的板载接线 | PD02↔PD07（I2C2_SCL ↔ I2C3_SCL 杜邦线） |
| 信号源 | PWM0 P2 = PD02（10 kHz / 50% 默认） |
| 捕获链路 | PD07 = IOC_PD07_FUNC_CTL_TRGM_P_07 → TRGM0_P07 → GPTMR2_CAPT_3（Icu ch1） |
| 不占用 | GPTMR0/1（Gpt 用）、GPTMR3（BSP）、PWM0 其它通道、EWDG |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/icu_capture
bash scripts/hwrun.sh mcal/icu_capture --secs 20
```

## 命令

| 命令 | 说明 |
|---|---|
| `icu freq <hz>` | 设置被测信号频率（1..200000 Hz） |
| `icu duty <permille>` | 设置被测信号占空比（千分比） |
| `icu meas` | 打印一次测量（period tick / 频率 / ppm / high tick / 占空比 / 误差） |
| `icu edge` | 边沿检测模式 1 s，打印边沿数 vs 理论值 |
| `icu ts` | 时间戳缓冲 32 点示例（打印索引与前 4 个时间戳） |
| `icu err` | 错误路径（非法通道/非法边沿/空缓冲/0 长度/空指针）|
| `icu info` | 版本/时钟/通道映射/输入状态 |

## 实测现象

日志：`logs/20261102-141450-icu_capture.log`

```text
[sweep] frequency 1/10/100 kHz, duty=50%, f_gptmr2=100000000 Hz
[icu] target=1000 Hz   -> period=100001 tick (999.990 Hz, -10.00 ppm)  high=49917 tick duty=49.917% (-0.083%)
[icu] target=10000 Hz  -> period=10001 tick  (9999.000 Hz, -99.99 ppm) high=4917 tick  duty=49.165% (-0.835%)
[icu] target=100000 Hz -> period=1000 tick   (100000.000 Hz, +0.00 ppm) high=417 tick   duty=41.700% (-8.300%)
[sweep] duty 20/50/80% @1 kHz
[icu] target=1000 Hz duty=6553/0x8000  -> period=100001 tick duty=19.915% (-0.083%)
[icu] target=1000 Hz duty=16384/0x8000 -> period=100000 tick duty=49.917% (-0.083%)
[icu] target=1000 Hz duty=26214/0x8000 -> period=100000 tick duty=79.915% (-0.084%)
[edge] 1 s: edges=1998 expect=2000 (2*1000Hz*1s) delta=-2
[ts] buffer len=32, idx=0, first_delta=100001 tick (1000.010 us), second_delta=100000 tick
[ts] buf[0] = 12232  buf[1] = 112233  buf[2] = 212233  buf[3] = 312234
[det] before=1 after=6 delta=5
[det] last: mod=122 inst=1 api=0x11 err=0x0A
[icu] Icu v1.0.0 moduleID=122 f_gptmr2=100000000 Hz
[icu] last edge timestamp ch1 = 12212293, elapsed=100000 tick, input=ACTIVE
```

要点：
- 周期/频率：1 kHz -10 ppm（1 tick 量化）、10 kHz -100 ppm（1 tick 量化）、100 kHz 0 ppm ——
  全部落在 ±1 tick 量化范围内；
- 占空比：恒定 **-0.083% × (1 kHz) / -0.835% (10 kHz) / -8.3% (100 kHz)**，绝对值都是约
  83 tick = 0.83 µs 的高电平缺失 —— 同一固定偏置在不同周期下的百分比不同（易错点 45-3）；
- 边沿计数 1998/2000，-0.1%；时间戳相邻间隔 100001/100000 tick 与 1 kHz 一致；
- `det before=1` 的那 1 条是运行中线性缓冲填满后 `Icu_StopTimestamp` 按 SWS 报
  ICU_E_NOT_STARTED(0x15)，属预期行为。

回归：IOC 基地址改为 PBcfg 字段后复跑（@@logs/20261102-143359-icu_capture.log@@），
占空比 19.915/49.917/79.915%、边沿 1998/2000、周期 100000~100001 tick，与重构前一致。

## 关键点

1. GPTMR2 的捕获通道 2/3 只能经 TRGM 引入（RM TRGM 输出源 GPTMR2_CAPT_2/3）；
   引脚必须配成 TRGM_P_xx 复用，否则捕获输入恒 0。
2. AUTOSAR Icu 的"测量模式"映射到 GPTMR 工作模式：SIGNAL_MEASUREMENT→measure_width
   （硬件填 CAPPRD/CAPDTY），TIMESTAMP/EDGE_DETECT→capture-at-edge（CAPVAL 时间戳）。
3. Icu_GetDutyCycleValues 返回的是 **tick 数**（ActiveTime/PeriodTime），换算物理量要用
   Icu 的时钟频率，不是被测信号频率。
4. 线性时间戳缓冲填满后驱动按 SWS 停采，再 StopTimestamp 会报 ICU_E_NOT_STARTED —— 这
   是"已定位"的正常路径，不是 bug。

## 待确认项

- 已实测：1/10/100 kHz 周期误差、20/50/80% 占空比误差、边沿计数、32 点时间戳、DET 路径。
- 已知限制（原因 + 复跑命令）：占空比固定 -0.83 µs 高电平缺失（捕获链路/输出级延迟，未逐通道
  标定）；复跑 `bash scripts/hwrun.sh mcal/icu_capture --secs 20`，看 duty 列随频率的变化。