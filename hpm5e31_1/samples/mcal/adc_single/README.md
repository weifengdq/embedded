# adc_single —— AUTOSAR Adc 单次采样 + 噪声统计（HPM5E31 ADC16）

一句话：用 `Adc_Init / Adc_SetupResultBuffer / Adc_StartGroupConversion / Adc_ReadGroup` 对 ADC0.IN01（PF26）
做 1000/5000 次单次采样，统计 mean / sd / pp / ENOB，并演示 Det 错误路径。

## 硬件要求 / 接线

| 项目 | 要求 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31），ADC16 单端 16 bit |
| 通道 | ADC0 通道 1 = PF26（**ADC0_IN01，16 bit**），VREFH 按 3.3 V 计算 |
| 接线 | 板上无电位器；PF26 经排针引出，**悬空时读数为噪声底 + 板级偏置**（约 1.78 V / 35400 LSB） |
| 冲突 | PF26 同时是 CAN0 红色状态灯：Dio/CAN 例程运行时不要同时点这颗灯，也不要同时烧写两个例程 |
| 串口 | /dev/ttyACM0 @921600 |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/adc_single
bash scripts/hwrun.sh mcal/adc_single --secs 30 --send 'adcstat 1000;adcstat 5000;adc;adcerr;adcver'
```

| 命令 | 作用 |
|---|---|
| `adcinfo` | 模块信息、转换时钟、理论采样率 |
| `adc` | 单次采样一次：raw + uV |
| `adcstat [n]` | n 次单次采样统计：mean / sd / pp / ENOB（默认 1000） |
| `adcver` | `Adc_GetVersionInfo` |
| `adcerr` | 触发 6 条 Dev/运行时错误，打印 Det 增量 |

## 实测现象（串口输出）

```text
Adc_Init done, conv_clk=50000000 Hz, group0 status=0
ADC0.IN01 raw=37007 (1863479 uV) status=0
ADCSTAT: n=1000 mean=35406.77 LSB sd=62.66 LSB min=34787 max=36165 pp=1378 ENOB=8.43 bit
ADCSTAT: mean=1782900 uV pp=69388 uV ; 2.03 us/sample = 491.8 kHz (含 Start+Read)
ADCSTAT: n=5000 mean=35404.32 LSB sd=60.29 LSB min=35232 max=36981 pp=1749 ENOB=8.48 bit
ADCSTAT: mean=1782776 uV pp=88070 uV ; 1.97 us/sample = 506.4 kHz (含 Start+Read)
DET: before=0 after=6 delta=6
Adc_GetVersionInfo: vendor=0xFFFF module=123 sw=1.0.0
```

日志：`logs/20261102-143752-adc_single.log`（最终版，COV_END_CNT 修复后）、
`logs/20261102-142931-adc_single.log`（首版，含 COV_END_CNT 缺失导致的抖动问题，见"关键点"）。

## 关键点

- **COV_END_CNT 必须配**：ADC16 的转换窗口结束计数 = `ADC16_SOC_MAX_CONV_CLK_NUM - 分辨率 + 1`（本板 21-16+1=6）。
  第一版漏了这一步，1000 次采样 sd 高达 13137 LSB、ENOB 只有 0.67 bit；补上后 sd 降到 60~63 LSB。
- **bus mode one-shot**：读 `BUS_RESULT[ch]` 即触发一次转换并阻塞到 VALID（BUF_CFG0.WAIT_DIS=0、BUS_MODE_EN=1）。
  因此 `Adc_StartGroupConversion` 内部完成第一轮转换（约 0.7 us），`Adc_ReadGroup` 取回结果。
- **校准不可省**：Adc_Init 里做了 SDK 同款的 4 轮 32 次平均校准（ADC16_PARAMS），直接决定 16 bit 线性度。
- PF26 悬空时读数不是 0，而是约 35400 LSB（≈1.78 V），这是板级偏置 + 输入噪声底，不是驱动错误；
  要验证线性度需要外接 0~3.3 V 基准源（本轮未做，见待确认项）。
- 单次采样吞吐 ~490~506 kHz（含 Start+Read 的软件开销），理论 fs 1.389 MHz，差距来自 API 调用与轮询开销。

## 待确认项

- 已实测：1000/5000 次统计、单次读数、6 条 Det 错误、版本信息。
- 已知限制：PF26 悬空，VREFH=3.3 V 按板级假设，**未做外部基准线性度标定**；复跑命令同上 `adcstat 5000`。
