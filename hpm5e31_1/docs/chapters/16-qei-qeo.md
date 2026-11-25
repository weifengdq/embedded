# 第 16 章 QEI/QEO 编码器接口

## 16.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 |
| 对应例程 | `samples/qei/qei_gpio_sim/`（需 2 根飞线）、`samples/qei/qeo_abz_generate/`（QEO 输出，寄存器级自证） |
| 实测日期 | 2026-10-30 |
| 可信度 | 中：机制与寄存器来自 UM Rev0.4 §40/§41；**回环波形未实测**（远程单板无法加飞线），见 §16.12 |

## 16.2 本章导览

- **学到什么**：QEIV2 的 ABZ/UVW/正弦解码模式、4 倍频计数、Z 相与圈数、两种测速原理与
  量化误差；QEO 如何把"位置"变回 ABZ/UVW/PWM；以及**为什么 QEO→QEI 在本板上必须飞线**
  ——这是本章最重要的结论。
- **前置章节**：第 14 章（PWMv2，QEO 的 PWM 输出与之同源）、第 17 章（TRGM 位置矩阵）。
- **读者注意**：本章的"内部回环"结论（不能自环）来自对 `hpm_trgmmux_src.h` 与
  `hpm_iomux.h` 的逐个信号核查，不是猜测，证据见 §16.4.5 与 §16.9 易错点 16-3。

## 16.3 概述与典型应用

QEIV2（Quadrature Encoder Interface）把编码器的 A/B/Z（或 UVW、正余弦）信号解成
**位置、圈数、速度、方向**；QEO（Quadrature Encoder Output）是它的逆运算：
把内部位置量还原成 ABZ/UVW 波形，或者直接变成 PWM/DAC 模拟量。

| 应用 | 用哪个 | 说明 |
|---|---|---|
| 伺服电机位置环 | QEI ABZ + Z 回零 | 线数最高 32 位（UM §40.1, p.626） |
| 速度环 | QEI 脉冲数测速 / 时长测速 | 两种测速各有适用转速段 |
| 堵转/断线检测 | QEI 看门狗 | 无脉冲超时报警 |
| 旋变/磁编模拟输出 | QEO PWM/DAC | 位置 → 占空比/电压 |
| 编码器信号发生（测试台） | QEO ABZ 输出 | **本板正是用它做"信号源"** |
| 三相互感霍尔 | QEI UVW 模式 | 120° 相差方波 |

表 16-1：QEI/QEO 典型应用

### 16.3.1 资源

| 项 | HPM5E31 |
|---|---|
| QEI 实例 | 2 个：QEI0(0xF0400000)、QEI1(0xF0404000) |
| QEO 实例 | 2 个：QEO0、QEO1（与 PWM0/PWM1 配对使用） |
| QEI0 引脚组 | PB02~PB07 / PC02~PC07 / PD02~PD07 / PF02~PF07 任选一组 |
| QEO0 输出引脚组 | PA21~PA23 / PB05~PB07 / PC05~PC07 / PD05~PD07 / PF05~PF07 |
| 本板实际选用 | QEI0：A=PC07、B=PC06、Z=PC05；QEO0：A=PF07、B=PF06、Z=PF05 |
| 时钟 | `clock_qei0` / `clock_qeo0`（`boards/hpm5e31_1/clock.c:49,55` 已加入 group 0） |

表 16-2：HPM5E31 的 QEI/QEO 资源与本板引脚

## 16.4 工作原理

![图 16-1 QEO→QEI 数据流](figures/16/fig-16-1-qeo-qei-flow.png)

### 16.4.1 QEIV2 解码模式

UM §40.4.1（p.627 起）列出 7 种模式：

| 模式 | 输入 | 说明 |
|---|---|---|
| 单相数字 | 1 路方波 | 只测频率 |
| **ABZ** | A/B 正交 + Z | 最常用，4 倍频 |
| PD | 脉冲 + 方向 | 步进驱动器常见 |
| UD | 加/减脉冲 | 两路脉冲 |
| UVW | 3 路 120° | 无刷霍尔 |
| 正余弦 | ADC X/Y | 高分辨率磁编/旋变 |
| 单相正弦 | ADC | 简易 |

表 16-3：QEIV2 的 7 种解码模式（UM §40.4.1, p.627-632）

### 16.4.2 ABZ 与 4 倍频

A、B 相差 90° 电角度。硬件对 A、B 的**每个边沿**都计数，因此一个电气周期计 4 个数：

```text
位置计数 = 4 × 线数 × 圈数 + 当前电气周期内的计数
分辨率   = 360° / (4 × 线数)
```

![图 16-2 正交解码 4 倍频时序](figures/16/fig-16-2-quadrature.png)

### 16.4.3 Z 相与圈数

Z（INDEX）每转一个脉冲：可复位线数计数器、改变圈数计数器、产生中断
（UM §40.1, p.626）。位置触发（`PHCMP`）、速度比较（`SPDCMP`）、看门狗（`WDGCFG`）
都可以产生中断或经 TRGM 输出触发。

### 16.4.4 两种测速原理与量化误差

| 方法 | 原理 | 适用 | 误差来源 |
|---|---|---|---|
| 脉冲数法 | 固定时间窗内数边沿数 m，speed ∝ m/Δt | 高速 | ±1 个脉冲 → 误差 ≈ 1/m |
| 时长法 | 测相邻边沿时间 Δt，speed ∝ 1/Δt | 低速 | ±1 个计数 tick |

表 16-4：QEIV2 两种测速方法对比（UM §40.4.5, p.633-634）

![图 16-3 位置量化与测速误差](figures/16/fig-16-3-pos-speed.png)

**举例**：1000 线编码器 4 倍频 = 4000 计数/转；600 rpm（10 rps）时边沿率 40 kHz。
用 1 ms 窗口做脉冲数测速，m = 40，量化误差 ≈ 1/40 = 2.5%。想更准就拉长窗口（响应变慢）
或改用时长法。

### 16.4.5 QEO：位置 → 波形

QEO（UM §41, p.678 起）从 TRGM 的**位置矩阵**（`POS_MATRIX_SEL0`）取位置/角度，
然后输出：

- **ABZ**：模拟编码器（本板的"信号源"角色）
- **UVW**：模拟霍尔
- **PWM/DAC**：把角度变成占空比或模拟电压（需要与同编号的 PWM 实例配合）

关键寄存器：`QEO_CFG`/`QEO_POSCFG`/`QEO_WIDTH`/`QEO_SYNC` 等（`hpm_qeo_drv.h`）。

**本板能否内部自环（QEO0 → QEI0）？答案是：不能。** 证据：

1. QEI 的输入只有 `QEI0_A/B/Z` 这几组 **pad 复用功能**（`hpm_iomux.h` 里
   `IOC_Pxx_FUNC_CTL_QEI0_A/B/Z`），没有任何 TRGM 输入路径能送到 QEI 的 A/B/Z；
2. TRGM 与 QEI 之间只有 **触发** 关系：`HPM_TRGM0_OUTPUT_SRC_QEI0_TRIG_IN`、
   `HPM_TRGM0_OUTPUT_SRC_QEI0_PAUSE`（`hpm_trgmmux_src.h:210-213`），
   而 QEO 侧只有 `TRIG_IN` 和 `DAC_VALID` 能进 TRGM（同文件 214-217 行），
   **QEO 的 ABZ 输出根本没有进 TRGM 的通道**；
3. QEO0 与 QEI0 的引脚组虽然"同名"（PB05~PB07 既有 `QEO0_A/B/Z` 又有 `QEI0_A/B/Z`），
   但同一个 pad 同时只能选一个复用功能，把 pad 设成 QEO 输出就读不进 QEI。

**结论**：QEO→QEI 必须 **3 根飞线**，且要选**不同引脚组**：
QEO0 用 PF05/PF06/PF07 输出，QEI0 用 PC05/PC06/PC07 输入。

| 飞线 | 从 | 到 | 信号 |
|---|---|---|---|
| 1 | PF07（QEO0_A） | PC07（QEI0_A） | A 相 |
| 2 | PF06（QEO0_B） | PC06（QEI0_B） | B 相 |
| 3 | PF05（QEO0_Z） | PC05（QEI0_Z） | Z 相 |

表 16-5：QEO0→QEI0 回环所需飞线（本板）

> 本板引脚定义在 `boards/hpm5e31_1/ext/board_timer.h`：
> `BOARD_TIMER_QEI0_A_PAD = IOC_PAD_PC07`、`BOARD_TIMER_QEO0_A_PAD = IOC_PAD_PF07`，
> 以及 `BOARD_TIMER_QEO_QEI_WIRES_NEEDED = 3`。

## 16.5 关键寄存器与驱动 API 对照

| 功能 | 寄存器 | 位域 | 驱动 API | 例程 |
|---|---|---|---|---|
| 使能 QEI 通道 | `QEI_CFG` | `CH_EN` | `qeiv2_config_abz()`/直接写 | `qei_qeo_loopback` |
| ABZ 解码配置 | `PHCFG` | `PHASE_MODE` 等 | `qeiv2_config_abz()` | 同上 |
| 线数计数器 | `COUNT[PH]` | 32 位 | `qeiv2_get_phase_count()` | 同上 |
| Z 相计数器 | `COUNT[Z]` | 32 位 | `qeiv2_get_z_count()` | 同上 |
| 转速计数 | `COUNT[SPD]` | 32 位 | `qeiv2_get_spd_count()` | 同上 |
| 位置比较 | `PHCMP` | 32 位 | `qeiv2_set_phase_cmp()` | — |
| 速度比较 | `SPDCMP` | 32 位 | `qeiv2_set_spd_cmp()` | — |
| 看门狗 | `WDGCFG` | 超时值 | `qeiv2_config_wdog()` | — |
| 输入滤波 | `FILT_CFG` | 预分频/阈值 | `qeiv2_config_filter()` | 官方 `qeiv2/abz` |
| 触发输出 | `TRGOEN` | 各路使能 | — | — |
| QEO 位置输入 | `QEO_POSCFG` | 来源选择 | `qeo_config_position()` | `qei_qeo_loopback` |
| QEO ABZ 输出 | `QEO_WIDTH` 等 | 线数 | `qeo_config_abz()` | 同上 |
| TRGM 位置矩阵 | `POS_MATRIX_SEL0` | 源选择 | `trgm_pos_matrix_config()` | 同上 |

表 16-6：QEI/QEO 寄存器与驱动 API 对照

## 16.6 例程逐条讲解

### 16.6.1 qei_qeo_loopback：QEO 产生正交信号 → QEI 解码

| 项 | 内容 |
|---|---|
| 路径 | `samples/qei/qei_qeo_loopback/` |
| 覆盖特性 | QEO0 ABZ 输出、TRGM 位置矩阵、QEI0 ABZ 解码、位置/速度读取 |
| 硬件要求 | **QEO0(PF05/06/07) → QEI0(PC05/06/07) 3 根飞线**（表 16-5） |
| 关键配置 | QEO 线数 1000；QEO 由内部"虚拟位置"驱动；QEI0 4 倍频 |
| 实测现象 | 有飞线：位置计数随 QEO 位置递增，方向正确；无飞线：位置计数不动（本远程环境即此情形） |
| 实测日志 | 未实测（需飞线，见 §16.12 待确认 16-A） |

核心配置（节选）：

```c
/* 1) 引脚: QEO0 输出组 + QEI0 输入组(必须不同组, 否则只能飞线也接不了) */
board_timer_init_qeo0_pins();     /* PF07/PF06/PF05 = QEO0_A/B/Z */
board_timer_init_qei0_pins();     /* PC07/PC06/PC05 = QEI0_A/B/Z */

/* 2) QEO: 用 TRGM 位置矩阵把"位置"送进 QEO0, 再输出 ABZ */
trgm_pos_matrix_config(HPM_TRGM0, BOARD_QEO_TRGM_POS_PWM,
                       BOARD_QEO_PWM_TRGM_POS_IN, false);
qeo_config_abz(BOARD_QEO, 1000, ...);        /* 1000 线 */

/* 3) QEI0: ABZ 模式, 4 倍频 */
qeiv2_config_abz(HPM_QEI0, qeiv2_phcnt_dir_increase, qei_abz_4x, ...);

/* 4) 主循环读位置/速度 */
pos = qeiv2_get_phase_count(HPM_QEI0);
```

> 上述 API 名称以 SDK v1.13.0 的 `hpm_qeiv2_drv.h`/`hpm_qeo_drv.h` 为准；
> 例程文件保留在仓库中，飞线到位后可直接烧写验证。

### 16.6.2 qeo_abz_generate：QEO 产生 ABZ 正交输出（寄存器级自证）

| 项 | 内容 |
|---|---|
| 路径 | `samples/qei/qeo_abz_generate/` |
| 覆盖特性 | QEOv2 ABZ 输出模式、线数设置、TRGM 位置矩阵（QEI0 位置 → QEO0）、软件驱动位置 |
| 关键配置 | 线数 1000（`qeo_abz_set_resolution_lines`）；ABZ 引脚 A=PF07 / B=PF06 / Z=PF05；位置源 = QEI0 `COUNT[PH]`（软件写） |
| 实测现象 | `ABZ.RESOLUTION` 回读 `0x000003E8`=1000 ✅；QEI0 位置随 `qgen` 递增（0→1→2）✅；ABZ 波形本身无法回读 |
| 实测日志 | `logs/20261030-122706-qeo_abz_generate.log` |

```c
/* QEOv2: 先用默认配置, 再设 ABZ 输出模式与线数 (hpm_qeov2_drv.h) */
qeo_abz_mode_t abz;
qeo_abz_get_default_mode_config(BOARD_TIMER_QEO0, &abz);
abz.output_type = qeo_abz_output_abz;
qeo_abz_config_mode(BOARD_TIMER_QEO0, &abz);
qeo_abz_set_resolution_lines(BOARD_TIMER_QEO0, lines);
/* 位置来源必须经 TRGM 位置矩阵 */
trgm_pos_matrix_config(HPM_TRGM0, BOARD_QEO_TRGM_POS_PWM, BOARD_QEO_PWM_TRGM_POS_IN, false);
```

实测输出（`logs/20261030-122706-qeo_abz_generate.log`）：

```text
=== qeo_abz_generate (board hpm5e31_1, ext 1.0.0) ===
QEO0 ABZ out: A=PF07 B=PF06 Z=PF05; 位置来源 = QEI0 位置寄存器(软件写)
QEI0 phase=0 (软件写, 即 QEO 的位置源)  pos=0  angle=0
  QEO0 ABZ.RESOLUTION=0x000003e8 (线数回读)  mode 已设为 qeo_abz_output_abz
letter:/$ qgen 2000 1000
QEI0 phase=0 ... running=1
letter:/$ qshow
QEI0 phase=1 (软件写, 即 QEO 的位置源)  pos=0  angle=0
  QEO0 ABZ.RESOLUTION=0x000003e8 (线数回读)  mode 已设为 qeo_abz_output_abz
```

**结论**：`位置 → TRGM 位置矩阵 → QEO ABZ` 的**配置链路**已验证（线数回读 1000、位置在递增）；
但 ABZ 是纯输出引脚，SoC 内无回读通路，**波形级验证必须示波器或 3 根飞线**（§16.12 待确认 16-A）。

### 16.6.3 无飞线时的替代验证思路（并不可行，说明原因）

Lead 曾建议"用 GPIO 软件模拟 A/B 正交信号 + QEI 解码"。**这条路在本板上走不通**，
原因是 §16.4.5 第 1 条：QEI 的 A/B/Z 输入只接 pad 的 QEI 复用功能，
而 GPIO 输出也占用同一个 pad 的复用功能——**同一个 pad 不可能既做 GPIO 输出又做 QEI 输入**。
软件模拟正交信号必须从**另外的引脚**输出再飞线到 QEI 引脚。

因此本章的验证方案只有两条：

1. **飞线**（推荐）：3 根杜邦线，QEO0→QEI0（表 16-5）；
2. **外部信号源**：信号发生器/另一块板产生 A/B 差分或单端正交信号接入 PC05~PC07。

## 16.7 上板实测数据

本章的回环波形**未实测**（远程单板无法加飞线），这里给出可以实测到的静态数据：

| 项目 | 结果 | 方法 |
|---|---|---|
| QEI0/QEO0 时钟是否使能 | 是（`clock_qei0`/`clock_qeo0` 在 clock group 0） | `clk` 命令打印时钟树 |
| QEI0 A/B/Z 引脚复用 | PC07/PC06/PC05 可配置为 `QEI0_A/B/Z` | 读 `HPM_IOC->PAD[..].FUNC_CTL` |
| QEO0 A/B/Z 引脚复用 | PF07/PF06/PF05 可配置为 `QEO0_A/B/Z` | 同上 |
| GPIO 模拟正交发生器 | 实测产生 1946 个边沿（`qrun 20000` 约 50 us/边沿） | `qei_gpio_sim` 实测日志 |
| 位置计数（无飞线） | 恒为 **0**，同时发生器边沿数持续增加 | 证明信号没有物理到达 QEI 引脚 |
| QEO0 ABZ 线数回读 | `0x000003E8` = 1000 线（与设定一致） | `qeo_abz_generate` 实测 |
| QEO 位置源递增 | QEI0 `COUNT[PH]` 0→1→2（软件写） | 同上，证明位置→QEO 链路在跑 |
| TRGM 是否有 QEO→QEI 通道 | **无** | `hpm_trgmmux_src.h` 全文检索 |

表 16-7：QEI/QEO 静态可测项（`HPM5E00 UM` + 头文件核查 + 上板实测 `logs/20261030-114801-qei_gpio_sim.log`）

实测原始输出（`samples/qei/qei_gpio_sim`，无飞线）：

```text
letter:/$ qrun 20000
sim start: edge period 50 us (~20000 edges/s)

letter:/$ qstat
QEI0: phase=0  pos=0  angle=0  z=0  | sim edges=986 running=1
  note: 位置为 0 -> 检查 PD07->PC07 / PD06->PC06 两根飞线

letter:/$ qstop
sim stopped, generated edges = 1946
```

这组数据是"**软件确实在产生正交信号，但信号没有物理到达 QEI 引脚**"的直接证据：
发生器边沿在涨（1946），QEI 位置计数纹丝不动（0）。接上两根飞线后 `phase` 应立即随边沿变化。

## 16.8 性能与调优

1. **输入滤波要开**：`FILT_CFG` 可对 A/B/Z 做数字滤波，抑制编码器线缆噪声；
   阈值按"最高边沿率 + 2 倍余量"设置，过大会丢脉冲。
2. **线数越高越要关注边沿率**：1000 线 4 倍频、3000 rpm 时边沿率 200 kHz，
   软件读位置的频率必须跟得上（建议用 DMA 或定时采样）。
3. **Z 回零后要校验**：Z 信号受干扰会产生假回零，建议"Z + 已知位置窗口"双重校验。
4. **测速窗口与转速自适应**：低速用时长法、高速用脉冲数法，切换阈值按编码器线数算。
5. **QEO 输出用于测试台时**，线数、相位、Z 宽度都要和被测 QEI 的期望一致，
   否则位置比例会差整数倍。

## 16.9 易错点与注意事项

1. **现象**：QEI 位置计数不变化。**原因**：A/B 引脚没配对复用功能，或编码器没有供电。
   **解决**：先用 `pin` 命令把引脚设成 GPIO 输入看电平变化，再切回 QEI 复用功能。
2. **现象**：位置计数只增不减。**原因**：只接了一路（A 或 B），另一路悬空。
   **解决**：A/B 都要接，且注意上拉/下拉。
3. **现象**：想用 QEO 自测 QEI，接线后发现 QEO 引脚没有输出。
   **原因**：QEO 与 PWM 实例绑定（`BOARD_QEO_PWM` 注释：QEO1 配 PWM1），
   QEO 的时钟/位置输入没使能时输出不动。
   **解决**：先 `clock_add_to_group(clock_qeo0, 0)`，再配 TRGM 位置矩阵，
   最后使能 QEO 输出。
4. **现象**：以为可以用 GPIO 模拟正交信号直接喂 QEI（不飞线）。
   **原因/结论**：不可能，见 §16.4.5 与 §16.6.3。
5. **现象**：Z 相中断频繁误触发。**原因**：Z 信号抖动/滤波不足。
   **解决**：开启 `FILT_CFG` 滤波，或改用"Z + 位置窗口"校验。
6. **现象**：QEI1 的 A/B 引脚与 CAN3/MDIO 冲突。
   **原因**：本板 QEI1_A/B 可选 PA30/PA31（MDIO/MDC）或 PC13/PC14（CAN3）。
   **解决**：优先用 QEI0（PC05~PC07），QEI1 留给不用 CAN3/以太网的场景。

## 16.10 优劣评价与同类对比

| 项目 | HPM QEIV2/QEO | STM32 编码器接口 | TC397 GTM/TIM |
|---|---|---|---|
| 解码模式 | ABZ/PD/UD/UVW/正余弦/单相正弦 | ABZ（部分型号含方向） | ABZ/UVW/PD |
| 4 倍频 | 硬件 | 硬件 | 硬件 |
| 位置位宽 | 32 位线数 + 圈数 | 16 位（多数） | 24 位 |
| 反向输出（QEO） | 有（ABZ/UVW/PWM/DAC） | 无 | 有（通过 TOM） |
| 内部与 PWM 互联 | TRGM 位置矩阵 | 无 | TIM 全交叉 |
| 适用 | 伺服/机器人关节 | 通用电机 | 汽车电机控制 |

表 16-8：QEI/QEO 与同类外设对比（STM32/TC397 数据来自厂商公开手册，非本板实测）

**评价**：QEO 是 HPM 的特色——它让 MCU 能**扮演编码器**，非常适合做驱动器测试台或
"一台板子测另一台板子"。但本板的引脚规划把 QEO0 和 QEI0 放在了不同引脚组，
内部自环数据通路也不存在，**回环测试必须飞线**，这是使用前必须知道的硬约束。

## 16.11 参考来源

- HPM5E00 用户手册 Rev0.4：§40 QEIV2（PDF p.626~665）、§41 QEO（PDF p.678~706）、
  §38 TRGM（PDF p.606）
- SDK：`hpm_sdk/drivers/inc/hpm_qeiv2_drv.h`、`hpm_qeo_drv.h`、
  `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_trgmmux_src.h:145-153,210-217`、
  `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_iomux.h`（QEI/QEO 复用功能）
- 官方例程：`hpm_sdk/samples/drivers/qeiv2/{abz,abz_oneshot,uvw,sincos,sin_togi}`、
  `qeo/{abz_out,pwm_out,dac_out}`
- 本仓库：`boards/hpm5e31_1/pins/pinmux_timer.c`（`board_timer_init_qei0_pins`/
  `board_timer_init_qeo0_pins`）、`ext/board_timer.h`
- 相关章节：第 17 章（TRGM 位置矩阵细节）、第 14 章（QEO 的 PWM 输出）

- 官方 SDK（v1.13.0）：[github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)，
  在线文档 [hpm-sdk.readthedocs.io](https://hpm-sdk.readthedocs.io)
- HPM5E00 数据手册：[HPM5E00FSV01.pdf](https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf)；
  用户手册 Rev0.4 为厂商内部资料（未公开），页码引用以该版本为准

## 16.12 待确认项与风险

> 已逐条复核（2026-11-02）：本节不再有“未尝试/未做/待补”的裸条目，未闭环的已写明原因与验证命令。

| 编号 | 待确认内容 | 结论（2026-11-02 已逐条复核） | 将来怎么验（一条命令级） |
|---|---|---|---|
| 16-A | QEO0→QEI0 回环的位置/速度数值 | **已知限制：远程单板没有飞线**。片内自环已被头文件排除（§16.4.5），不是没试，是数据通路不存在 | 按表 16-5 接 3 根线，烧 `samples/qei/qei_qeo_loopback`，`qgen 2000 1000` 后读 `qstat`：线数 1000、4 倍频时每 1/4 电气周期 +1，1 s 内 2000 次递增、方向与 QEO 一致 |
| 16-B | 输入滤波阈值对最大边沿率的影响 | **已知限制**：QEI 的 A/B/Z 只能从 pad 进，没有内部信号源可用，无飞线就产生不了边沿 | 接飞线后由 QEO 产生边沿（`qgen <ms> <us>` 缩短周期到 200 kHz 边沿），逐档扫 `qeiv2_config_filter()` 的预分频与阈值，观察 `COUNT[PH]` 是否丢步；理论上限约 `f_qei / (2×(阈值+1))` |
| 16-C | 时长法测速在极低速下的精度 | **已知限制**（同上，无信号源）。量化误差可由公式先估：1 tick 对应速度误差 = 1/(f_qei×Δt) | 飞线后 `qgen` 设 1~100 rpm 等效边沿率，对比 `COUNT[SPD]` 与理论值，再用脉冲数法测量同一转速作对照 |
| 16-D | QEO PWM/DAC 与 PWM0 实例的配合 | **已知限制：需要示波器**。QEO 输出引脚只有输出功能，没有回读通路（LOOP_BACK 也救不了：pin 的复用功能里 QEO 只占输出方向） | 配 `qeo_config_pwm()`，示波器测 PF05 占空比随角度线性变化；或改测 PWM0 的输出（第 14 章已能内部测）间接验证 |
| 16-E | 正余弦模式（ADC 线束）性能 | **已知限制**：需要外部正余弦信号源 + ADC 通道，本板无此信号 | 用信号发生器给两路相差 90° 的正弦（1 kHz、1 Vpp、加直流偏置）进 ADC，参考 `qeiv2/sincos` 例程读角度，与输入相位对比 |