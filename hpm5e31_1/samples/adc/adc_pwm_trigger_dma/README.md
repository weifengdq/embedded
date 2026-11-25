# adc_pwm_trigger_dma —— PWM0 触发 ADC16 序列转换 + DMA 连续采样

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- PWM0 的 TRGO（内部触发，不占引脚）经 TRGM 路由到 ADC0 序列触发（ADC0_STRGI），以固定 20 kHz 触发 ADC0.IN01 采样；
- 转换结果由 **ADC16 序列 DMA** 连续写入 non-cacheable 缓冲（1024 个 32 位采样字，含 cycle/seq/ch 字段）；
- 中断里用 mchtmr 记录相邻触发间隔，统计**实际采样率与抖动**；
- 命令：`adcinfo` / `adcdma [ms]` / `adcstop`。

## 硬件要求 / 接线

无需外部接线（PWM 触发在片内走 TRGM，不需要引脚）。ADC 输入仍是 PF26（ADC0.IN01）。

## 编译与运行

```bash
bash scripts/build.sh adc/adc_pwm_trigger_dma
bash scripts/hwrun.sh adc/adc_pwm_trigger_dma --secs 12 --send "adcinfo;adcdma 1000;adcdma 2000"
```

## 命令

| 命令 | 说明 |
|---|---|
| adcinfo | 打印 ADC 时钟/触发频率/DMA 缓冲/当前状态 |
| adcdma [ms] | 开 PWM 触发运行 N 毫秒，统计 SPS、间隔抖动、缓冲区 mean/sd |
| adcstop | 停止 PWM 触发（测完恢复默认，不留输出） |

## 实测现象（串口输出）

日志：`logs/20261030-120354-adc_pwm_trigger_dma.log`

```text
ADC16 ADC0 sequence + PWM trigger + DMA
  conv clk  : 50000000 Hz, sample_cycle=20, 1 channel (IN1)
  trigger   : PWM0 TRGO_0 -> TRGM -> ADC0_STRGI @ 20000 Hz
  dma buffer: 1024 words @0x01230000 (non-cacheable)

letter:/$ adcdma 1000
adcdma: 1000 ms, irq=20000 -> 20000.0 SPS (expected 20000)
  interval: mean=50.00 us min=49.50 max=50.50 us (jitter pp=1.00 us)
  buffer[0..255]: mean=36676.6 sd=34.1 min=36578 max=36755
  raw[0..3]: 0x02108f39 0x06108f21 0x0a108f10 0x0e108efe

letter:/$ adcdma 2000
adcdma: 2000 ms, irq=40001 -> 20000.5 SPS (expected 20000)
  interval: mean=50.00 us min=45.54 max=50.00 us (jitter pp=4.46 us)
  buffer[0..255]: mean=36660.8 sd=35.5 min=36552 max=36753
```

| 指标 | 1 s 测试 | 2 s 测试 |
|---|---|---|
| 触发频率 | 20 kHz | 20 kHz |
| 实际转换数 | 20000 | 40001 |
| 实际采样率 | **20000.0 SPS**（误差 0%） | 20000.5 SPS（+0.0025%） |
| 间隔抖动 pp | **1.00 µs** | 4.46 µs（一次 45.54 µs 的异常） |
| 缓冲区 sd | 34.1 LSB | 35.5 LSB |

对比：软件轮询同一通道 sd≈53 LSB、吞吐 607 kHz 且抖动大；**PWM 触发 + DMA 的间隔抖动只有 1 µs、
采样率精确 20 kSPS**，适合电流环等"时间对齐要求高"的场景。

## 关键点 / 易错点

1. 触发链路要"先 TRGM、后使能 PWM"，否则先到来的触发会丢（本示例最后才 pwm_trigger_start()）。
2. PWM 触发源用 PWM0 的计数器 0 + 比较点 16（pwmv2_set_trigout_cmp_index(pwm, 0, 16)），
   TRGM 输入选 HPM_TRGM0_INPUT_SRC_PWM0_TRGO_0，输出选 TRGM_TRGOCFG_ADC0_STRGI。
3. 序列模式要 hw_trig_en = true、cont_en = true、队列最后一项 seq_int_en = true，否则不会产生 SEQ_CVC 中断。
4. DMA 缓冲必须 non-cacheable（ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(ADC_SOC_DMA_ADDR_ALIGNMENT)），否则 CPU 读到旧数据。
5. 测完调 adcstop 停 PWM，避免一直在跑（全项目约定：测试要短、恢复默认）。
6. 2 s 测试里出现过一次 45.54 µs 的间隔（jitter pp 4.46 µs），怀疑是 shell 轮询/串口中断抢占，
   长期抖动需专项验证（见"待确认项"）。
