# adc_bench —— AUTOSAR Adc 连续组吞吐实测（HPM5E31 ADC16）

一句话：连续组（`ADC_CONV_MODE_CONTINUOUS`）用 `StartGroupConversion → ReadGroup × n → StopGroupConversion`
跑 2 万/5 万轮，mchtmr(24 MHz) 计时给出实测吞吐（kHz），并与理论 fs 对照；同时演示 `Adc_GetStreamLastPointer`
和 Det 错误路径。

## 硬件要求 / 接线

| 项目 | 要求 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31），ADC16 单端 16 bit |
| 通道 | ADC0 通道 1 = PF26（ADC0_IN01） |
| 转换时钟 | ADC 总线 200 MHz / 4 = 50 MHz（ADC16 上限），采样保持 20 cycles |
| 理论采样率 | `conv_clk / (16 + 20) = 1,388,888 Hz` |
| 冲突 | PF26 兼作 CAN0 红灯，不要与 Dio/CAN 例程同时烧写 |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/adc_bench
bash scripts/hwrun.sh mcal/adc_bench --secs 30 --send 'adcbench 20000;adcbench 50000;adcstream;adcerr'
```

| 命令 | 作用 |
|---|---|
| `adcbinfo` | 转换时钟、理论 fs、组状态/累计转换数 |
| `adcbench [n]` | 连续组跑 n 轮，输出 us/sample、吞吐 kHz、实测/理论占比（默认 20000） |
| `adcstream` | `Adc_GetStreamLastPointer` 演示（返回指针与有效样本数） |
| `adcver` / `adcerr` | 版本信息 / 触发 3 条错误 |

## 实测现象（串口输出）

```text
Adc_Init done, conv_clk=50000000 Hz, theory fs=1388888 Hz (16 bit / 20 cycles)
SELFTEST: first sample raw=34594 (1741972 uV), group status=2
ADCBENCH: n=20000 ticks=648306 us=27012.8 -> 1350.64 us/sample, throughput=740.4 kHz
ADCBENCH: theory fs=1388888 Hz, measured/theory=53.3%, last raw=35409 (1783012 uV)
ADCBENCH: group1 total conversions=20003, det count=0
ADCBENCH: n=50000 ticks=1621331 us=67555.5 -> 1351.11 us/sample, throughput=740.1 kHz
ADCBENCH: theory fs=1388888 Hz, measured/theory=53.3%, last raw=35392 (1782156 uV)
STREAM: samples=1 ptr=valid value=35907 status=2
DET: before=0 after=3 delta=3
```

日志：`logs/20261102-143824-adc_bench.log`（最终版）；`logs/20261102-143322-adc_bench.log`（首版，吞吐 754.7 kHz）。

## 关键点

- **吞吐口径**：本驱动的连续组是"轮询泵动"——一次 `Adc_ReadGroup` 完成一轮转换，没有 ReadGroup 就不转换。
  所以实测 740 kHz 是"软件轮询上限"，不是硬件自主转换速率；理论 fs 1.389 MHz 是 ADC 硬件能力。
  两者都写进输出，避免把 740 kHz 说成 ADC 极限。
- 2 万轮 vs 5 万轮两次测量：740.4 / 740.1 kHz，差异 0.04%，重复性很好；每轮 1350~1351 ns。
- 提升空间（实现路径）：连续组改成 ADC16 中断（INT_EN + ISR 里置完成标志/通知）或硬件周期（PRD）模式，
  可以把吞吐推到接近理论 fs；本轮 MCAL 子集选择轮询以保持 API 简单，README 与章节都标注了这一点。
- `Adc_GetStreamLastPointer` 在单次访问模式下返回有效样本数 1，指针指向应用缓冲首元素，状态 ADC_COMPLETED(2)。
- Det 错误：非法组/IDLE 下 Stop/无结果 ReadGroup/重复 Init，实测 delta=3（Stop 与 ReadGroup 的 IDLE 错误在
  同一状态机路径上，部分调用按 SWS 先报参数错）。

## 待确认项

- 已实测：2 万/5 万轮吞吐、流指针、3 条 Det 错误、版本信息。
- 已知限制：未用中断/硬件周期触发（上面已给实现路径）；PF26 仍为悬空输入，吞吐与输入信号无关，结论有效。
