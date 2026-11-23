# 第 17 章 SDM / PLB / TRGM 可编程逻辑与互联

## 17.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 |
| 对应例程 | `samples/plb/plb_lut_passthrough/`（实测）、`samples/pwm/pwm_basic/`（TRGM 路由实验）、`samples/sdm/sdm_probe/`（骨架） |
| 实测日期 | 2026-10-30 |
| 可信度 | 中高：TRGM/PLB 为上板实测；**SDM 无外部 ΣΔ 调制器，仅做初始化与寄存器自检**（见 §17.12） |

## 17.2 本章导览

- **学到什么**：TRGM 这张"片上路由表"怎么用、PLB 的 LUT+FF 与数据单元能做什么、
  SDM 到底是"发位流"还是"收位流"——**本章会纠正一个常见误解**：
  HPM 的 SDM 是 ΣΔ **接收**单元（MDAT 是输入），**不能**用来驱动 LED 做亮度。
- **前置章节**：第 14 章（PWMv2，TRGM 的主要信号源）、第 15 章（GPTMR，本章的测量工具）。
- **读者收益**：读完能自己查表判断"某个信号能不能内部路由"，不用飞线就能做交叉验证。

## 17.3 概述与典型应用

这三个模块经常一起出现，但角色完全不同：

| 模块 | 角色 | 一句话 |
|---|---|---|
| TRGM | **路由/交换矩阵** | 把任意输入源接到任意输出目的，还能做取反、边沿脉冲、数字滤波 |
| PLB | **可编程逻辑** | 8 个 TYPE A（LUT4 + 触发器）+ 8 个 TYPE B（数据单元/计数器 + 比较器） |
| SDM | **ΣΔ 解调接收** | 收外部 ΣΔ 调制器的 1 bit 位流，用 Sinc 滤波还原成电流/位置数值 |

表 17-1：TRGM / PLB / SDM 的角色对比

典型应用：

| 应用 | 用到的模块 | 说明 |
|---|---|---|
| PWM 触发 ADC 采样 | TRGM | 比较事件 → ADC 序列/抢占触发（第 18 章） |
| PWM 死区/故障信号路由 | TRGM | 外部故障 → PWM 触发输入 |
| 编码器信号整形/分频 | PLB + TRGM | LUT 做逻辑、TYPE B 做分频 |
| 自定义保护逻辑 | PLB + TRGM | 两路故障相与后送 PWM 故障输入 |
| 电流环隔离采样 | SDM | AMC1306/AD7401 等 ΣΔ 调制器 + Sinc 滤波 |
| 无示波器的交叉测量 | TRGM + GPTMR | 本章 §17.8.2 的实测 |

表 17-2：三模块的典型组合用法

## 17.4 工作原理：TRGM 片上路由矩阵

![图 17-4 TRGM 路由模型](figures/17/fig-17-4-trgm-route.png)

### 17.4.1 结构

TRGM（Trigger Multiplexer，UM §38, p.606）内部是一张**输入源 → 输出目的**的交叉开关：

- **输入源**：约 150 项，包括 PWM0/PWM1 的触发输出、GPTMR 的 OUT2/OUT3、
  QEI 触发/位置有效、SDM 比较器越限、PLB 输出、ACMP、以及 `TRGM_P00~P31` 引脚。
- **输出目的**：PWM 触发输入 0~7、GPTMR 捕获输入（**仅通道 2/3**）、
  ADC 序列/抢占触发、SDM 同步事件、QEI 触发、PLB 输入、`TRGM_Pnn` 引脚。
- **每个输出可配**：取反、同相、上升沿脉冲、下降沿脉冲（`trgm_output_t.type`）。

### 17.4.2 本板可用的关键路由（已核对头文件）

| 源 | 目的 | 宏 | 本章用途 |
|---|---|---|---|
| `PWM0_TRGO_6/7` | `GPTMR0_CAPT_2/3` | `HPM_TRGM0_INPUT_SRC_PWM0_TRGO_n` → `HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_2/3` | `pwm_basic` 的硬件捕获实验 |
| `GPTMR0_OUT2` | `PLB_IN_00` | `HPM_TRGM0_INPUT_SRC_GPTMR0_OUT2` → `TRGM_TRGOCFG_PLB_IN_00` | `plb_lut_passthrough` |
| `PLB_OUT00` | `GPTMR0_CAPT_3` | `HPM_TRGM0_INPUT_SRC_PLB_OUT00` → `HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_3` | 同上 |
| `PWM0_TRGO_0` | `ADC0_STRGI` | `TRGM_TRGOCFG_ADC0_STRGI` | 第 18 章 ADC 触发 |
| `TRGM_P00` | `GPTMR0_CAPT_2` | 引脚 → 捕获 | 官方 `gptmr/input_capture` |

表 17-3：本板已验证/可用的 TRGM 路由（`hpm_trgmmux_src.h`）

### 17.4.3 数字滤波器

TRGM 有 `FILTCFG`（UM §38.2.3, p.607），可对输入做数字滤波后才送往输出，
适合把带毛刺的故障信号"去抖"后再送 PWM 故障输入。滤波器长度按
`长度 × (1/TRGM 时钟)` 算，超过这个宽度的脉冲才算有效。

### 17.4.4 位置矩阵 / ADC 矩阵 / DAC 矩阵

除了普通触发通路，TRGM 还有三张专用矩阵（`TRGM_SOC_HAS_*_MATRIX_SEL`）：

- `POS_MATRIX_SEL0`：QEI 位置 → QEO（第 16 章的 QEO 输入来源）；
- `ADC_MATRIX_SEL0`：ADC 结果 → QEI 正余弦输入；
- `DAC_MATRIX_SEL0/1/2`：DAC 值 → PWM 的 DAC 输入。

## 17.5 PLB：可编程逻辑块

![图 17-3 PLB TYPE A/B 结构](figures/17/fig-17-3-plb-block.png)

PLB（Programmable Logic Block，UM §42, p.707）有两种可编程单元：

### 17.5.1 TYPE A（组合 + 时序）

- **8 个通道**，每通道 4 路 `trig_in`、4 路 `trig_out`（UM §42.3.1, p.708）；
- 每路输出对应一个 **4 输入查找表**：4 bit 输入做索引 → 16 bit 真值表
  （所以 `plb_type_a_truth_t.val` 是 16 位，例如直通 `out=in0` 就是 `0xAAAA`）；
- 查找表输出经过**寄存器**送到 `trig_out`，所以输出天然与系统时钟同步；
- `SW_INJECT` 可让软件直接注入一个系统时钟周期的值——本章例程用它验证输出通路。

### 17.5.2 TYPE B（数据单元 + 比较）

- **8 个通道**，每通道一个 **64 bit 查找表**（= 16 个 4 bit slice）驱动"数据单元"；
- 数据单元每周期根据 slice 的操作码动作（UM §42.3.2, p.709）：

| 操作码 | 含义 |
|---|---|
| 0 | 保持上周期数值 |
| 1 / 2 / 3 | 载入 `CMP[0]` / `CMP[1]` / `CMP[2]` |
| 4 / 5 | 当前值 +1 / +2（或左移） |
| 6 / 7 | 当前值 -1 / -2（或右移） |

表 17-4：PLB TYPE B 数据单元操作码（UM §42.3.2, p.709）

- 4 个比较单元把数据单元与 `CMP` 比较，输出送到 `trig_out`，
  比较模式有 >、<、=、>=、<= 以及按位与/或/异或（同页）。

**因此 PLB 能做的事**：分频/计数（TYPE B）、自定义逻辑门（TYPE A）、
把多路故障"与"起来再送 PWM 故障输入、把编码器 A/B 直接做 4 倍频解码。

## 17.6 SDM：ΣΔ 信号接收单元（不是输出！）

![图 17-1 SDM 结构](figures/17/fig-17-1-sdm-block.png)

### 17.6.1 先纠正一个常见误解

网上和项目初期的说法是"用 SDM 输出位流驱动 LED 做亮度"。**这在 HPM5E00 上做不到**：

- UM §43.2（p.723）明确写：**4 根 sigma-delta 数据管脚 MDATx 是"数据输入"**，
  MCLKx 是"时钟"管脚；
- `hpm_sdm_drv.h` 的全部 API 都是"接收 + 滤波 + 比较器 + FIFO"，
  没有任何"发送位流"的接口；
- 引脚复用里 `IOC_PF16_FUNC_CTL_SDM0_DAT_0` 的 DAT 是**输入**方向。

真正能"用位流驱动 LED"的做法是用 PWM 做高位深调光，或用 PLB TYPE B 做 ΣΔ 调制器
（软件/PLB 产生 1 bit 流），与 SDM 模块无关。

### 17.6.2 SDM 的实际能力

| 项 | 规格（UM §43.2, p.723-724） |
|---|---|
| 通道数 | 4（4 组 MCLK/MDAT） |
| 采样模式 | 上升沿 / 双沿 / 下降沿 / 每 2 个上升沿 / 每 2 个下降沿 / Manchester |
| 数据滤波器 | Sinc1 / Sinc2 / Sinc3 / SincFast，**OSR 1~256** |
| 幅值滤波器 | 同上，**OSR 1~32** |
| 输出位宽 | 16 或 32 bit |
| FIFO | 16 × 32 bit，可配门限 |
| 比较器 | 高门限/低门限/过零，可产生中断或经 TRGM 输出 |
| 同步 | 16 根 `PWM_EVT`（来自 TRGM）可做 SDSYNC 同步采样 |
| 时间戳 | 可随数据输出采样时刻 |

表 17-5：SDM 关键能力

### 17.6.3 数据速率

UM §43.3.2.1（p.726）：`输出数据速率 = ΣΔ 调制器的采样率 / OSR`。
例：调制器时钟 20 MHz、OSR = 256 → 数据率 78.125 kSPS；OSR = 32 → 625 kSPS。
**OSR 越大精度越高、延迟越大**，这就是 Sinc 滤波器的经典折中。

![图 17-2 ΣΔ 位流与 Sinc 滤波原理](figures/17/fig-17-2-sigma-delta.png)

## 17.7 关键寄存器与驱动 API 对照

| 功能 | 寄存器 | 驱动 API | 例程 |
|---|---|---|---|
| TRGM 输出路由 | `TRGM_OUT[n]` | `trgm_output_config()` | `plb_lut_passthrough` |
| TRGM 输出电平 | `TRGM_OUT[n].SW` | `trgm_output_update_source()` | `pwm_deadtime_fault` |
| TRGM 滤波 | `FILTCFG` | `trgm_filter_config()` | — |
| 位置矩阵 | `POS_MATRIX_SEL0` | `trgm_pos_matrix_config()` | 第 16 章 QEO |
| ADC 矩阵 | `ADC_MATRIX_SEL0` | `trgm_adc_matrix_config()` | 第 16 章 sincos |
| PLB TYPE A LUT | `TYPE_A[n][LOOKUP_TABLE][m]` | `plb_type_a_set_lut()` | `plb_lut_passthrough` |
| PLB TYPE A 软件注入 | `TYPE_A[n][SW_INJECT]` | `plb_type_a_inject_by_sw()` | `plb_lut_passthrough` |
| PLB TYPE B LUT | `TYPE_B[n][LUT][m]` | `plb_type_b_set_lut()` | 官方 `plb/pulseoutput` |
| PLB TYPE B 比较值 | `TYPE_B[n][CMP][m]` | `plb_type_b_set_cmp_val()` | 官方 `plb/qei_encoder` |
| PLB TYPE B 比较模式 | `TYPE_B[n][MODE]` | `plb_type_b_set_cmp_mode()` | 同上 |
| SDM 模块控制 | `SDM_CTRL` | `sdm_init_module()` | `sdm_probe` |
| SDM 通道滤波 | `CH[n][SDCTRLP]` | `sdm_config_channel_filter()` | `sdm_probe` |
| SDM FIFO | `CH[n][SDFIFO]` | `sdm_get_channel_fifo_data()` | 官方 `sdm` |
| SDM 比较器 | `CH[n][SDST]` | `sdm_config_channel_comparator()` | 官方 `sdm` |

表 17-6：TRGM / PLB / SDM 寄存器与驱动 API 对照

## 17.8 例程逐条讲解

### 17.8.1 plb_lut_passthrough：PLB LUT + TRGM 双向路由 + GPTMR 验证

| 项 | 内容 |
|---|---|
| 路径 | `samples/plb/plb_lut_passthrough/` |
| 覆盖特性 | TRGM 输入/输出路由、PLB TYPE A LUT4、PLB 软件注入、GPTMR 捕获验证 |
| 关键配置 | GPTMR0 CH2 输出 1 kHz 方波（500 us 高/低）→ `PLB_IN_00` → LUT0 直通（`0xAAAA`）→ `PLB_OUT00` → `GPTMR0_CAPT_3` |
| 实测现象 | PLB LUT 回读 = 0xAAAA（配置生效）；但 GPTMR 捕获计数仍为 0，捕获通路**未打通**（见 §17.9.1 与 §17.14 待确认 17-E） |
| 实测日志 | `logs/20261030-115640-plb_lut_passthrough.log`；改用引脚+LOOP_BACK 的版本见 `logs/20261030-185324-plb_lut_blink.log` |

关键代码：

```c
/* 1) TRGM: GPTMR0_OUT2 -> PLB_IN_00, PLB_OUT00 -> GPTMR0_CAPT_3 */
trgm_output_t cfg = {0};
cfg.type = trgm_output_same_as_input;
cfg.input = HPM_TRGM0_INPUT_SRC_GPTMR0_OUT2;
trgm_output_config(HPM_TRGM0, TRGM_TRGOCFG_PLB_IN_00, &cfg);
cfg.input = HPM_TRGM0_INPUT_SRC_PLB_OUT00;
trgm_output_config(HPM_TRGM0, HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_3, &cfg);

/* 2) PLB TYPE A 通道0 的表0 = "输出等于 in0"（4 输入索引, 奇数项为 1） */
plb_type_a_truth_t lut = { .val = 0xAAAA };
plb_type_a_set_lut(HPM_PLB, plb_chn0, plb_type_a_table0, &lut);
```

> 为什么用 GPTMR 的方波而不是 PWM 的 TRGO 做源？因为 TRGO 是 5 ns 窄脉冲，
> 跨时钟域会漏采（第 14 章坑 14-3 的实测），而 GPTMR 输出是 500 us 宽脉冲，
> 捕获 100% 可靠。

### 17.8.2 pwm_basic 中的 TRGM 路由实验（反面教材，但价值很高）

| 项 | 内容 |
|---|---|
| 实验 | PWM0 的 TRGO6/TRGO7 → TRGM → GPTMR0 CAP2/CAP3，纯硬件测 PWM 周期/占空比 |
| 结论 | **CMP12 的 TRGO 能捕到，CMP13 的捕不到**；且捕到的周期严重不连续（漏采） |
| 原因 1 | CMP13 受"交叉周期"限制（UM §37.4.8, p.613），计数器 1 重载值为 0 时事件被丢弃 |
| 原因 2 | TRGO 只有 1 个 PWM 时钟周期宽（5 ns），GPTMR 100 MHz 采样 → 漏采 |
| 数据 | 50 kHz 下 2 s 只捕到 3533 个周期（理论 10 万），见 `logs/20261030-113634-pwm_basic.log` |
| 最终方案 | 改用 PWM 中断 + GPTMR 自由运行时基打时间戳（`pwm_basic` 现行实现） |

表 17-7：TRGM 硬件捕获实验结论

### 17.8.3 sdm_probe：SDM 初始化与寄存器自检

| 项 | 内容 |
|---|---|
| 路径 | `samples/sdm/sdm_probe/` |
| 覆盖特性 | SDM 时钟使能、模块/通道初始化、Sinc3 + OSR 256 配置、FIFO/比较器状态回读 |
| 硬件限制 | **没有外部 ΣΔ 调制器**，MDAT 无有效位流，因此读回的数据无意义 |
| 实测现象 | 打印寄存器配置值与数据就绪计数（预期 0），证明初始化通路正确 |
| 实测日志 | `logs/20261030-115504-sdm_probe.log` |

### 17.8.4 plb_lut_blink：PLB 输出到引脚 + LOOP_BACK 回读（推荐做法）

| 项 | 内容 |
|---|---|
| 路径 | `samples/plb/plb_lut_blink/` |
| 覆盖特性 | PLB TYPE A LUT、PLB → TRGM → 引脚输出、IOC LOOP_BACK 回读、GPTMR 捕获验证 |
| 关键配置 | GPTMR0 CH3 输出 1 kHz → TRGM `PLB_IN_00` → LUT0 直通(0xAAAA) → `TRGM_P_15`(PF15)/`TRGM_P_14`(PF14)；PF15 开 LOOP_BACK → `TRGM_P15` → GPTMR0 CAP2 |
| 实测现象 | LUT 回读 0xAAAA ✅、PF15 FUNC_CTL=0x00010011（alt=17=TRGM_P_15 + LOOP_BACK）✅、PF14=0x00000011 ✅；**捕获仍为 0**（见坑 17-7） |
| 实测日志 | `logs/20261030-185324-plb_lut_blink.log` |

```c
/* PLB 输出 -> 引脚, 并打开该引脚的 LOOP_BACK 以便回读 */
cfg.type = trgm_output_same_as_input;
cfg.input = HPM_TRGM0_INPUT_SRC_PLB_OUT00;
trgm_output_config(TRGM, HPM_TRGM0_OUTPUT_SRC_TRGM0_P15, &cfg);
trgm_enable_io_output(TRGM, 1UL << 15);                       /* 引脚输出必须开这一位 */
HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL =
    IOC_PF15_FUNC_CTL_TRGM_P_15 | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
/* 回读: TRGM_P15 -> GPTMR0 CAP2 */
cfg.input = HPM_TRGM0_INPUT_SRC_TRGM0_P15;
trgm_output_config(TRGM, HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_2, &cfg);
```

### 17.8.5 sdm_status_int：SDM 状态/中断/数据就绪自证

| 项 | 内容 |
|---|---|
| 路径 | `samples/sdm/sdm_status_int/` |
| 覆盖特性 | SDM 模块/通道初始化、数据就绪+误差中断、FIFO 门限、比较器阈值、理论数据率换算 |
| 关键配置 | Sinc3 + OSR 256（数据通路）、OSR 32 + 高门限 32000/低门限 1000（幅值通路）、中断使能 |
| 实测现象 | SDM0 时钟 200 MHz → 理论 781 250 SPS；`isr_entries=0`、`data_ready=0`、FIFO 空（无调制器，符合预期） |
| 实测日志 | `logs/20261030-122732-sdm_status_int.log` |

## 17.9 上板实测数据

### 17.9.1 PLB + TRGM 数据通路

| 指标 | 实测 | 说明 |
|---|---|---|
| GPTMR0 输出方波 | 1000 Hz（500 us 半周期） | `s_clk_hz / 1000` 重载 |
| PLB LUT0 配置 | `0xAAAA` | out = in0 |
| PLB LUT0 回读 | `0x0000aaaa` | 说明 `plb_type_a_set_lut()` 写入生效 |
| GPTMR0 OUT2 输出 | 1000 Hz（配置值 1 ms 周期 / 50%） | 内部比较输出 |
| GPTMR0 CAP3 捕获计数 | **0** | 通路未打通，见 §17.14 待确认 17-E |

实测原始输出（`logs/20261030-115640-plb_lut_passthrough.log`）：

```text
letter:/$ lstat
[plb] cap_n=0  PLB_OUT00 上升沿 (还没捕到, 检查 TRGM/PLB 路由)
      PLB LUT0=0x0000aaaa  OUT=100000 Hz  GPTMR=100000000 Hz
```

> `OUT=100000 Hz` 是打印口径问题：固件里写的是 `s_clk_hz / 1000`（本意是重载值
> 100000 ticks = 1 kHz），打印时标成了 Hz，实际输出就是 1 kHz。这个笔误保留在日志里，
> 但结论不受影响——LUT 生效、捕获为 0。

表 17-8：PLB LUT 直通 + TRGM 路由实测（`logs/20261030-115640-plb_lut_passthrough.log`）

### 17.9.2 TRGM 路由可行性实测

| 路由 | 结果 | 数据 |
|---|---|---|
| `PWM0_TRGO_6`(CMP12) → `GPTMR0_CAPT_2` | 成功 | 周期值连续，10 kHz 下可测 |
| `PWM0_TRGO_7`(CMP13) → `GPTMR0_CAPT_3` | 失败 | 状态位永不置位（CMP 交叉周期限制） |
| `GPTMR0_OUT2` → `PLB_IN_00` | **未证成** | 2026-11-02 复测：GPTMR0 CH2/CH3 寄存器确认在跑（`CR=0x508`、CNT 递增、`RLD=99999`、`CMP0=49999`），但 `TRGM_IN[0x10]` 回读 20 ms 内 0 次翻转，无法证明 OUT2 到达 PLB。日志 `logs/20261102-124850-plb_trgm_diag.log` |
| `PLB_OUT00` → `GPTMR0_CAPT_3` / `CAPT_2` | **失败（capture=0）** | 2026-11-02 复测：`ddirect`/`dplb`/`dpin` 全部 0 次捕获；PLB 软件注入 10×100 us 也只有 0 次，且 `TRGM_IN[0x44]` 读不到 PLB 输出。断点在 PLB 输出之后或 TRGM 输入侧，见 17-F/17-G |

表 17-9：TRGM 关键路由实测结论

### 17.9.3 PLB 引脚输出与 LOOP_BACK 回读实测

| 观测点 | 实测值 | 说明 |
|---|---|---|
| PLB TYPE A LUT0 | `0x0000AAAA` | 写入生效 |
| PF15 `FUNC_CTL` | `0x00010011` | alt=0x11(17)=`TRGM_P_15`，bit16=`LOOP_BACK` |
| PF14 `FUNC_CTL` | `0x00000011` | alt=17=`TRGM_P_14`（镜像输出，供示波器） |
| GPTMR0 CAP2 捕获数 | 0 | 仍未打通，见坑 17-7 |

表 17-11：PLB 输出到引脚与回读的实测（`logs/20261030-185324-plb_lut_blink.log`）

### 17.9.4 SDM 无调制器条件下的自证

| 项 | 实测 | 说明 |
|---|---|---|
| SDM0 时钟 | 200 000 000 Hz | `clock_add_to_group(clock_sdm0)` 后读取 |
| 理论数据率 | 781 250 SPS | 200 MHz / OSR(256) |
| 数据就绪中断 | 使能成功，`isr_entries=0` | 没有 MDAT 位流，符合预期 |
| FIFO / 比较器 | `fifo_count=0`，`comparator data=0` | 同上 |
| 结论 | 配置与中断通路**就绪**，缺的是外部 ΣΔ 调制器 | 见 §17.14 待确认 17-A |

表 17-12：SDM 状态/中断自证实测（`logs/20261030-122732-sdm_status_int.log`）

### 17.9.5 捕获通路分段诊断（本板的排障方法）

![图 17-5 PLB→TRGM→GPTMR 捕获通路的分段探针](figures/17/fig-17-5-trgm-probe.png)

图 17-5 把这条链路拆成 5 段，每段都留了一个可以直接读寄存器判断的观测点。

“PLB 输出经 TRGM 进 GPTMR 捕获，计数恒为 0”这种问题，最难的是不知道信号死在哪一段。如图 17-5 所示，我们把这条通路拆成“源 → 路由 → PLB → 捕获”几段，每段都能单独读回。
TRGM 有两个**只读回读寄存器**可以当“片内逻辑分析仪”用：

| 寄存器 | 偏移 | 含义 | 用法 |
|---|---|---|---|
| `TRGM_IN[0..4]` | 0x600 + 4×n | 每个输入源当前电平，位号 = `HPM_TRGM0_INPUT_SRC_*` | 轮询某位，数它 20 ms 内的翻转次数 |
| `TRGM_OUT[0..4]` | 0x680 + 4×n | 每个输出目的当前电平，位号 = `HPM_TRGM0_OUTPUT_SRC_*` | 同上，判断路由“有没有把信号送出去” |

表 17-13：用于本板排障的回读观测点（索引取自 `hpm_trgmmux_src.h`）

| 段 | 观测信号 | TRGM 回读位 | 期望（1 kHz 方波） |
|---|---|---|---|
| ① 源 | GPTMR0 比较输出 OUT2 / OUT3 | `TRGM_IN[0x10]` / `[0x11]` | 20 ms 内约 40 次翻转 |
| ② 路由 | TRGM 输出到捕获通道 | `TRGM_OUT[0x38]`（CAPT_2）/ `[0x39]`（CAPT_3） | 约 40 次翻转 |
| ③ PLB | PLB TYPE A ch0 LUT0 输出 | `TRGM_IN[0x44]`（PLB_OUT00） | 约 40 次翻转 |
| ⑤ 引脚回环 | TRGM0_P15 引脚输入 | `TRGM_IN[0x37]` | 约 40 次翻转 |
| ④ 捕获 | GPTMR0 CHn `CAPPOS` / `SR.CHxCAPF` | 不经 TRGM | 300 ms 内约 300 次 |

配套例程 `samples/plb/plb_trgm_diag/` 把这条链路拆成 5 条命令，任意两段之间换一种接法即可定位断点：

```text
dgen              GPTMR0 CH2/CH3 各输出 1 kHz 方波（CMPEN=1）
dstat             打印 TRGM 配置/回读翻转计数、GPTMR 捕获寄存器、PLB LUT
ddirect <2|3>     GPTMR0 OUTn --TRGM--> CAPT_n             （跳过 PLB）
dplb <2|3>        GPTMR0 OUTn --> PLB_IN_00 -> LUT0 --TRGM--> CAPT_n
dpin <2|3>        PLB_OUT00 --> TRGM_P15 引脚 + IOC LOOP_BACK --> CAPT_n
dsw <n> <us>      PLB 软件注入 n 个脉冲（不依赖 GPTMR 输出）
dquad <ms>        GPTMR 产生 90° 相差 A/B -> PLB TYPE B 4 倍频计数
dfilt <len> <mode> [hz]  引脚回环方波过 TRGM 数字滤波器，数捕获/测周期
dmon <min> <max>  GPTMR 捕获 + 输入监测（MONITOR）阈值试验
```


2026-11-02 在 hpm5e31_1 上跑了 `dall`（`logs/20261102-124850-plb_trgm_diag.log`），结果和设计预期不一样，值得写下来：

- **源在跑，这一点可以确认**：`dstat` 打出 GPTMR0 CH2/CH3 的 `CR=0x00000508`（CEN+CMPEN 都置位）、`CNT` 在递增、`RLD=99999`、`CMP0=49999`，就是 1 kHz 方波该有的配置。
- **但所有 TRGM 回读位在 20 ms 内都是 0**：`TRGM_IN[0x10]`（OUT2）、`[0x11]`（OUT3）、`[0x44]`（PLB_OUT00）、`[0x37]`（TRGM0_P15）全为 0；`TRGM_OUT[0x38/0x39]` 也是 0。
- **连软件注入都读不到**：`dsw 10 100` 用 `SW_INJECT` 往 PLB 灌 10 个 100 us 脉冲（完全不依赖 GPTMR 输出），`TRGM_IN[0x44]` 仍然 0，捕获 0 次。
- 捕获端同样全 0：`ddirect`（跳过 PLB）、`dplb`、`dpin`（PF15/PD15 两个 pad 都试了）都是 0/300 ms。

结论是这条“回读当逻辑分析仪”的路子在本板**没有走通**：读回来的永远是 0，不能用来判断信号死活。
要么这几个只读寄存器在 HPM5E31 上没有真正接到内部信号上，要么需要额外的使能位——头文件里找不到，本轮也没找到。
这段实验因此只留下两个可复用的东西：①“分段换接法 + 每段换一种观测手段”的排查顺序；②“GPTMR 输出寄存器层正常但捕获端看不到”这个事实。
真正的结论仍要等引脚上有示波器或逻辑分析仪。


这套方法的要点是**先证明源在动**：`TRGM_IN[0x10]` 不翻转，后面所有段都不用查；
翻转了再看 `TRGM_OUT`，最后才怀疑 GPTMR 的捕获配置。


## 17.10 性能与调优

1. **TRGM 是组合路由，几乎零延迟**（一个 TRGM 时钟周期），适合把故障信号直连 PWM。
2. **PLB 的 LUT 输出有 1 拍寄存器延迟**：做保护逻辑时要把它算进响应时间。
3. **PLB TYPE B 数据单元跑系统时钟**：用它做分频时，输入脉冲频率必须
   小于系统时钟的一半，否则会丢计数。
4. **SDM 的 OSR 是精度与延迟的旋钮**：电机电流环常用 OSR 32~128（延迟 1~2 us），
   位置检测可用更大 OSR。
5. **TRGM 引脚（`TRGM_Pnn`）是最省事的"内部信号引出脚"**：调试时把一个内部信号
   路由到空闲引脚，用示波器看，比改代码快得多。

## 17.11 坑与注意事项

1. **现象**：想用 SDM 驱动 LED 亮度，找不到"发送位流"的 API。
   **原因**：HPM 的 SDM 是 ΣΔ **接收**单元，`SDM0_DAT` 是输入引脚（UM §43.2, p.723）。
   **解决**：调光用 PWM（第 14 章）或 PLB 做 ΣΔ 调制；SDM 只用于接外部调制器。
2. **现象**：PLB 输出配好了但 GPTMR 捕获不到。
   **原因**：PLB 的输出必须再经 TRGM 才能到其它外设（`HPM_TRGM0_INPUT_SRC_PLB_OUTxx`），
   PLB 自己不会"直连"。
   **解决**：像 `plb_lut_passthrough` 那样配两段 TRGM。
3. **现象**：PLB LUT 写了却输出不对。
   **原因**：真值表位序是"4 bit 输入为索引"，in0 是最低位；`0xAAAA` 才是"out=in0"，
   写成 `0x00FF` 之类的直觉值会得到别的逻辑。
   **解决**：先用 `SW_INJECT` 验证输出通路，再逐位推导真值表。
4. **现象**：TRGO→TRGM→GPTMR 捕获漏事件。
   **原因**：窄脉冲跨时钟域（第 14 章坑 14-3）。
   **解决**：被测信号先在 TRGM 里"展宽"（用 `trgm_output_pulse_at_input_rising_edge`
   只能产生脉冲，不能展宽），更好的是换 GPTMR 输出或改用中断打时间戳。
5. **现象**：SDM 配好了但 FIFO 永远没数据。
   **原因**：没有 MCLK（调制器时钟）或没有 MDAT 位流；SDM 需要外部调制器工作。
   **解决**：接 AD7401/AMC1306 等调制器，并确认 MCLK 由 MCU 提供还是外部提供。
7. **现象**：PLB 输出已配到 PF15/PF14 且 LOOP_BACK 已开，GPTMR CAP2 仍然捕获不到。
   **原因（已定位到现象层面）**：本板实测**所有送到 `GPTMR0_CAPT_3` 的路由都不触发**
   （PWM TRGO7→CAPT_3、GPTMR OUT2→CAPT_3、PLB→CAPT_3 全部 cap=0），而
   `GPTMR0_CAPT_2` 的路由能触发（PWM TRGO6→CAPT_2 实测成功）。
   最终两个例程统一改用 **CAPT_2**；CAPT_3 的可用性列入 §17.14 待确认 17-F。
   **解决**：把捕获通道改到 CH2（CAPT_2），并注意源通道不能再占用 CH2——本例程因此
   用 GPTMR CH3(OUT3) 当信号源。
6. **现象**：PLB TYPE B 计数比预期少。
   **原因**：数据单元跑系统时钟，输入边沿太快会被吞。
   **解决**：降低输入频率，或先用 TYPE A 做边沿检测再送 TYPE B。

## 17.12 优劣评价与同类对比

| 项目 | HPM TRGM+PLB | STM32（HRTIM/互联） | TC397（GTM/TIM） |
|---|---|---|---|
| 片上可编程逻辑 | PLB 8×TYPE A + 8×TYPE B | 无（固定互联） | 无（TIM 固定矩阵） |
| 触发路由 | 全交叉 + 滤波 + 取反/边沿 | 有限的触发选择 | 全交叉 |
| ΣΔ 解调 | SDM 4 通道 + Sinc 滤波 | 部分型号有 DFSDM | 需外部芯片 |
| 可编程逻辑用途 | 自定义保护/分频/解码 | — | — |
| 学习成本 | 高（LUT 真值表/操作码） | 低 | 中 |

表 17-10：TRGM/PLB/SDM 与同类外设对比（STM32/TC397 数据来自厂商公开手册，非本板实测）

**评价**：TRGM 是"零成本的调试神器"——把任何内部信号引到引脚上看波形；
PLB 则是"把 CPLD 塞进 MCU"，做**非标保护逻辑**（比如三路故障相与、编码器 4 倍频）
非常合适，但要接受 LUT 真值表和 TYPE B 操作码的学习成本。
SDM 属于"有需求才用"的模块：只有做隔离电流/位置采样时才需要它。

## 17.13 参考来源

- HPM5E00 用户手册 Rev0.4：§38 TRGM（PDF p.606~620）、§42 PLB（PDF p.707~722）、
  §43 SDM（PDF p.723~741）
- SDK：`hpm_sdk/drivers/inc/hpm_trgm_drv.h`、`hpm_plb_drv.h`、`hpm_sdm_drv.h`；
  `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_trgmmux_src.h`、`hpm_soc_ip_feature.h`（各 MATRIX_SEL）
- 官方例程：`hpm_sdk/samples/drivers/plb/{pulseoutput,qei_encoder,filteroutput}`、
  `sdm/src`（SDM 轮询/中断/比较器/同步四段）
- 本仓库：`samples/plb/plb_lut_passthrough/`、`samples/sdm/sdm_probe/`、
  `samples/pwm/pwm_basic/`（TRGM 实验）、`boards/hpm5e31_1/ext/board_timer.h`
- 实测日志：`logs/20261030-115640-plb_lut_passthrough.log`、`logs/20261030-115504-sdm_probe.log`、`logs/20261030-113*-pwm_basic.log`

- 官方 SDK（v1.13.0）：[github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)，
  在线文档 [hpm-sdk.readthedocs.io](https://hpm-sdk.readthedocs.io)
- HPM5E00 数据手册：[HPM5E00FSV01.pdf](https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf)；
  用户手册 Rev0.4 为厂商内部资料（未公开），页码引用以该版本为准

## 17.14 待确认项与风险

> 已逐条复核（2026-11-02）：未闭环条目均写明了原因与验证命令。

| 编号 | 待确认内容 | 风险 | 验证方法 |
|---|---|---|---|
| 17-A | SDM 在真实 ΣΔ 调制器下的数据精度/延迟 | **已知限制（无调制器无法验证 ENOB、延迟、FIFO 时序）** | 接 AD7401/AMC1306，输入已知电压，扫 OSR 与 Sinc 阶数，对比万用表读数 |
| 17-B | PLB TYPE B 做正交 4 倍频解码的可行性 | **已知限制（LUT 操作码组合复杂，可能丢步）** | 用 GPTMR 产生 90° 相差 A/B → TRGM → PLB，读 TYPE B 数据单元计数与 QEI 对比 |
| 17-C | PLB 做保护逻辑的响应延迟 | **已知限制（1 拍寄存器 + TRGM 延迟，可能超过功率级允许时间）** | 用 GPIO 翻转输入、GPIO 输出观测，逻辑分析仪量延迟 |
| 17-D | TRGM 数字滤波器的实际去抖宽度 | **已知限制（参数单位与时钟域需确认）** | 输入带毛刺方波，扫 `FILTCFG` 看输出是否去抖 |
| 17-F | `GPTMR0_CAPT_3` 与片内捕获通路不触发 | **已知限制（现象已定位，根因未拿到硅片证据）**：2026-11-02 复测，CAPT_2 与 CAPT_3 用同一 GPTMR 源都是 0 次捕获；GPTMR 源本身寄存器层正常；PLB 软件注入也捕不到。本板没有示波器/逻辑分析仪，无法再往下切 | ① 示波器接 PD07（TRGM_P7/OUT2）确认 GPTMR 比较输出引脚真的翻转；② 把已知可用的源（PWM0 TRGO_6）经 PLB 中转后再进 CAPT_2，隔离“PLB 还是 TRGM”这一段；③ 读 `TRGM_OUT[0x39]` 的状态位（本板读到恒 0，见 17-G） |
| 17-G | **TRGM_IN/TRGM_OUT 回读寄存器在本板读不到内部信号**（全 0） | **已知限制（第 17 章的“片内逻辑分析仪”排障方法失效；不能再用回读判断某段通路的死活）** | 写一个最小用例：把 GPTMR 输出或 PWM TRGO 路由到任意输出，同时读 `TRGM_IN[源]`；若仍恒 0，则说明该回读在本硅片不反映内部信号（或需额外使能），届时改用引脚+示波器 |
| 17-E | **PLB 输出 → TRGM → GPTMR 捕获通路未打通**（实测 capture=0，LUT 回读 0xAAAA 正常） | **已知限制（卡住 PLB 类例程的实测闭环；PLB 是否够用无法量化）** | 依次排查：①PLB 是否有全局使能位（查 `hpm_plb_drv.h`/`PLB` 寄存器头的 `GCR` 或 `MODE`）；②TYPE A 通道号与 `TRGM_TRGOCFG_PLB_IN_00` 的对应关系（in0 是否真的接到通道 0 的 bit0）；③把 PLB 输出改路由到引脚 `TRGM_Pnn` 用示波器看有无翻转，先确认 PLB 有没有输出，再查 TRGM 到 GPTMR 的路由；④改用 `plb_type_a_inject_by_sw` 产生宽脉冲（软件拉高 ms 级再拉低）代替 1 拍注入 |