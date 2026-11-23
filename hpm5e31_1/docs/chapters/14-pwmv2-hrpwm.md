# 第 14 章 PWMv2 与 HRPWM

## 14.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列，PWMv2）/ hpm5e31_1 |
| 对应例程 | `samples/pwm/pwm_basic/`、`samples/pwm/pwm_deadtime_fault/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高：机制来自 UM Rev0.4 §37 与勘误 Rev0.1；频率/占空比/抖动为上板实测（`logs/`） |

## 14.2 本章导览

- **学到什么**：PWMv2 的四个子模块与"计数器 + 比较值 a/b/c/d"波形生成规则；
  24 个比较值如何分配到子模块；死区、互补、强制输出、故障保护、HRPWM 100 ps；
  以及**没有示波器时如何量出 PWM 的真实频率与占空比**。
- **前置章节**：第 02 章（构建/烧写）、第 03 章（letter-shell）、第 15 章（用 GPTMR 做时基）。
- **特别注意**：HPM5E00 是 **PWMv2**，寄存器和 HPM6750/6300 的 PWMv1 完全不同，
  网上大量 PWMv1 的代码不能直接套用（见 §14.10 对照表）。

## 14.3 概述与典型应用

PWMv2 是 HPM5E00 面向电机与电源的波形引擎：每个实例 4 个生成子模块、8 路输出、
24 个比较值、28 个影子寄存器、16 个通用计算单元，并用延迟链把边沿精度做到 100 ps。

| 应用 | 用到的特性 | 本章位置 |
|---|---|---|
| 三相电机 FOC | 互补输出 + 死区 + 故障保护 + 用比较事件触发 ADC | §14.4.5、§14.4.7 |
| 数字电源（Buck/LLC） | HRPWM 高分辨率占空比、Burst | §14.4.6、§14.8 |
| 步进/伺服 | 多计数器同步 + 移相 | §14.4.4 |
| 照明/加热 | 低频率 + 强制输出 | §14.4.7 |
| 功率级保护 | 异步故障输入（时钟丢失仍有效） | §14.4.7 |

表 14-1：PWMv2 典型应用

### 14.3.1 关键特性（UM §37.1, p.607；DS p.12）

| 特性 | 数值/说明 |
|---|---|
| 实例数 | 2（PWM0 @0xF0420000、PWM1 @0xF0424000），共 16 路 |
| 生成子模块 | 每实例 4 个，各含 1 个 32 位计数器（24 位整数 + 8 位小数） |
| 输出 | 每实例 8 路，可 8 路独立或 4 对互补 |
| 比较值 | 24 个：CMP0~15 固定分配到 4 个子模块，CMP16~23 可自由映射 |
| 影子寄存器 | 28 个（SHADOW_VAL[0..27]） |
| 通用计算单元 | 16 个，做 `D·pD + S + T·pT` 并限幅 |
| DAC 输入 | 4 个，可来自片内其它模块 |
| 触发输入/输出 | 各 8 路（经 TRGM） |
| 死区 | `PWM_DEAD[n][DEAD_AREA]`，24 位（高 16 位整数 + 低 8 位小数），最小 0x200 |
| 高分辨率 | HRPWM 100 ps（依赖 TRGM 延迟链校准） |
| 故障保护 | 每子模块独立：同步故障（固定触发输入）+ 异步故障（16 个 pad 任选） |

表 14-2：PWMv2 关键特性

## 14.4 工作原理

![图 14-1 PWMv2 结构框图](figures/14/fig-14-1-pwmv2-block.png)

### 14.4.1 时钟与计数器

- PWM0/PWM1 的时钟来自 `clock_pwm0`/`clock_pwm1`。**本板实测 `clock_pwm0 = 200 MHz`**
  （`boards/hpm5e31_1/clock.c:59` 把它加进 clock group 0；日志里 `PWM0: clk=200000000 Hz`）。
- 计数器从 0 单调向上计数，到 `CNT_RELOAD_WORK` 产生重载事件并回 0（UM §37.4.3, p.608）。
- **频率公式**：`f_pwm = f_pwm_clk / T`，其中 T 就是重载值。200 MHz 下
  `T = 20000` → 10 kHz；`T = 200000` → 1 kHz。
- 计数器当前值可读 `CNT<n>_VAL`，高 24 位是整数、低 8 位是小数（HRPWM 用）。

### 14.4.2 影子寄存器与更新时机

PWM 波形由"工作寄存器"决定，软件写的是"影子寄存器"（UM §37.4.1, p.608）。
影子值什么时候生效由各处的 `*_UPDATE_TIME` 决定：

| 更新时机 | 编码 | 典型用途 |
|---|---|---|
| 影子锁事件后 | 0b00 | 一次性批量生效（本板例程用这个改重载） |
| 比较点匹配时 | 0b01 | 与另一个通道对齐更新 |
| 计数器重载时 | 0b10 | 占空比逐周期更新（电机 FOC 常用） |
| 触发输入上升沿 | 0b11 | 外部事件同步更新（注意勘误 E00038） |

表 14-3：影子寄存器更新时机（UM §37.4.4 p.609、§37.4.8 p.613）

**为什么必须解锁**：写影子寄存器前要 `pwmv2_shadow_register_unlock()`，写完
`pwmv2_shadow_register_lock()`；锁定动作本身就是"让影子值生效"的事件（0b00 模式）。

### 14.4.3 PWM 波形生成：a/b/c/d 四个比较值

UM §37.4.5（p.610）用 a/b/c/d 描述一个子模块的 4 个比较值，输出 X 由 a、b 决定，
输出 Y 由 c、d 决定。**最常用的情形（0 ≤ a < b ≤ T）**：

```text
a <= CNT < b   时 X 输出高电平，其余时刻低电平
=> 占空比 = (b - a) / T
```

其余三种情形的完整规则（b>T 等）见 UM §37.4.5 的图 34~36；本板例程只用第一种。

**输出与比较值的固定映射**（UM §37.4.8, p.613）：

| 输出 | 计数器 | 比较值 a,b（X） | 比较值 c,d（Y） |
|---|---|---|---|
| P0 / P1 | 计数器 0 | CMP0 / CMP1 | CMP2 / CMP3 |
| P2 / P3 | 计数器 1 | CMP4 / CMP5 | CMP6 / CMP7 |
| P4 / P5 | 计数器 2 | CMP8 / CMP9 | CMP10 / CMP11 |
| P6 / P7 | 计数器 3 | CMP12 / CMP13 | CMP14 / CMP15 |

表 14-4：输出通道与计数器/比较值的固定对应关系

> 表 14-4 是本章最容易踩坑的地方：想用 P6/P7 就必须配计数器 3 和 CMP12/13，
> 配成 CMP0/1 是没有输出的。

### 14.4.4 边沿对齐、中心对齐与移相

- **边沿对齐**：a = 0，b = 占空比计数 → 输出在周期起点上升。
- **中心对齐**：a = (T - duty)/2，b = (T + duty)/2 → 输出在周期中点对称，谐波更低，
  电机 FOC 常用（需要硬件支持在"计数器过零/中点"触发 ADC，见 §14.4.7）。
- **移相**：4 个计数器可各自设不同初值/相位（官方例程用通用计算单元在触发时改计数），
  多相交错并联电源用它降低输入纹波。

![图 14-2 边沿对齐/中心对齐/死区/移相波形](figures/14/fig-14-2-waveforms.png)

### 14.4.5 互补输出与死区

- `PWM_CFG1[n][PAIR_MODE]`=1 时，同一子模块的 X/Y 变成一对互补输出（UM §37.4.7, p.612）。
- 死区寄存器 `PWM_DEAD[0]` 推迟输出 0 的上升沿，`PWM_DEAD[1]` 推迟输出 1 的上升沿；
  位域 24 位 = **高 16 位整数 + 低 8 位小数**，**最小 2 个时钟 = 0x200**（p.613）。

```text
需要的死区时间 = 功率管关断时间 + 驱动传输延迟 + 余量（典型 0.5~2 us）
200 MHz 下：dead_ticks = t_dead × 200e6
例：0.5 us -> 100 tick -> DEAD_AREA = 100 << 8 = 0x6400
```

### 14.4.6 HRPWM 100 ps

计数器的低 8 位是"小数计数"，配合 TRGM 里的延迟链把一个时钟周期再细分 256 份：
200 MHz（5 ns）÷ 256 ≈ **19.5 ps** 的量化步进，规格书上写 100 ps 是保守值（DS p.12）。
使用前必须用 `trgm_pwmv2_calibrate_delay_chain()` 校准，温度变化超过约 5 ℃ 要重校
（官方 `pwmv2/hrpwm_calibrate` 示例用 TSNS 触发重校）。

![图 14-3 频率-占空比分辨率关系](figures/14/fig-14-3-freq-resolution.png)

### 14.4.7 强制输出、故障保护与触发

| 功能 | 关键寄存器 | 说明 |
|---|---|---|
| 强制输出 | `FORCE_MODE`、`GLB_CTRL[SW_FORCE]`、`PWM_CFG1[n][FORCE_TIME]` | 2 位/路：00=强0、01=强1、10=HiZ、11=不强制（UM §37.4.10, p.615） |
| 同步故障 | `PWM_CFG0[n][FAULT_EN_SYNC]` | **输出 0/1 固定用触发输入 0，2/3 用 1，4/5 用 2，6/7 用 3**（p.616） |
| 异步故障 | `PWM_CFG3[n][ASYNC_FAULT_SEL]` 等 | 16 个异步故障 pad 任选，时钟丢失仍有效（p.617） |
| 故障后状态 | `PWM_CFG0[n][FAULT_MODE]` | 00=强0、01=强1、1x=HiZ（p.617） |
| 故障恢复 | `PWM_CFG1[n][FAULT_REC_TIME]` | 立即 / 重载后 / 触发输入 / 软件清（p.617） |
| 触发输出 | `TRIGGER_CFG[0..7][TRIGGER_OUT_SEL]` | 选任一比较值作为 TRGO 源 |
| 触发输入 | 8 路，经 TRGM | 启动、强制、故障、更新 |

表 14-5：强制输出、故障保护与触发

## 14.5 关键寄存器与驱动 API 对照

| 功能 | 寄存器 | 位域 | 驱动 API | 例程 |
|---|---|---|---|---|
| 影子值写入 | `SHADOW_VAL[0..27]` | value[31:8]/小数[7:0] | `pwmv2_set_shadow_val()` | `pwm_basic` |
| 影子锁 | `WORK_CTRL1` | `SHADOW_LOCK` | `pwmv2_shadow_register_lock()` | `pwm_basic` |
| 计数器重载 | `CNT_RELOAD_WORK[n]` | 32 位 | `pwmv2_counter_select_data_offset_from_shadow_value()` | `pwm_basic` |
| 重载更新时机 | `CNT_CFG0[n]` | `RLD_UPDATE_TIME` | `pwmv2_set_reload_update_time()` | `pwm_basic` |
| 比较值来源 | `CMP_CFG[n]` | `CMP_IN_SEL` | `pwmv2_select_cmp_source()` | `pwm_basic` |
| 比较值更新时机 | `CMP_CFG[n]` | `CMP_UPDATE_TIME` | `pwmv2_cmp_update_trig_time()` | 官方 `pwmv2/pwm_output` |
| 四比较器模式 | `PWM_CFG0[n]` | `TRIG_SEL4` | `pwmv2_enable/disable_four_cmp()` | `pwm_basic` |
| 互补模式 | `PWM_CFG1[n]` | `PAIR_MODE` | `pwmv2_enable_pair_mode()` | `pwm_basic` |
| 死区 | `PWM_DEAD[n]` | `DEAD_AREA` | `pwmv2_set_dead_area()` | `pwm_deadtime_fault` |
| 通道输出使能 | `PWM_CFG0[n]` | 输出使能 | `pwmv2_channel_enable_output()` | `pwm_basic` |
| 计数器启停 | `CNT_GLBCFG` | `TIME_ENABLE/TIME_RESET` | `pwmv2_enable_counter()`、`pwmv2_start_pwm_output()` | `pwm_basic` |
| 触发输出源 | `TRIGGER_CFG[n]` | `TRIGGER_OUT_SEL` | `pwmv2_set_trigout_cmp_index()` | `pwm_basic` |
| 比较中断 | `IRQ_STS_CMP`/`IRQ_EN_CMP` | 位 n 对应 CMPn | `pwmv2_enable_cmp_irq()` | `pwm_basic` |
| 重载中断 | `IRQ_STS_RELOAD`/`IRQ_EN_RELOAD` | 位 n 对应计数器 n | `pwmv2_enable_reload_irq()` | `pwm_basic` |
| 故障配置 | `PWM_CFG0/1/3` | `FAULT_*` | `pwmv2_enable_async_fault()` 等 | `pwm_deadtime_fault` |
| 强制输出 | `FORCE_MODE`、`GLB_CTRL` | `FORCE_MODE[15:0]` | `pwmv2_force_output()` | 官方 `pwmv2/pwm_output` |
| HRPWM 使能 | `GLB_CTRL` | `HR_PWM_EN` | `pwmv2_enable_hrpwm()` | 官方 `pwmv2/hrpwm_calibrate` |

表 14-6：PWMv2 寄存器与驱动 API 对照

## 14.6 例程逐条讲解

### 14.6.1 pwm_basic：频率/占空比/互补 + 无示波器实测

| 项 | 内容 |
|---|---|
| 路径 | `samples/pwm/pwm_basic/` |
| 覆盖特性 | 影子寄存器、计数器 3、CMP12/13、PAIR_MODE 互补、P6/P7 引脚输出、比较/重载中断、实测 |
| 关键配置 | `f_pwm_clk = 200 MHz`；默认 `T = 20000`（10 kHz）、`b = 10000`（50%）、`a = 0` |
| 引脚 | P6 = PD06、P7 = PD07（`boards/hpm5e31_1/pins/pinmux_timer.c`） |
| 实测现象 | 串口每 2 s 打印实测周期/频率/占空比/抖动/误差 |
| 实测日志 | `logs/20261030-114021-pwm_basic.log` |

关键代码（节选）：先把 4 个计数器的重载值统一，再选比较值来源，最后锁影子寄存器：

```c
pwmv2_shadow_register_unlock(APP_PWM);
pwmv2_set_shadow_val(APP_PWM, SHADOW_RLD, reload, 0, false);   /* 周期 T */
pwmv2_set_shadow_val(APP_PWM, SHADOW_A, 0, 0, false);          /* a = 上升沿 */
pwmv2_set_shadow_val(APP_PWM, SHADOW_B, b, 0, false);          /* b = 下降沿 */
for (i = pwm_counter_0; i <= pwm_counter_3; i++) {
    pwmv2_counter_select_data_offset_from_shadow_value(APP_PWM, i, SHADOW_RLD);
    pwmv2_set_reload_update_time(APP_PWM, i, pwm_reload_update_on_shlk);
    pwmv2_counter_burst_disable(APP_PWM, i);
}
pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(12), cmp_value_from_shadow_val, SHADOW_A);
pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(13), cmp_value_from_shadow_val, SHADOW_B);
pwmv2_shadow_register_lock(APP_PWM);
```

**为什么要把 4 个计数器的重载值都写成 T**：UM §37.4.8（p.613）规定，
比较值 `cmp[k*4+i]` 只有在"不大于计数器 i 的重载值"时才能产生触发输出/比较中断。
CMP12 受计数器 0 的重载值约束、CMP13 受计数器 1 约束；如果只配计数器 3，
计数器 0/1 的重载值是 0，CMP12/13 的事件会被**静默丢弃**（实测现象：捕获通道永远收不到
下降沿）。这是本章最重要的坑，见 §14.9 坑 14-2。

### 14.6.2 pwm_deadtime_fault：死区与故障保护配置

| 项 | 内容 |
|---|---|
| 路径 | `samples/pwm/pwm_deadtime_fault/` |
| 覆盖特性 | PAIR_MODE 互补 + `DEAD_AREA` 死区 + 同步故障（触发输入 3）+ 故障后 HiZ/强 0 |
| 关键配置 | 死区默认 100 tick（200 MHz 下 500 ns，`DEAD_AREA = 100 << 8`）；串口可改 |
| 实测现象 | 打印回读的 `PWM_DEAD`/`PWM_CFG0` 寄存器值，并演示注入/清除故障后的状态位 |
| 实测日志 | `logs/20261030-114436-pwm_deadtime_fault.log`（见 §14.12 待确认 14-A：死区波形本身需示波器） |

```c
/* 互补 + 死区：P6/P7 一对，死区 500 ns */
pwmv2_enable_pair_mode(PWM, pwm_channel_6);
pwmv2_set_dead_area(PWM, pwm_channel_6, dead_ticks);   /* 内部左移 8 位写 DEAD_AREA */
/* 同步故障：输出 6/7 的同步故障输入固定为触发输入 3 (UM §37.4.11, p.616) */
pwmv2_enable_sync_fault(PWM, pwm_channel_6);
pwmv2_set_fault_mode(PWM, pwm_channel_6, pwm_fault_output_high_z);
pwmv2_set_fault_recovery_time(PWM, pwm_channel_6, pwm_fault_recovery_immediately);
```

实测回读（`logs/20261030-114436-pwm_deadtime_fault.log`）：

```text
letter:/$ dead 400
  死区: ticks=400 -> DEAD_AREA=0x00019000 (高16位整数=400, 低8位小数=0)
        换算: 2000 ns = 2.000 us @ 200000000 Hz
  故障: 同步故障输入=TRIG_IN_3, 当前注入=0, IRQ_STS_FAULT=0x00000000
        PWM_CFG0[6]=0x00000010 (FAULT_EN_SYNC/FAULT_MODE 位域)

letter:/$ fault 1
同步故障注入=1 -> IRQ_STS_FAULT=0x00000000, PWM_CFG0[6]=0x00000010

letter:/$ fault 0
同步故障注入=0 -> IRQ_STS_FAULT=0x000000c0, PWM_CFG0[6]=0x00000010
```

**读法**：`dead 400` → `DEAD_AREA = 400 << 8 = 0x19000`，换算 2.000 us，说明
"ticks → 24 位寄存器"的换算正确；`fault 0` 之后 `IRQ_STS_FAULT` 变成 **0xc0**，
即 bit6、bit7 置位——正好是本示例使能了同步故障的**输出 6 和输出 7**，
说明故障通路确实被触发过（该标志在故障撤销后才可见，见坑 14-9）。

> 死区/故障的**波形**必须用示波器看；本章只做到"寄存器配置正确 + 状态位符合预期"，
> 波形级验证列入 §14.12 待确认项 14-A。

## 14.7 上板实测数据

### 14.7.1 1 kHz ~ 100 kHz：软件中断法

测量方法：`samples/pwm/pwm_basic` 用 GPTMR0 通道 3（100 MHz，10 ns/拍）做时间基，
在 PWM 的"计数器 3 重载中断"与"CMP13 比较中断"里各读一次时间戳，
统计周期与高电平时间。命令：`bash scripts/hwrun.sh pwm/pwm_basic --secs 16 --send "psweep"`。

| 目标频率 | 目标占空比 | 实测周期 | 实测频率 | 实测占空比 | 频率误差 | 占空比误差 |
|---|---|---|---|---|---|---|
| 1 kHz | 25.0% | 1000.005 us | 1000.0 Hz | 25.00% | -4.9 ppm | -0.000% |
| 10 kHz | 50.0% | 100.005 us | 9999.5 Hz | 50.04% | -50.0 ppm | +0.035% |
| 20 kHz | 75.0% | 50.005 us | 19998.0 Hz | 75.04% | -100.0 ppm | +0.037% |
| 50 kHz | 10.0% | 20.005 us | 49987.5 Hz | 10.00% | -249.9 ppm | -0.005% |
| 100 kHz | 50.0% | 10.005 us | 99950.0 Hz | 50.35% | -499.7 ppm | +0.353% |

表 14-7：PWMv2 频率/占空比实测（`logs/20261030-114021-pwm_basic.log`）

10 kHz 这一行的原始输出：

```text
[meas] reload=12039 cmp=12040  period=100.005 us (9999.5 Hz)  high=50.038 us  duty=50.04%
       period jitter: min=99.900 max=100.090 pp=0.190 us
       high  jitter: pp=0.170 us (=17.0 tick)
       target: 10000 Hz / 500 permille -> freq err -50.0 ppm, duty err +0.035%
```

**读数解读**：

- 频率误差 -50 ppm 来自时间戳量化：100 us 对应 10000 个 10 ns tick，
  平均后残留 ±0.5 tick → ±50 ppm（与实测完全一致），**不是 PWM 本身的误差**。
- 占空比误差 +0.035%（50.04% vs 50.00%）相当于 3.5 tick，同样是量化与中断延迟的合成。
- 周期抖动峰峰值 0.19 us（19 tick）：这是**中断响应抖动**，与第 15 章 1 kHz 中断抖动
  （1.01 us）同量级偏小，因为 PWM ISR 比 GPTMR 示例的 ISR 更短。
- 100 kHz/50% 行的占空比误差 +0.353% 明显变大：T 只有 2000 tick，高电平 1000 tick，
  同样的中断延迟占比被放大；也说明**频率越高越不该用软件测占空比**。

### 14.7.2 100 kHz 以上的占空比误差

把 `psweep` 扩到 200 kHz / 500 kHz 之后（`psweep2`，2026-11-02 实测），问题就不是“准不准”而是“还用不用软件测”了：

| 目标频率 | T (tick) | 实测周期 | 实测频率 | 频率误差 | 实测占空比 | 占空比误差 | 折合高电平误差 |
|---|---|---|---|---|---|---|---|
| 100 kHz | 2000 | 10.005 us | 99950.0 Hz | −500 ppm | 50.43% | +0.43% | +43 ns |
| 200 kHz | 1000 | 5.005 us | 199800.2 Hz | −999 ppm | 50.45% | +0.45% | +23 ns |
| 500 kHz | 400 | 2.005 us | 498753.1 Hz | −2494 ppm | 51.49% | +1.49% | +30 ns |

表 14-9：高频段频率/占空比实测（`psweep2`，`logs/20261102-124411-pwm_basic.log`）

两个误差的来路不一样，读表时要分开看：

- **频率误差**随频率线性变差：−500 / −999 / −2494 ppm，正好等于 0.5 tick 的量化误差除以周期 tick 数
（100 kHz 周期 1000 tick → 0.5/1000 = 500 ppm）。这条跟 PWM 无关，是 10 ns 时基的分辨率。
- **占空比误差**在 100k/200k 两档都是 0.43~0.45%，折合 23~43 ns，量级与“中断响应延迟”一致；
到 500 kHz 时 T 只剩 400 tick，同样 30 ns 的固定延迟占比就跳到 1.49%。换句话说，占空比的绝对误差没变，是被短周期放大了。

结论有两条：一是 **100 kHz 以上不要用软件中断测占空比**，误差已经进入百分点级，
要测就上示波器/逻辑分析仪，或者用 PWM 自己的 CAPPRD 类硬件测量（GPTMR 捕获方案在坑 14-3 里已证明窄脉冲会漏采）；
二是 500 kHz 时 T=400 tick，占空比的最小步进已经是 1/400 = 0.25%，所谓“误差”里也有分辨率本身的贡献。


### 14.7.3 勘误 E00038 的实测尝试（CMP0/CMP4 同一触发组）

勘误说：外部触发更新比较值时，同一 `CMP_CFG0` 组（CMP0/4/8/12）的更新可能丢失或异常，
而且只有 `CMP_CFG0` 的 `CMP_TRIG_SEL` 生效。`pwm_basic` 里加了 `perr <n> <mode>` 直接做这个实验：

| 步骤 | 配置 |
|---|---|
| 触发源 | PWM0 自身的 `TRGO_6`（CMP15 事件，每周期一次）经 TRGM 送 `PWM0 TRIG_IN_0` |
| 被测对象 | CMP0（计数器 0）与 CMP4（计数器 1），影子 20/21 交替写 `T/4↔3T/4`、`T/3↔2T/3` |
| 更新时机 | mode 0：`CMP_UPDATE_TIME=trigmux`（踩中勘误）；mode 1：`=shlk`（影子锁，规避法）；mode 2：CMP4 的 `TRIG_SEL` 故意写 1，CMP0 写 0（验证“只有 CMP_CFG0 生效”） |
| 判据 | CMPn 中断里读 `CNT_VAL`，与本次写入的比较值比，偏差 > 3000 tick 记为异常 |

表 14-10：E00038 试验配置（`perr`，首轮 20 次×3 模式，复跑 100 次×3 模式）

实测（`logs/20261102-124644-pwm_basic.log`）：三种模式都报 `CMP0 异常 20/20、CMP4 异常 20/20`，
看起来是“勘误必然复现”，但把读数还原一下就知道判据写错了——`CNT_VAL` 是 **24.8 定点**，
整数在 bit31:8，读回来要右移 8 位才是 tick：

```text
mode 0: 最后 v0=1303552 -> 5092 tick (期望 5000, 差 92)  v4=1731072 -> 6762 tick (期望 6666, 差 96)
mode 1: 最后 v0=1303552 -> 5092 tick                   v4=1730304 -> 6759 tick (期望 6666, 差 93)
mode 2: 最后 v0=1303552 -> 5092 tick                   v4=1731328 -> 6763 tick (期望 6666, 差 97)
```

也就是说：CMP0 与 CMP4 **都**更新成了新的比较值，偏差 92~97 tick = 460~485 ns，正好是中断响应延迟的量级，
不是“更新丢失”。`mode 2` 里 CMP4 的 `TRIG_SEL` 写 1 却仍然跟着触发 0 更新，这一点和勘误描述的
“同组共用 CMP_CFG0 的选择”一致。**本轮数据不支持 E00038 在本板复现**；
不过逐次统计的判据本身有 bug：`CNT_VAL` 是 24.8 定点，第一版没移位、第二版又多移了一次（`logs/20261102-125159-pwm_basic.log` 里 v0=19 = 1303552>>16，所以还是 20/20）。两处都改完并重新编译，严格的逐次统计要等重跑 `perr 100 0;perr 100 1;perr 100 2`。
试验的触发源是 `TRIG_IN_0`（源为 PWM0 自己的 TRGO，片内，不用外部引脚），不是外部信号。
**复跑结果（判据修正后，`logs/20261102-125859-pwm_basic.log`）**：三种模式全部为
`CMP0 异常 0/100、CMP4 异常 0/100、无中断 0/200` —— 100 次交替更新一次不差。
所以本板在该用例下 **E00038 的“更新丢失”不复现**，但 mode 2 确实证实了“同组共用 `CMP_CFG0` 的
`TRIG_SEL`”这一半结论。工程建议不变：同组比较值统一 `CMP_TRIG_SEL`，或 `CMP_UPDATE_TIME`
不用 `0b011`；真遇到“偶发不更新”，优先怀疑中断延迟/影子寄存器写时序，而不是直接归因勘误。

第 26 章勘误表引用本节数据。


## 14.8 性能与调优

1. **频率-分辨率取舍**：`f_pwm = 200 MHz / T`，T 最大 24 位（16 777 215），
   因此最低频率约 11.9 Hz；占空比分辨率是 1/T。10 kHz 时 T = 20000，
   占空比步进 0.005%；100 kHz 时 T = 2000，步进 0.05%。
2. **需要更细的占空比**：开 HRPWM，把低 8 位小数用上，等效步进再细分 256 倍
   （10 kHz 时约 19.5 ps ≈ 0.00002%）。
3. **不要用 CPU 改占空比追高频**：200 kHz 以上时每周期都进中断会吃满 CPU；
   改用 DMA 在重载点搬影子寄存器，或直接用通用计算单元由硬件生成占空比序列。
4. **多通道同频**：同一个计数器的 4 个比较值（a/b/c/d）可驱动 2 路输出；
   要 4 路以上就开多个计数器并用同步启动（`pwmv2_start_pwm_output_sync`）。
5. **边界占空比**：0% 用 a=b（或 b=0），100% 用 b>T 的规则（UM §37.4.5 p.611），
   不要用"a=T、b=T"这种边界写法，容易在重载瞬间出现窄毛刺。

## 14.9 坑与注意事项

1. **现象**：配了 CMP0/CMP1，但 P6/P7 没有输出。
   **原因**：输出 6/7 固定对应计数器 3 与 CMP12~15（表 14-4）。
   **解决**：按表 14-4 配对，或改用 P0/P1。
2. **现象**：CMP13 的触发输出/比较中断完全不来（捕获通道永远收不到下降沿），
   但 CMP12 正常。
   **原因**：UM §37.4.8（p.613）的交叉周期限制——`cmp[k*4+i]` 必须 ≤ 计数器 i 的
   重载值才能产生事件。CMP13 = k=3,i=1，受计数器 1 的 `CNT_RELOAD_WORK[1]` 约束；
   只配计数器 3 时它是 0，事件被丢弃。
   **解决**：把 4 个计数器的重载值都配成同一个 T（本例做法），或改用 CMP16~23
   自由映射比较值。
3. **现象**：把 PWM 的 TRGO 经 TRGM 接到 GPTMR 捕获，实测周期忽大忽小、事件数严重偏少。
   **原因**：TRGO 是 **1 个 PWM 时钟周期**宽的窄脉冲（200 MHz 下 5 ns），
   GPTMR 时钟 100 MHz（10 ns/拍），跨时钟域采样大量漏采。
   实测：50 kHz 下 2 s 只捕获 3533 个周期，理论 10 万个。
   **解决**：改用 PWM 中断 + GPTMR 自由运行时基打时间戳（`pwm_basic` 的做法），
   或用引脚 + 逻辑分析仪。
4. **现象**：写了 `PWM_DEAD` 但死区没变化，甚至输出异常。
   **原因**：`DEAD_AREA` 是 24 位且**低 8 位是小数**，最小合法值 0x200（2 个时钟）；
   直接写"时钟数"会小 256 倍（UM §37.4.7, p.613）。
   **解决**：用 `pwmv2_set_dead_area(chn, ticks)`（内部会左移 8 位），或自己写 `ticks << 8`。
5. **现象**：用外部触发更新比较值，只有 CMP0~3 生效，其它比较值不动。
   **原因**：勘误 **E00038**（HPM5E00 Errata Rev0.1, p.2）：`CMP_UPDATE_TIME=0b011` 时
   只有 `CMP_CFG0~3` 的 `CMP_TRIG_SEL` 有效，且 CMP0/4/8/12 共用 CMP_CFG0 的选择……
   **解决**：让同组比较值共享同一 `CMP_TRIG_SEL`，或改用 0b011 以外的更新时机。
6. **现象**：PWM0 输出脚配不出来 / 与 CAN 冲突。
   **原因**：本板 PWM0 的 P0~P5 引脚本板落在 PD00~PD05，而 `board.h` 把
   PD00/PD01 给 CAN0、PD04/PD05 给 CAN1（`README.md` 的硬件表）。
   **解决**：PWM 用 P6/P7（PD06/PD07），或确认 CAN 未使用后再复用（`pinmux_timer.c` 注释）。
7. **现象**：`pwmv2_enable_cmp_irq()` 返回失败。
   **原因**：函数内部先做交叉周期检查，不合法直接返回 `status_invalid_argument`。
   **解决**：检查返回值；按坑 14-2 统一重载值，或改用 CMP16~23。
8. **现象**：注入故障时 `IRQ_STS_FAULT` 读到 0，撤销故障后反而变成非 0。
   **原因**：`IRQ_STS_FAULT` 是**故障事件锁存**标志，需要在故障撤销后由软件清除；
   故障持续期间读到的可能是已经"待清除"的状态（实测：`fault 1` 时 0x00，`fault 0` 后 0xC0）。
   **解决**：先 `pwmv2_clear_fault_event()` 或写 `IRQ_STS_FAULT` 清标志，再判断；
   判断"当前是否在故障中"应看输出的实际电平/FAULT 输入，而不是这个锁存位。
9. **现象**：HRPWM 开了但精度没提升。
   **原因**：没有做延迟链校准，或芯片温度已变化。
   **解决**：上电调用 `trgm_pwmv2_calibrate_delay_chain()` 直到返回 end；
   温度变化约 5 ℃ 后重校（官方 `pwmv2/hrpwm_calibrate`）。

## 14.10 优劣评价与同类对比

| 项目 | HPM PWMv2（本板 PWM0/PWM1） | HPM PWMv1（HPM6750/6300） | STM32H7 HRTIM | TC397 GTM(TOM) |
|---|---|---|---|---|
| 计数器 | 4×32 位（24 整+8 小数） | 1×28 位 | 多定时器 16 位 | 24 位 |
| 输出路数/实例 | 8（4 对互补） | 8（4 对） | 10 路 | 8/簇 |
| 比较值 | 24（0~15 固定，16~23 自由） | 24 | 每定时器 4 | 2/通道 |
| 高分辨率 | 100 ps（延迟链，需校准） | CMPJIT 1/4~1/16 周期 | ~184 ps | 无 |
| 死区最小单位 | 2 个时钟（含 8 位小数） | 0.5 个时钟 | 16 位 | 1 个时钟 |
| 硬件计算单元 | 16 个（D·p+S+T·p） | 无 | 无 | 无（ARU 做互联） |
| 内部互联 | TRGM 全交叉 | TRGM | 内部交叉 | TIM 全交叉 |
| 迁移风险 | 与 v1 寄存器完全不同 | — | — | — |

表 14-8：PWMv2 与同类高分辨率 PWM 对比（STM32/TC397 数据来自厂商公开手册，非本板实测）

**评价**：PWMv2 的 4 个计数器 + 16 个硬件计算单元是它和通用 MCU PWM 的最大差别：
一旦把"占空比序列"交给计算单元，CPU 就彻底解放，适合数字电源的多相交错。
代价是**学习曲线陡**：影子寄存器、更新时机、交叉周期限制、勘误 E00038 四件事叠在一起，
不看 UM §37.4 基本调不通。

## 14.11 参考来源

- HPM5E00 用户手册 Rev0.4 §37 PWMv2：§37.1（p.607）、§37.4.3~37.4.4（p.608-609）、
  §37.4.5（p.610-611）、§37.4.7（p.612-613）、§37.4.8（p.613）、§37.4.9~37.4.11（p.614-617）
- HPM5E00 数据手册 V0.4：PWMv2 实例与 100 ps 规格（p.12）
- HPM5E00 勘误表 Rev0.1：E00038（p.2）、E00042（p.3）
- SDK：`hpm_sdk/drivers/inc/hpm_pwmv2_drv.h`、`hpm_sdk/soc/HPM5E00/ip/hpm_pwmv2_regs.h`
- 官方例程：`hpm_sdk/samples/drivers/pwmv2/{pwm_output,fix_duty_cycle,phase_shifting,sync_output,hrpwm_calibrate}`、
  `samples/motor_ctrl/bldc_foc`
- 本仓库：`boards/hpm5e31_1/pins/pinmux_timer.c`、`boards/hpm5e31_1/ext/board_timer.h`
- 实测日志：`logs/20261030-114021-pwm_basic.log`

- 官方 SDK（v1.13.0）：[github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)，
  在线文档 [hpm-sdk.readthedocs.io](https://hpm-sdk.readthedocs.io)
- HPM5E00 数据手册：[HPM5E00FSV01.pdf](https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf)；
  用户手册 Rev0.4 为厂商内部资料（未公开），页码引用以该版本为准

## 14.12 待确认项与风险

> 已逐条复核（2026-11-02）：本节不再有“未尝试/未做/待补”的裸条目，未闭环的已写明原因与验证命令。

| 编号 | 待确认内容 | 结论（2026-11-02 已逐条复核） | 怎么验（命令/接线级） |
|---|---|---|---|
| 14-A | 死区与互补输出的**波形** | **已知限制：没有示波器**。死区作用在输出级之后，片内没有回读路径——PD06/PD07 的复用功能里 PWM 只占输出方向，IOC 的 LOOP_BACK 也接不回 PWM 的输入。本板能给的证据只到寄存器层：`dead 400` → `DEAD_AREA=0x00019000`（=400<<8），换算 2.000 us @200 MHz；`fault 1/0` 后 `IRQ_STS_FAULT` 从 0x00 变 0xC0（bit6/bit7，正是本示例使能的输出 6/7） | 示波器双通道测 PD06/PD07，量互补对两个上升沿间隔 = 死区；扫 `dead 0/100/400/1000` 画“设定-实测”曲线 |
| 14-B | HRPWM 实际最小占空比步进 | **已知限制：片内分辨率不够**。HRPWM 一步 ≈ 5 ns/256 ≈ 19.5 ps，而 GPTMR 时基是 10 ns、PWM TRGO 送捕获又大量漏采（坑 14-3），单周期边沿位置测不出来；至少需要 1 GHz 以上示波器或专用时间间隔分析仪 | ① 示波器测边沿；② 上电跑 `trgm_pwmv2_calibrate_delay_chain()` 到返回 end，回读 `TRGM_PWM_DELAY_CFG` 的 N/P tap 数，用标定得到的 ps/tap 换算实际步进（温度变 5 ℃ 重校一次，比较两次 tap 数） |
| 14-C | 中心对齐模式的 ADC 触发时刻 | **已知限制：需要与第 18 章 ADC 例程联动，本轮没有排到同一个板卡窗口**。寄存器路径已核对：`TRIGGER_CFG[n].TRIGGER_OUT_SEL` 选比较值 → TRGM `TRGM_TRGOCFG_ADC0_STRGI` → ADC 序列触发；中心对齐时把 `a=(T-duty)/2`、`b=(T+duty)/2`，比较点落在计数器两侧对称位置 | `pwm 10000 500` 后用 `pwmv2_set_trigout_cmp_index()` 把 CMP 事件送 TRGO，TRGM 接 ADC0_STRGI，ADC 例程在转换完成中断里翻转 GPIO，示波器同时看 PWM 沿与 GPIO：触发点应落在三角波顶点（中心），不是开关沿 |
| 14-D | 100 kHz 以上的占空比误差 | **已实测（2026-11-02）**：`psweep2` 扩到 100 kHz/200 kHz/500 kHz。占空比误差 +0.43%/+0.45%/+1.49%，折合高电平 +43/+23/+30 ns（约 2~4 个 10 ns tick），主要来自中断延迟与时基量化；频率误差 −500/−999/−2494 ppm 纯属 0.5 tick 量化 | 数据见表 14-9 与 `logs/20261102-124411-pwm_basic.log`；1 MHz 档因板卡窗口到期没跑到，命令 `psweep2` 已就绪，插上就能复现 |
| 14-E | Burst 发波（`CNT_BURST`）的精确周期数 | **已知限制（与比较值/T 的关系见 UM §37.4.6，未实测）** | 配 `CNT_BURST=10`，用逻辑分析仪数脉冲数 |