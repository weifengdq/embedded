# adc_basic_poll —— ADC16 单通道轮询、噪声统计与吞吐实测

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 用 ADC16 的 oneshot（单次）模式轮询采样 ADC0 通道 1，打印原始码值并换算电压；
- 统计 N 次采样的均值/标准差/峰峰值，并估算有效位数 ENOB；
- 用 mcycle 统计"一次读 = 一次转换"的轮询吞吐（kHz），作为无 DMA/无中断时 ADC 采样率上限参考；
- 注册 letter-shell 命令，可交互式重复测量。

## 硬件要求 / 接线

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31IPB1） |
| ADC 实例 | ADC0，**通道 1** |
| 引脚 | **PF26**（ADC0_IN01 / ADC1_IN01，DS Rev0.4 p.53 引脚表；board_init_adc16_pins() 把 PF26 设为 ANALOG） |
| 外部接线 | **不需要**。板上无电位器，PF26 悬空时读到的是"悬空电平 + 噪声"；若要测直流精度/线性度，请从外部把 0~VREFH 的分压接到 PF26 |
| VREFH | 代码按 **3.3 V** 换算（APP_ADC_VREF_MV），该值**待确认**，见"待确认项" |

## 编译与运行

```bash
bash scripts/build.sh adc/adc_basic_poll
bash scripts/hwrun.sh adc/adc_basic_poll --secs 8 --send "`adcsr;adc;adcstat 5000`"
```

## 命令

| 命令 | 说明 |
|---|---|
| adcsr | 打印 ADC16 时钟链：输入 200 MHz(AHB) → 分频 4 → conv clk 50 MHz、分辨率、采样周期、理论 fs |
| adc | 读一次，打印 raw / 电压 |
| adcstat [n] | 连续读 n 次（默认 256），打印 mean/sd/min/max/pp、ENOB 估算、轮询吞吐 |
| help | letter-shell 内置 |

## 实测现象（串口输出）

日志：`logs/20261030-110218-adc_basic_poll.log`

```text
=== adc_basic_poll: ADC16 oneshot polling ===
channel: ADC0.IN1 (PF26)
ADC16 ADC0 single-ended oneshot
  input clk : 200000000 Hz (ahb bus clock)
  clk div   : 4 -> conv clk 50000000 Hz (max 50000000)
  res       : 16 bit, sample_cycle = 20
  fs(theory): 1219512 Hz (conv 21 cycles + sample 20 cycles)
  VREFH     : 3300 mV (assumed, to be confirmed)

letter:/$ adc
ADC0.IN01 raw=0x876c (34668) -> 1745699 uV

letter:/$ adcstat 5000
adcstat: n=5000 mean=35545.30 sd=53.35 min=35406 max=36824 pp=1418
         mean=1789875 uV  pp=71403 uV  ENOB~8.63 bit
         poll rate=605.5 kHz (1.65 us/sample, cpu 480000000 Hz)

letter:/$ adcstat 20000
adcstat: n=20000 mean=35545.18 sd=48.63 min=35391 max=37208 pp=1817
         mean=1789869 uV  pp=91494 uV  ENOB~8.72 bit
         poll rate=607.9 kHz (1.65 us/sample, cpu 480000000 Hz)
```

要点：

- ADC 转换时钟 50 MHz（200 MHz AHB / 4，adc16_calc_clock_divider() 自动算），16 bit 转换 21 个 ADC 时钟 + 20 个采样保持时钟 → 理论 1.22 MSPS；
- 轮询吞吐 **~607 kHz**：远低于理论 fs，瓶颈是"CPU 读结果寄存器 → 再触发下一次转换"的软件开销（1.65 µs/次），要跑到 MSPS 级必须用序列/抢占 + DMA；
- 悬空输入的 sd≈49~53 LSB，按 FSR/噪声估算 ENOB≈8.6~8.7 bit —— **这是悬空引脚噪声，不是 ADC 本身精度**，实测精度必须外接低噪声源。

## 关键点 / 坑

1. **oneshot 必须打开 bus mode**：HPM5E31 的 ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT = 1（hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_feature.h:131），初始化后要调用 adc16_enable_oneshot_mode()，否则 BUS_RESULT.VALID 不置位、adc16_get_oneshot_result() 一直返回失败。
2. **默认是非阻塞读**：adc16_get_default_config() 把 wait_dis 设为 true（hpm_sdk/drivers/src/hpm_adc16_drv.c:34），此时读结果不等待转换完成，连续读会时好时坏；要"一次读=一次转换"必须 adc16_set_blocking_read()。
3. **HPM5E31 没有 ADC 内部温度通道**：ADC16_SOC_TEMP_CH_EN 未定义，SDK 的 samples/drivers/adc/adc16_temperature 无法直接移植；温度请用独立 TSNS 外设（samples/power/tsns_temp/）。
4. **HPM5E31 没有 ADC_LOOP（硬件过采样）**：HPM_IP_FEATURE_ADC16_HAS_ADC_LOOP 未定义，过采样只能用软件多次采样累加（见第 18 章"精度与过采样"一节）。
5. 不要用 -lm：本仓库例程没有链接 libm，sqrt/log 会链接失败；本示例用 FPU 纯算术实现 app_sqrtf()/app_log2f()。

## 待确认项

| 项 | 状态 | 验证方法 |
|---|---|---|
| 板载 VREFH 实际电压 | 待确认（需实测） | 用万用表量 VREFH/VDDA_ADC 引脚，或外接基准源后比对 adc 读数 |
| PF26 在板上是否被其它器件拉偏 | 待确认（需实测） | 断电测 PF26 对地阻抗；或外接已知电压后比对 |
| 精度（INL/DNL/ENOB） | 待确认（需实测） | 外接 0~VREFH 精密分压（或 16bit DAC），扫点做线性拟合 |

## 软件过采样实测（adcavg）

HPM5E31 没有硬件 ADC_LOOP，只能用软件平均。命令 `adcavg [groups]` 每组采 m 次求平均，
统计 256 组平均值的 sd 与 ENOB（PF26 悬空，日志 `logs/20261030-115554-adc_basic_poll.log`）：

```text
letter:/$ adcavg 256
#m,gaps,mean_lsb,sd_lsb,pp_lsb,enob,poll_khz
1,256,35537.59,80.33,1482,7.92,476.5
2,256,35501.70,84.57,1275,7.87,657.5
4,256,35479.89,44.78,597,8.81,746.2
16,256,35465.09,13.88,160,10.58,832.7
64,256,35461.10,5.88,49,11.76,857.3
```

- m≥4 后 sd 约按 1/√m 下降，ENOB 从 7.9 bit 提升到 11.8 bit（m=64）；
- 但有效采样率降为 1/m，要高速+高分辨率必须上 DMA 批量采样后再平均；
- m=1/2 的 sd 波动大，是因为悬空输入基线不稳，不代表 ADC 真实噪声。
