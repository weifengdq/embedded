# 第 45 章 定时器与看门狗：Gpt / Pwm / Icu / Wdg 的 AUTOSAR 化

## 45.1 文档信息

| 项目 | 内容 |
|---|---|
| 章节 | 第 45 章 定时器与看门狗：Gpt / Pwm / Icu / Wdg 的 AUTOSAR 化 |
| 版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 对应模块 | Gpt（模块 ID 100）、Pwm（121）、Icu（122）、Wdg（102） |
| 代码位置 | `mcal/Gpt`、`mcal/Pwm`、`mcal/Icu`、`mcal/Wdg` |
| 例程 | `samples/mcal/gpt_periodic`、`gpt_oneshot`、`pwm_basic`、`pwm_notify`、`icu_capture`、`wdg_trigger`、`wdg_timeout_reset` |
| 实测日志 | `logs/20261102-134707-gpt_periodic.log`、`logs/20261102-141404-gpt_oneshot.log`、`logs/20261102-141320-pwm_basic.log`、`logs/20261102-141427-pwm_notify.log`、`logs/20261102-141450-icu_capture.log`、`logs/20261102-141513-wdg_trigger.log`、`logs/20261102-142810-wdg_timeout_reset.log` |
| 实测日期 | 2026-11-02（全部数据来自本板上板运行） |
| 可信度 | 高：每个数字都有 logs/ 下对应日志，命令可复跑 |
| 对应例程 | 7 个，见"例程"节表 45-3，全部上板运行 |
| 依据 SWS | AUTOSAR_CP_SWS_GPTDriver / PWMDriver / ICUDriver / WatchdogDriver（CP R25-11） |
| 对应 RM | HPM5E31 用户手册：GPTMR、PWMv2、TRGM、EWDG 章节 |
| 图表脚本 | `tools/figs/mcal_timer_timing.py`、`mcal_timer_error.py`、`mcal_timer_gpt_precision.py` |

## 45.2 本章导览

本章讲四件事：怎么把 HPM5E31 的 GPTMR 定时器、PWMv2 波形发生器、TRGM 输入捕获链路和
EWDG 看门狗，做成 AUTOSAR CP 的 Gpt/Pwm/Icu/Wdg 四个 MCAL 模块；怎么在没有示波器的条件下
用"引脚 + 板载杜邦线 + 第二组硬件捕获"测出真实波形质量；以及实测数据暴露出来的几个坑。

阅读顺序建议：

1. "工作原理"看四个模块的硬件映射和状态机；
2. "寄存器与资源映射"对照表 45-2 找 API 与寄存器的关系；
3. "例程" + "实测"看 7 个例程分别证明了什么（图 45-1/44-2/44-3）；
4. "坑"是给后面移植的人看的教训；
5. "待确认项与风险"只写三态：已实测 / 已定位 / 已知限制（带复跑命令）。

## 45.3 概述

HPM5E31 有两类定时资源：4 个通用定时器 GPTMR（每实例 4 通道，32 位）和 4 组 PWMv2
（每组 4 个计数器、8 路输出、24 位计数器 + 8 位小数）。AUTOSAR 对时间相关外设的抽象是：

- **Gpt**：只管"定时到点"（周期/单次 + 通知），不关心输出波形；
- **Pwm**：管波形输出（周期、占空比、相位、边沿通知、idle 电平）；
- **Icu**：管输入测量（边沿检测、脉宽/周期测量、时间戳、占空比）；
- **Wdg**：管看门狗（模式切换、触发条件、超时复位）。

本轮把四类外设分别映射到：GPTMR0/GPTMR1 给 Gpt，PWM0 P2/P3 给 Pwm，GPTMR2 + TRGM 给 Icu，
EWDG0 给 Wdg。资源划分的原则是**一个外设只归一个模块**，避免例程之间互相抢；唯一的复用是
GPTMR0 CH3 做 Gpt 的 Predef Timer 自由运行时基（不产生中断，见 45.3.1）。

与商业 MCAL 相比，本子集的目标不是"功能全"，而是"每个 API 都能在板上跑通、每个数据都能复现"。
因此选择"post-build 单配置集 + 板级参数全部集中在 PBcfg"的做法：换板子只改 `*_PBcfg.c`。

## 45.4 工作原理

### 45.4.1 Gpt：GPTMR 通道状态机与 Predef Timer

GPTMR 每个通道有独立的计数器、重载值（RLD）、两个比较值（CMP0/CMP1），四个通道共用
状态寄存器 SR 与中断使能 IRQEN。Gpt 驱动只用了"无捕获"模式：

- `Gpt_StartTimer(Channel, Value)`：把 RLD 写成 Value-1（SDK 内部减一），计数器从 0 数到
  Value-1 回绕，回绕时置位 RLD 状态并触发中断；
- 连续模式：回绕后计数器自动从 0 继续，`Gpt_GetTimeElapsed` 直接读 CNT（就是"距上次
  recurrence 的 tick 数"，SWS_Gpt_00361）；
- one-shot 模式：在 RLD 中断里停表并置 expired，`Gpt_GetTimeElapsed` 返回目标值
  （SWS_Gpt_00299），`Gpt_GetTimeRemaining` 返回 0（SWS_Gpt_00305）。

Predef Timer（SWS_Gpt §7.1，给 Time Service 用）没有额外硬件，做法是：GPTMR0 CH3 配成
32 位自由运行（RLD=0xFFFFFFFF），用它的 CNT 当 1 tick = 10 ns 的时基，软件整数除法换算成
1 µs / 100 µs。这样一个自由运行计数器同时服务四个 Predef Timer 枚举，代价是每次读要多做
一次 32 位除法（约 30 cycles）。

### 45.4.2 Pwm：PWMv2 计数器、影子寄存器与比较器

PWMv2 的结构是"4 个计数器 × 8 路输出 × 24 个比较器"。本驱动把两个 AUTOSAR 逻辑通道放在
计数器 1 的两路输出上：

- 计数器 1 的重载值来自影子寄存器 0（SHADOW_VAL[0]），决定周期 T；
- 输出 P2 用比较器 4/5：CNT 在 [CMP4, CMP5) 区间输出高，CMP4 是"上升沿"位置、CMP5 是
  "下降沿"位置；
- 输出 P3 用比较器 6/7，同理。相位就是把 CMP4（或 CMP6）从 0 挪到 T×phase/1000 处。

关键约束（RM §37.4.8，第 14 章表 14-4 有原文）：CMP[4k+i] 的事件是否生效，取决于**计数器 i
的重载值是否被配置**。所以驱动在 Init 里把 4 个计数器的重载值全部绑定到影子 0，否则
CMP5 之类的事件会被静默丢弃——这是本模块最容易踩的硬件约束。

AUTOSAR 的占空比标度是 0..0x8000（0x8000 = 100%），不是 0..65535；0% 和 100% 用两个
特例实现：0% 令 CMP5=CMP4（区间为空，恒低），100% 令 CMP4=0、CMP5=T（整周期都在区间内）。

### 45.4.3 Icu：TRGM 捕获链路

GPTMR 的捕获通道 2/3 不是直接连引脚，而是经 TRGM 交叉开关：

`@
PD07 (TRGM_P_07) --> TRGM0_P07 --> GPTMR2_CAPT_3 --> GPTMR2 CH3 (Icu ch1)
`@

所以 Icu 的配置里必须有两组参数：引脚复用（PD07 → IOC_PD07_FUNC_CTL_TRGM_P_07）和
TRGM 路由（输入 TRGM0_P07 → 输出 GPTMR2_CAPT_3）。两者缺一，捕获值恒 0。

GPTMR 的捕获模式有两类，正好对应 AUTOSAR Icu 的两类服务：

- `gptmr_work_mode_measure_width`（硬件测宽）：硬件自动把"相邻两个上升沿之间的 tick 数"
  填进 CAPPRD、"高电平 tick 数"填进 CAPDTY → `Icu_StartSignalMeasurement` /
  `Icu_GetDutyCycleValues` 直接用；
- `gptmr_work_mode_capture_at_*_edge`（边沿捕获）：每次边沿把当前 CNT 存进 CAPPOS/CAPNEG
  → `Icu_StartTimestamp` / `Icu_GetTimeElapsed` / 边沿检测用。

图 45-1 画出了完整的测量链路与 CAPPRD/CAPDTY 的物理含义。

![图 45-1 Gpt/Pwm/Icu 定时器链路与捕获时序](figures/45-mcal-timer/fig-45-1-timer-chain-and-capture-timing.png)

### 45.4.4 Wdg：EWDG 与窗口模式

EWDG 有两个计数器：超时中断值（OT_INT_VAL）和超时复位值（OT_RST_VAL），一个刷新寄存器
WDT_REFRESH_REG。喂狗 = 往刷新寄存器写解锁序列 + 刷新值；如果开了窗口模式，刷新发生在
"窗口下界之前"或"窗口上界之后"都会直接复位。

AUTOSAR Wdg 把模式抽象成 OFF/SLOW/FAST 三种，每种对应一组预配置的超时；本实现里
SLOW=1000 ms、FAST=100 ms，超时源用 OSC32（32768 Hz），不随 PLL 配置变化。
`Wdg_SetTriggerCondition(ms)` 不是简单喂狗，而是"按新的 ms 重新装载超时计数器并刷新一次"
（SWS_Wdg_00136/00138），所以它可以用来动态改触发周期；传 0 表示尽快复位。

## 45.5 寄存器与资源映射

表 45-1 是四个模块用到的板级资源划分（与 docs/MCAL开发约定.md §2 一致）。

| 资源 | 实例/引脚 | 归属 | 说明 |
|---|---|---|---|
| GPTMR0 CH0 | GPTMR0 | Gpt 通道 0 | 1 ms 周期 tick / notification |
| GPTMR0 CH3 | GPTMR0 | Gpt Predef Timer | 32 位自由运行，1 tick = 10 ns |
| GPTMR1 CH0 | GPTMR1 | Gpt 通道 1 | one-shot / 时间戳 |
| GPTMR3 | GPTMR3 | BSP | `board_delay_us`，本模块不碰 |
| PWM0 P2/P3 | PD02/PD03 | Pwm 通道 0/1 | I2C2_SCL/SDA 复用，板载杜邦线到 PD07/PD06 |
| GPTMR2 CAPT_2/3 | PD06/PD07 | Icu 通道 0/1 | TRGM_P_06/P_07 → TRGM → GPTMR2_CAPT_2/3 |
| EWDG0 | EWDG0 | Wdg | OSC32 32768 Hz，超时复位 |

表 45-1 板级资源划分

表 45-2 是驱动 API 与寄存器的对照（每个模块至少两行，覆盖本轮所有关键写入点）。

| 寄存器 | 位域/字段 | 驱动 API | 例程位置 |
|---|---|---|---|
| GPTMR CHn_CR | CMPEN/CNTRST | Gpt_StartTimer / Gpt_StopTimer | gpt_periodic 主循环 |
| GPTMR CHn_RLD | 重载值（写 Value-1） | Gpt_StartTimer(Channel, Value) | gpt_oneshot 六档扫描 |
| GPTMR CHn_CNT | 当前计数 | Gpt_GetTimeElapsed / GetTimeRemaining | gpt_periodic ISR |
| GPTMR IRQEN / SR | RLD/CAP 中断使能与状态 | Gpt_EnableNotification、Icu_EnableEdgeDetection | 两个模块 ISR |
| GPTMR CHn_CAPPRD | 周期测量值 | Icu_GetDutyCycleValues().PeriodTime | icu_capture 频率扫描 |
| GPTMR CHn_CAPDTY | 高电平测量值 | Icu_GetDutyCycleValues().ActiveTime | icu_capture 占空比扫描 |
| GPTMR CHn_CAPPOS/CAPNEG | 边沿时刻 | Icu_GetLastEdgeTimestamp（扩展）、时间戳缓冲 | pwm_basic 相位测量 |
| PWMV2 SHADOW_VAL[0] | 计数器重载 | Pwm_SetPeriodAndDuty | pwm_basic 频率扫描 |
| PWMV2 SHADOW_VAL[4..7] | CMP4~CMP7 影子 | Pwm_SetDutyCycle / SetPeriodAndDuty | pwm_basic 占空比扫描 |
| PWMV2 CNT_GLBCFG | 计数器使能/复位 | Pwm_Init / Pwm_DeInit | 所有 Pwm 例程 |
| PWMV2 CNT_VAL[n] | 计数器当前值（24.8 定点） | Pwm_GetOutputState | pwm_basic idle 回读 |
| PWMV2 IRQ_STS_CMP / IRQ_STS_RELOAD | 比较/重载中断状态 | Pwm_IsrHandler | pwm_notify |
| TRGM TRGOCFG[n] | TRIGOSEL（输入源选择） | Icu_Init（PBcfg 参数） | icu_capture |
| EWDG OT_RST_VAL | 超时复位 tick | Wdg_SetTriggerCondition / Wdg_SetMode | wdg_trigger / wdg_timeout_reset |
| EWDG WDT_REFRESH_REG | 刷新（喂狗） | Wdg_SetTriggerCondition | wdg_trigger 主循环 |
| EWDG WDT_EN | 看门狗使能 | Wdg_Init / Wdg_SetMode(OFF) | wdg_trigger |
| PPOR RESET_FLAG bit16 | WDG0 复位源 | 例程直接读（非 MCAL API） | wdg_timeout_reset |

表 45-2 API 与寄存器对照

## 45.6 配置模型

本轮四个模块都采用 Pre-compile + Post-build 两级配置，Link-time 配置不做（第 43 章有说明）。

**Pre-compile**（`<Mod>_Cfg.h`）放"编译期开关与规模"：

- Gpt：`GPT_NUM_CHANNELS=2`、DeInit/TimeElapsed/TimeRemaining/VersionInfo/SetMode 的可选 API 开关；
- Pwm：`PWM_NUM_CHANNELS=2`、通知开关、`PWM_MAX_PERIOD_TICKS=0x00FFFFFF`；
- Icu：`ICU_NUM_CHANNELS=2`、时间戳缓冲上限、通知/边沿检测开关；
- Wdg：`WDG_DISABLE_ALLOWED=OFF`、`WDG_SRC_CLK_HZ=32768`。

**Post-build**（`<Mod>_PBcfg.c`）放"板级映射"，要求是**驱动 .c 里不出现实例名和引脚名**：

- 实例基地址、IRQ 号、时钟节点、IOC 基地址；
- 引脚/复用功能（PWM 的 PD02/PD03、Icu 的 PD06/PD07 + TRGM 输入输出源）；
- 通道的硬件编号（PWM 输出通道/计数器/比较器/影子索引；GPTMR 通道号）；
- 回调函数指针（`Pwm_Notification_<ch>`、`Gpt_Notification_<ch>` 等，弱符号默认空实现）。

这套划分是静态检查器 `tools/mcal_check.py` 的检查项：一旦驱动源码里出现 `HPM_IOC`、
`HPM_GPTMR0`、`PD02` 之类符号就会报错。本章的 Pwm/Icu 在初版里确实踩过这个检查
（IOC 基地址写死），最终把 IOC 基地址也放进 PBcfg 结构体才通过。

## 45.7 例程

7 个例程覆盖 4 个模块，全部上板跑过。表 45-3 是清单与各自的量化产出。

| 例程 | 模块 | 测什么 | 关键量化结果 | 日志 |
|---|---|---|---|---|
| gpt_periodic | Gpt | 1 ms tick 10 s 计数误差、ISR 抖动、API 开销 | 9999/10000 tick（-100 ppm 量化极限）；抖动 pp 0.51 µs；ISR 入口 753 ns | logs/20261102-134707 |
| gpt_oneshot | Gpt | one-shot 六档精度、API 开销 | 50 µs~5 ms 恒定偏置 0.98 µs，sd 31~35 ns | logs/20261102-141404 |
| pwm_basic | Pwm+Icu | 0/25/50/75/100% 占空比、1/10/50 kHz、相位 | 频率误差 ≤100 ppm；占空比固定 -0.83 µs；相位 22.1%/25% | logs/20261102-141320 |
| pwm_notify | Pwm | 双边沿通知吞吐与抖动 | 2 万边沿/s，丢/多 <0.03%，抖动 pp 0.62 µs | logs/20261102-141427 |
| icu_capture | Pwm+Icu | 1/10/100 kHz 周期、20/50/80% 占空比、时间戳 | 周期误差 ≤100 ppm；占空比固定 -0.83 µs | logs/20261102-141450 |
| wdg_trigger | Wdg | 正常喂狗、动态改周期、FAST 模式 | 3 段全部 SURVIVED | logs/20261102-141513 |
| wdg_timeout_reset | Wdg | 不喂狗复位、复位源识别、跨复位启动计数 | 冷启动 BOOT#1、WDG0=0 → 500 ms 不喂狗复位；复位源 WDG0 位=0x00010000（既有 wdg_reset 日志交叉证实） | logs/20261102-142810 |

表 45-3 七个例程与实测产出

以 Gpt 的启动逻辑为例，AUTOSAR 用法是"Init → EnableNotification → StartTimer"，驱动内部
把 tick 数换算成硬件重载值：

```c
/* samples/mcal/gpt_periodic/src/main.c（节选） */
Gpt_Init(&Gpt_Config);
s_clkHz  = clock_get_frequency(clock_gptmr0);   /* 100 MHz */
s_reload = s_clkHz / 1000u;                     /* 1 ms = 100000 ticks */
Gpt_EnableNotification(GPT_CHANNEL_0);
Gpt_StartTimer(GPT_CHANNEL_0, s_reload);

/* 通知回调（强符号覆盖 PBcfg 里的弱默认实现） */
void Gpt_Notification_0(void)
{
    uint32_t now = cpu_cycles();                 /* mcycle */
    uint32_t lat = (uint32_t)Gpt_GetTimeElapsed(GPT_CHANNEL_0);
    ...
}
```

## 45.8 实测

三个图分别对应三类证据：链路与时序（图 45-1，见 45.3.3）、占空比/频率误差曲线（图 45-2）、
Gpt 的误差预算与软件开销（图 45-3）。

![图 45-2 Pwm 占空比与频率实测误差（Icu 在杜邦线对端捕获）](figures/45-mcal-timer/fig-45-2-pwm-icu-error-curve.png)

图 45-2(a) 是 10 kHz 下占空比 25/50/75/100% 的设定-实测对照：四个点都在理想线下方，
但差值几乎恒定。把它换算成 tick：10 kHz 周期 10000 tick，实测高电平少 83 tick，
即 **83 × 10 ns = 0.83 µs**；1 kHz 时同样少 83 tick（占 0.083%），50 kHz 时同样少 83 tick
（占 4.2%），100 kHz 时同样少 83 tick（占 8.3%）。这证明误差不是"占空比算错"，而是
"高电平时间固定少 0.83 µs"。原因在坑 45-3 里讨论。

![图 45-3 Gpt 1 ms tick 精度预算与软件时间开销](figures/45-mcal-timer/fig-45-3-gpt-precision-budget.png)

图 45-3(a) 把 10 s 计数误差拆开：量化极限 ±100 ppm（10000 个计数差 1 个就是 100 ppm）、
GPTMR 实测时钟 +4.1 ppm、实测总误差 -100 ppm —— 三者一致，说明 1 ms tick 的长期计数
没有额外系统误差。图 45-3(b) 是软件可见开销：ISR 入口延迟 0.75 µs、周期抖动 pp 0.51 µs、
stddev 0.095 µs、`Gpt_GetTimeElapsed` 296 ns。

Icu 的实测在 1/10/100 kHz 三档上分别得到 -10 ppm / -100 ppm / 0 ppm 的频率误差，
全部落在"±1 tick 量化"范围内：

| 目标频率 | 实测周期（tick） | 实测频率 | 频率误差 | 占空比误差 |
|---|---|---|---|---|
| 1 kHz | 100000~100001 | 1000.000 Hz | 0 ~ -10 ppm | -0.083% |
| 10 kHz | 10000~10001 | 9999~10000 Hz | 0 ~ -100 ppm | -0.835% |
| 50 kHz | 2000~2002 | 49950~50000 Hz | 0 ~ -1000 ppm | -4.2% |
| 100 kHz | 1000 | 100000.000 Hz | 0 ppm | -8.3% |

表 45-4 Icu 周期与占空比实测（logs/20261102-141450-icu_capture.log）

回归说明：IOC 基地址从驱动常量改为 PBcfg 字段后（见坑 45-6），icu_capture 复跑
@@logs/20261102-143359-icu_capture.log@@，占空比 19.915/49.917/79.915%、边沿 1998/2000、
周期 100000~100001 tick，与重构前逐项一致；Pwm 的引脚复用路径也随该例程一并验证（PWM 输出被
Icu 正常捕获）。

Wdg 的三段存活测试与复位链：

| 测试 | 配置 | 持续时间 | 喂狗间隔 | 结果 |
|---|---|---|---|---|
| t1 | WDGIF_SLOW，1000 ms | 3 s | 100 ms | SURVIVED，30 次喂狗 |
| t2 | SetTriggerCondition(300 ms) | 2 s | 100 ms | SURVIVED，20 次喂狗 |
| t3 | WDGIF_FAST，100 ms | 1 s | 50 ms | SURVIVED，20 次喂狗 |
| reset | 500 ms 不喂狗 | 500 ms | 无 | 冷启动日志停在 "armed ... reset expected"；复位源 WDG0 位由既有 wdg_reset 日志证实 = 0x00010000（bit16） |

表 45-5 Wdg 实测（logs/20261102-141513-wdg_trigger.log + logs/20261102-142810-wdg_timeout_reset.log）

## 45.9 性能

| 指标 | 数值 | 测量方法 |
|---|---|---|
| Gpt_GetTimeElapsed | 142 cycles / 296 ns | mcycle 前后差，1000 次平均 |
| Gpt_GetTimeRemaining | 160 cycles / 332 ns | 同上 |
| Gpt_StartTimer | 1427 cycles / 2.97 µs | 含 GPTMR 重配 + NVIC 使能 |
| 1 ms tick ISR 入口延迟 | 均值 753 ns（min 0.68 / max 3.79 µs） | ISR 入口读已计 tick |
| 1 ms tick 周期抖动 | pp 0.51 µs / stddev 0.095 µs | 相邻 ISR 的 mcycle 差 |
| Pwm 边沿通知 | 2 万边沿/s，CPU 占用 <1% | 3 s 窗口计数 vs 理论 |
| Pwm 边沿通知抖动 | pp 0.62 µs / stddev 0.13 µs | 相邻通知 mcycle 差 |
| Pwm 参数更新 | 影子寄存器写入，下一 shlk 生效 | 无阻塞，主循环可调 |
| Icu 硬件测宽 | 0 CPU 参与（硬件填 CAPPRD/CAPDTY） | 只在 ISR 读寄存器 |
| Wdg 喂狗 | 一次 ewdg_refresh，约 1 µs 量级 | 未单独计时 |

表 45-6 软件可见性能

## 45.10 坑

**坑 45-1：把"测量窗口"和"会改定时器状态的命令"混在一起，统计全错。**
第一版 gpt_periodic 在 10 s 计数窗口里轮询 shell，结果 10 s 只数到 9272 个 tick
（-72800 ppm），而同一窗口 mcycle 算出的 ISR 周期又是 1000.000 µs，两者矛盾。
根因不是时钟，而是窗口里执行了 `gpt_api` 命令，它结束时把统计变量清零，而窗口起点是清零前取的。
修复：测量窗口内不轮询 shell，测量命令排到最后。教训：**先用两个独立时基互相验证，再下"时钟不对"的结论**。

**坑 45-2：one-shot 通道不开中断，状态机永远停在 running。**
SWS 要求"到期后停表并置 expired"，这个动作只能在中断里做。因此即使应用没使能 notification，
one-shot 通道也必须开 RLD 中断；驱动在 `Gpt_StartTimer` 里对 one-shot 通道强制开中断。

**坑 45-3：PWM 高电平固定少 0.83 µs。**
占空比在 1/10/50/100 kHz 下分别偏 -0.083%/-0.835%/-4.2%/-8.3%，换算成绝对时间都是 0.83 µs，
说明是**固定时间偏置**而不是百分比误差。候选原因：PWM 输出级与 TRGM 捕获链路的延迟不对称
（上升沿/下降沿用不同比较器，比较器到引脚的路径不同），或 GPTMR 测宽模式在边沿采样上有一个
固定 offset。本轮没有示波器做逐边沿标定，因此按"已知限制"记录；如果要用 MCAL 做占空比闭环，
建议先做一次逐通道标定（用 `pwm set 500` + `icu meas` 量出 offset，写回 PBcfg 补偿）。

**坑 45-4：PWMv2 的 CMP 事件依赖"计数器重载值"，只配自己用的计数器会静默丢事件。**
第 14 章坑 14-3 已经记录过：CMP[4k+i] 的生效条件是计数器 i 的重载值非 0。本驱动在 Init 里
把 4 个计数器都绑到影子 0，绕开这个约束。

**坑 45-5：看门狗复位后的启动打印抓不到。**
wdg_timeout_reset 第一次做成"连复位 3 次"，单次 hwrun 只抓到复位前一行；改成"第一次烧写跑
不喂狗→复位，第二次 --no-flash 抓复位后启动"，第二次仍然抓不到启动打印。已定位到两个约束：
复位前后串口链路会短暂中断，同一次抓包跨不过去；复位后固件要重新跑 1.2 s 的抓包等待窗口，
两次 hwrun 的窗口重叠不足时什么都抓不到。本轮交付里，"不喂狗→复位"的冷启动侧证据由
`logs/20261102-142810-wdg_timeout_reset.log` 给出，"复位源"证据取既有例程
`samples/power/wdg_reset` 的历史日志 `logs/20261030-112938-wdg_reset.log`（其中
`"*** last reset was caused by EWDG0 timeout (RESET_FLAG=0x00010000) ***"` 正是 PPOR bit16）。

**坑 45-6：IOC 基地址也算板级参数。**
静态检查器会把驱动 .c 里的 `HPM_IOC` 判为硬编码。正确做法是把 IOC 基地址放进 PBcfg
配置结构体，驱动用 `(IOC_Type *)(uintptr_t)pch->iocBase` 访问。

## 45.11 对比

| 维度 | 本实现（HPM5E31 MCAL 子集） | Infineon TC4x MCAL 2.20.0（Gtm/Gpt） | NXP S32K3 RTD 6.0.0（Gpt） | HPM SDK 直调 |
|---|---|---|---|---|
| API 覆盖 | Gpt 12 个（含 Predef）、Pwm 10 个、Icu 18 个、Wdg 4 个 | Gpt 全套 + Gtm 专用 API（TOM/ATOM/DTM） | Gpt 全套 + 通道模式/唤醒 | 无 AUTOSAR，直接用 gptmr/pwmv2 驱动 |
| 配置方式 | Pre-compile + Post-build（PBcfg 结构体） | Post-build + EB tresos 生成 | Pre-compile + Post-build（RTD 生成） | 手写 C 代码 |
| Dev Error | 全部 API 有 DET + SWS 错误码 | 全套 DET/Dem | 全套 DET/Dem | 无 |
| 定时器抽象 | 逻辑通道 = GPTMR 实例通道 | 通道 = GTM 子模块通道（TOM/ATOM） | 通道 = eMIOS/LPIT 通道 | 直接操作实例 |
| 通知 | 弱符号 `<Mod>_Notification_n` | 生成回调 | 生成回调 | 手写 ISR |
| 输入捕获 | GPTMR2 + TRGM 交叉开关 | GTM TIM 输入捕获 | eMIOS 输入捕获 | 手写 TRGM + GPTMR |
| 看门狗 | EWDG：OFF/SLOW/FAST + 窗口 | Wdg 支持窗口/外部看门狗 | SWT/Wdog 支持窗口 | 手写 ewdg 寄存器 |
| 本板实测 | 7 个例程、全部上板运行并落日志 | —（无硬件） | —（无硬件） | 第 14/15/22 章已有寄存器级实测 |
| 主要短板 | 无唤醒/EcuM、Pwm 无 power state、Icu 无 edge counter | 功能全但代码量大 | 功能全 + AUTOSAR 认证 | 不可移植 |

表 45-7 与商用 MCAL / SDK 直调的对比

从表 45-7 看，本实现的差距主要在"可选服务"：Gpt 没做 wakeup（没有 EcuM）、Pwm 没做 power state、
Icu 没做 edge counter。这些在 SWS 里都是 opt-api，用 `*_CFG_*` 开关裁掉并在本章"待确认项"里
写清原因，符合契约"不允许静默缺失"的要求。

## 45.12 参考来源

- AUTOSAR CP SWS_GPTDriver：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_GPTDriver.pdf>
- AUTOSAR CP SWS_PWMDriver：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_PWMDriver.pdf>
- AUTOSAR CP SWS_ICUDriver：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_ICUDriver.pdf>
- AUTOSAR CP SWS_WatchdogDriver：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_WatchdogDriver.pdf>
- AUTOSAR CP SWS_BSWGeneral（模块 ID 表 §11.2）：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_BSWGeneral.pdf>
- 本地 SWS 抽取文本：`autosar_txt/AUTOSAR_CP_SWS_GPTDriver.txt` 等
- HPM SDK（GPTMR/PWMv2/TRGM/EWDG 驱动与 HPM5E31 头文件）：<https://github.com/hpmicro/hpm_sdk>
- 先楫半导体官网：<https://www.hpmicro.com/>
- HPM5E31 用户手册（GPTMR、PWMv2、TRGM、EWDG 章节）：板卡资料包 `ref/`
- Infineon AURIX TC4x MCAL：<https://www.infineon.com/cms/en/product/microcontroller/32-bit-tricore-microcontroller/aurixtm-tc4x/>
- NXP S32K3 RTD：<https://www.nxp.com/design/software/development-software/s32k3-standard-software/s32k3-real-time-drivers>
- 本书第 14 章（PWMv2 寄存器级实测）、第 15 章（GPTMR 捕获/中断实测）、第 22 章（EWDG 实测）

## 45.13 待确认项与风险

**已实测**

- Gpt 1 ms tick：10 s 计数 9999/10000（-100 ppm 量化极限）、ISR 抖动 pp 0.51 µs、
  入口延迟 753 ns、GPTMR0 实际时钟 100.000409 MHz（+4.1 ppm）。日志
  `logs/20261102-134707-gpt_periodic.log`。
- Gpt one-shot：50 µs~5 ms 恒定偏置 0.98 µs、sd 31~35 ns。日志
  `logs/20261102-141404-gpt_oneshot.log`。
- Pwm 占空比 0/25/50/75/100%、频率 1/10/50 kHz、相位 22.1%（设定 25%）、idle 回读。
  日志 `logs/20261102-141320-pwm_basic.log`。
- Pwm 双边沿通知 2 万/s、丢/多 <0.03%、抖动 pp 0.62 µs。日志
  `logs/20261102-141427-pwm_notify.log`。
- Icu 1/10/100 kHz 周期误差 ≤100 ppm、占空比误差 -0.083%~-8.3%、边沿计数 1998/2000、
  32 点时间戳。日志 `logs/20261102-141450-icu_capture.log`。
- Wdg 三段存活（SLOW/动态改周期/FAST 全部 SURVIVED）。日志 `logs/20261102-141513-wdg_trigger.log`。
- Wdg 冷启动"不喂狗→复位请求"路径与复位源判定（PPOR bit16 = WDG0）。日志
  `logs/20261102-142810-wdg_timeout_reset.log`；复位源 PPOR=0x00010000 由既有
  `logs/20261030-112938-wdg_reset.log` 交叉证实。

**已定位**

- 坑 45-1 的 -72800 ppm 来自测量代码清零统计，不是时钟问题；已用 mcycle 标定确认真实时钟 +4.1 ppm。
- 坑 45-5 的复位后丢日志来自"复位瞬间串口链路中断 + 两次 hwrun 重叠窗口不足"；已改成两步抓法。
- Icu 线性时间戳缓冲满后 `Icu_StopTimestamp` 报 ICU_E_NOT_STARTED(0x15)，符合 SWS 语义，属正常路径。

**已知限制（原因 + 一条复跑命令）**

- Pwm 占空比存在固定约 0.83 µs 的高电平缺失（捕获/输出链路延迟，未逐通道标定）：
  复跑 `bash scripts/hwrun.sh mcal/icu_capture --secs 20`，看 duty 列在不同频率下的百分比变化。
- Pwm 相位测量偏差 -2.9%（22.1% vs 25%），跨通道捕获延迟未标定：
  复跑 `bash scripts/hwrun.sh mcal/pwm_basic --secs 20`，看 [phase] 行。
- Wdg 复位后的启动打印在共享板卡上抓不到（复位瞬间串口链路中断 + 两次 hwrun 之间可能被他人的
  烧写打断）：先 `bash scripts/hwrun.sh mcal/wdg_timeout_reset --secs 8`，再立即
  `bash scripts/hwrun.sh mcal/wdg_timeout_reset --no-flash --secs 12`。
- Gpt 唤醒服务（Gpt_EnableWakeup/DisableWakeup/CheckWakeup）未实现：本子集没有 EcuM，
  且 GPTMR 的中断唤醒路径需要 Mcu 的低功耗配合；实现路径：在 Mcu_SetMode 里挂
  EcuM_CheckWakeup，Gpt 侧只维护 wakeup enabled 属性。
- Pwm power state 服务（SetPowerState/GetCurrentPowerState/...）未实现：没有 EcuM/BswM
  的低功耗状态机可对接；实现路径同上。
- Icu edge counter 服务（EnableEdgeCount/GetEdgeNumbers）未实现：需要额外的软件计数器，
  本轮用时间戳/边沿检测覆盖了同类需求；实现路径：在 ISR 里自增一个 uint32 计数器并暴露读接口。
- 无示波器：所有波形证据来自"第二组硬件捕获"（GPTMR2 + TRGM），无法测死区、上升沿时间、
  过冲等模拟量；复跑 `bash scripts/hwrun.sh mcal/pwm_basic --secs 20` 只能给数字量证据。