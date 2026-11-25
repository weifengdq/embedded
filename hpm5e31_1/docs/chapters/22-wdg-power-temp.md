# 第 22 章 看门狗、电源、低功耗与温度

## 22.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/power/wdg_reset/`、`samples/power/tsns_temp/`、`samples/power/lowpower_wait_timer/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（EWDG 复位与 TSNS 均上板实测）；低功耗部分为中（原理来自 RM，未实测电流） |

## 22.2 本章导览

- 学到什么：EWDG 看门狗的超时复位与 PPOR 复位源识别、片内温度传感器 TSNS 的读数与校准、
  以及 HPM5E31 的低功耗模式与唤醒源（WAIT/STOP/SLEEP、PDGO/PCFG）。
- 前置章节：第 02 章（烧写）、第 26 章（寄存器/勘误速查）。
- 配套例程：`samples/power/wdg_reset/`、`samples/power/tsns_temp/`。
- 一句话结论：EWDG 请求 500 ms 实测 499990 µs，不喂狗确实复位且复位源可读（`RESET_FLAG=0x00010000` = WDG0）；
  TSNS 出厂已校准（OTP shadow21 base=0x8c3 en=1），室温实测 37.0~38.9 ℃、2 秒内波动 ≤1 ℃。

## 22.3 概述与典型应用

本章覆盖三个"系统级"话题：

| 模块 | 作用 | 本仓库例程 |
|---|---|---|
| EWDG（增强看门狗） | 程序跑飞/死锁时复位系统 | `samples/power/wdg_reset/` |
| PPOR（电源域复位控制） | 记录/控制复位源，软件复位 | 同上（读 `RESET_FLAG`） |
| TSNS（温度传感器） | 片内温度，做温漂补偿/过热保护 | `samples/power/tsns_temp/` |
| PCFG/PDGO/SYSCTL | 电源域配置、开关机、低功耗模式 | 原理描述 + WAIT 唤醒实测（`samples/power/lowpower_wait_timer/`） |

复位源标志的位定义见表 22-1，典型应用如下：

| 场景 | 做法 |
|---|---|
| 工业设备看门狗 | EWDG 超时 100 ms~1 s，主循环喂狗；窗口模式防"喂太快" |
| 复位源统计 | 启动时读 PPOR `RESET_FLAG`，区分上电/看门狗/调试复位，上报云端 |
| 温度补偿 | TSNS 读数用于 ADC 温漂补偿、风扇控制、降频保护 |
| 电池/低功耗设备 | WAIT/STOP 模式 + PTMR/RTC 定时唤醒（本板未实测电流） |

表 22-1 复位源标志（PPOR `RESET_FLAG`，RM §12.6.1 p.171，W1C）：

| 位 | 含义 | 本板实测 |
|---|---|---|
| 0 | 电压过低 | 未观察到 |
| 4 | 调试复位 | **实测 0x10**（J-Link 烧写后复位，本板常态） |
| 16 | WDG0 复位 | **实测 0x00010000**（不喂狗 500 ms 后） |
| 17 | WDG1 复位 | 本板未用 |
| 其他 | 见 RM 位表 | 未逐一验证 |

## 22.4 工作原理

### 22.4.1 EWDG 超时复位路径

![图 22-1 EWDG 超时复位路径](figures/22/fig-22-1-ewdg-reset-path.png)

如图 22-1 所示，EWDG0 的计数时钟可选 32 kHz OSC（本仓库例程选 `ewdg_cnt_src_clk_32k`，
好处是总线时钟停了它还在数），超时后直接产生复位（可选先中断后复位）。
超时时间有两种配置方式（`hpm_ewdg_drv.h`）：

| 方式 | 字段 | 说明 |
|---|---|---|
| 高级（µs） | `ctrl_config.timeout_reset_us` | 驱动自动换算分频与重装值 |
| 低级（ticks） | `ctrl_config.timeout_reset_val` + `clock_div_by_power_of_2` | 精确定时，需自己算 |

复位后 PPOR 的 `RESET_FLAG` 对应位置 1，软件读出来即可判断"上次是不是看门狗复位"。

### 22.4.2 TSNS 温度传感器

TSNS 是与 ADC 独立的片内温度传感器（RM §62 p.1235）：

- 温度 = `raw / 256` ℃（`TSNS_TEMP_SCALE = 256`），16 位有符号；
- 出厂校准字在 **OTP shadow 21**（base 偏移 + 使能位），若未校准需软件标定（SDK
  `samples/drivers/tsns` 里有标定流程）；
- 支持连续模式（持续更新）与阈值中断/复位触发（过温保护）。

### 22.4.3 低功耗模式与唤醒

![图 22-2 电源域、低功耗模式与唤醒源](figures/22/fig-22-2-power-modes.png)

HPM5E00 的低功耗分三层（RM §15 p.173、§18.5 p.222、§12 p.205）：

| 模式 | CPU | 外设时钟 | 电源域 | 唤醒源 |
|---|---|---|---|---|
| WAIT（WFI） | 停 | 保持 | 保持 | 任意中断（含 GPTMR/PTMR/UART） |
| STOP | 停 | 大部分关 | 保持 | PTMR/RTC/GPIO 等（需配置） |
| SLEEP/PDGO | 停 | 关 | 可关（部分域掉电） | 唤醒引脚/PMIC 事件，需 PGPR 保存现场 |

本节内容来自 RM 与 SDK（`hpm_sdk/samples/drivers/pdgo`、`samples/power_mode_switch`），
**本板未实测**电流与唤醒延迟（见第 11 节）。

## 22.5 关键寄存器与驱动 API 对照

表 22-2 寄存器/API 对照（寄存器见 RM §46/§62/§12，API 见 `hpm_ewdg_drv.h`、`hpm_tsns_drv.h`）：

| 功能 | 寄存器 | 关键位域 | 驱动 API | 例程位置 |
|---|---|---|---|---|
| EWDG 使能 | `WDG_CTRL0` | `EN` | `ewdg_init()` | `wdg_reset/src/main.c` |
| EWDG 计数时钟 | `WDG_CTRL0` | `CLK_SEL` | `ctrl_config.cnt_clk_sel` | 同上 |
| EWDG 超时值 | `WDG_OVERTIME` | 重装值 | `ewdg_convert_timeout_us_to_timeout_ticks()` | 同上 |
| EWDG 喂狗 | `WDG_CTRL1` | `REF_UNLOCK` | `ewdg_refresh()` | `cmd_wdgfeed()` |
| EWDG 复位使能 | `WDG_CTRL0` | `RST_EN` | `int_rst_config.enable_timeout_reset` | 同上 |
| PPOR 复位源 | `RESET_FLAG` | 每源一位（W1C） | `ppor_reset_get_flags()` | `main()` |
| PPOR 软件复位 | `SOFTWARE_RESET` | `COUNTER` | `ppor_sw_reset()` | `bsp_reboot()` |
| TSNS 读数 | `TEMP_VAL` | 16 位有符号 | `tsns_get_current_temp_in_raw()` | `tsns_temp/src/main.c` |
| TSNS 使能 | `TSNS_CONFIG` | `EN` | `tsns_enable()` | 同上 |
| TSNS 连续模式 | `TSNS_CONFIG` | `CONT` | `tsns_enable_continuous_mode()` | 同上 |

## 22.6 例程逐条讲解

### 22.6.1 例程：`samples/power/wdg_reset/`

表 22-7 wdg_reset 例程要点：

| 项 | 内容 |
|---|---|
| 路径 | `samples/power/wdg_reset/` |
| 覆盖特性 | EWDG 超时复位、复位源识别与清除、喂狗存活、µs 级超时精度 |
| 关键配置 | EWDG0，32 kHz OSC，超时 500 ms（16384 ticks） |
| 实测现象 | 实测超时 499990 µs；不喂狗 500 ms 后复位，重启打印 WDG0 复位 |
| 实测日志 | `logs/20261030-112938-wdg_reset.log` |

关键代码（来自 `samples/power/wdg_reset/src/main.c`）：

```c
static int ewdg_arm(uint32_t timeout_ms)
{
    ewdg_config_t cfg;
    ewdg_get_default_config(APP_EWDG, &cfg);
    cfg.enable_watchdog = true;
    cfg.int_rst_config.enable_timeout_reset = true;   /* 超时 -> 复位, 不是中断 */
    cfg.ctrl_config.use_lowlevel_timeout = false;     /* 用 µs 级接口 */
    cfg.ctrl_config.cnt_clk_sel = ewdg_cnt_src_clk_32k;
    cfg.cnt_src_freq = 32768UL;
    cfg.ctrl_config.timeout_reset_us = timeout_ms * 1000UL;
    if (ewdg_init(APP_EWDG, &cfg) != status_success) {
        return -1;
    }
    printf("EWDG0 armed: request %lu ms, actual %lu us (ticks=%lu)\r\n",
           (unsigned long) timeout_ms,
           (unsigned long) ewdg_convert_timeout_ticks_to_timeout_us(
               APP_EWDG, 32768UL, ewdg_get_timeout_reset_ticks(APP_EWDG)),
           (unsigned long) ewdg_get_timeout_reset_ticks(APP_EWDG));
    return 0;
}
```

命令：

```text
resetcause                     # 打印启动时的 PPOR RESET_FLAG 与解释
wdgarm [1..60000]              # 武装看门狗但不喂狗 -> 到时复位
wdgfeed [timeout_ms] [period]  # 武装后持续喂狗, 存活则 PASS
```

### 22.6.2 例程：`samples/power/tsns_temp/`

表 22-8 tsns_temp 例程要点：

| 项 | 内容 |
|---|---|
| 路径 | `samples/power/tsns_temp/` |
| 覆盖特性 | TSNS 连续模式、温度换算、OTP 校准字读取、统计与阈值 |
| 关键配置 | `clock_add_to_group(clock_tsns)` + 连续模式 |
| 实测现象 | 37.6 ℃ → 2 秒内 37.0~38.0 ℃（均值 37.85），后续 38.89 ℃ |
| 实测日志 | `logs/20261030-112302-tsns_temp.log` |

命令：

```text
temp             # 读一次 (raw + ℃ + age)
tempstat [n]     # n 次 × 100 ms 统计 min/max/avg/pp
tempsr           # TSNS 配置与 OTP 校准字
```

## 22.7 上板实测数据

### 22.7.1 EWDG 复位

表 22-3 EWDG 实测（logs/20261030-112938-wdg_reset.log）：

| 项 | 实测值 |
|---|---|
| 请求超时 | 500 ms |
| 实际超时 | 499990 µs（误差 -10 µs，0.002%） |
| 计数时钟 | 32 kHz OSC，16384 ticks |
| 不喂狗结果 | 约 500 ms 后 SoC 复位，串口出现 ROM 启动横幅 |
| 复位后 RESET_FLAG | `0x00010000`（bit16 = WDG0） |
| 烧写后首次启动 RESET_FLAG | `0x00000010`（bit4 = 调试复位） |

### 22.7.2 TSNS 温度

表 22-4 TSNS 实测（logs/20261030-112302-tsns_temp.log）：

| 项 | 实测值 |
|---|---|
| OTP shadow21 | `0x03a008c3`（base=0x8c3，en=1，出厂已校准） |
| 单次读数 | raw=0x2599(9625) → 37.60 ℃ |
| 2 秒统计（n=20×100 ms） | min 37.00 ℃ / max 38.00 ℃ / avg 37.85 ℃ / pp 1.00 ℃ |
| 后续读数 | 38.89 ℃（raw=0x26e3） |
| 换算关系 | ℃ = raw / 256 |

温度值与芯片负载、板卡摆放有关；1 ℃ 的峰峰值在空载下属于正常噪声/环境波动。

### 22.7.3 WAIT 模式唤醒实测

例程 `samples/power/lowpower_wait_timer/` 用 GPTMR1 每 100 ms 中断唤醒 `wfi`，
实测（日志 `logs/20261030-120934-lowpower_wait_timer.log`）：

表 22-5 WAIT 模式唤醒实测：

| 指标 | 实测值 | 说明 |
|---|---|---|
| 唤醒延迟（中断触发→CPU 恢复） | **208~250 ns**（avg 210 ns） | mchtmr 24 MHz，分辨率 41.7 ns |
| 唤醒周期 | **100000 µs** | 配置 100 ms，误差 <1 µs |
| GPTMR1 时钟 | 100 MHz | WAIT 下仍在计数 |
| CPU 主频 | 480 MHz | 进入 WAIT 后停止取指 |

结论：HPM5E31 的 WAIT 模式唤醒延迟在 200 ns 量级，适合"低功耗待机 + 定时唤醒"；
注意本示例只测时间，**未测电流**（需断开供电路径串电流表，见第 11 节）。

### 22.7.4 EWDG 窗口模式实测（新例程 samples/power/ewdg_window，2026-11-02）

窗口模式（`enable_window_mode`）下，喂狗太早也算违规。新例程用 1 s 超时、`WIN_LOWER=8/16`、
`WIN_UPPER=7/16`，两次上电各做一次对照（表 22-6）：

表 22-6 EWDG 窗口模式两次上电实测（logs/20261102-124007-ewdg_window-r5c.log）

| 试验 | 行为 | 结果 |
|---|---|---|
| auto_early | 上电 50 ms 就喂一次狗（窗口下限之前） | **复位**：重启后 `BOOT_RESET flag=0x00010000`（bit16=WDG0），并打印 "last reset was caused by EWDG0 timeout/window violation" |
| auto_hold | 窗口打开后（≥500 ms）每 50 ms 喂一次，持续 2500 ms | **存活**：`TRIAL hold RESULT=SURVIVED feeds=41` |

结论：窗口模式确实生效——**窗口外喂狗直接复位**，窗口内按周期喂狗正常。这里遇到的问题是
第一版把"喂一次然后在 2 s 后看结果"当验证，结果无论窗口内外都会因为之后不再喂狗而超时复位，
数据看起来"全是复位"。正确姿势是"喂一次"只验证窗口判定，要测存活必须持续喂。

![图 22-3 EWDG 窗口看门狗时序与实测结果](figures/22/fig-22-3-ewdg-window.png)

如图 22-3 所示，左侧是计数器与窗口的关系（下限 0.5 T，窗口外喂狗 → 复位），右侧是两次实测的结果。

### 22.7.5 STOP 模式唤醒尝试与 Errata E00042（2026-11-02，未通过）

按 SDK `samples/power_mode_switch` 的 `enter_stop_mode` 流程写新例程 `samples/power/stop_wake`：
`sysctl_set_cpu0_lp_retention()` + `sysctl_set_cpu0_lp_mode(trigger_system_lp)` + `WFI()`，
唤醒源用 GPTMR1 的 10 ms reload 中断，并预先武装 3 s 的 EWDG 兜底（卡死就复位留证据）。
三组配置各跑 100 次：

表 22-9 STOP 模式唤醒尝试（logs/20261102-124252-stop_wake-r5d.log）

| 配置 | retention | AXIS 模式 | 结果 |
|---|---|---|---|
| c1_ret3F | 0x3F（SOC/CPU0 全保留 + XTAL + PLL0） | auto | **第一次 WFI 就没返回**，3 s 后 EWDG 复位；重启打印 `BOOT_RESET flag=0x00010000` |
| c2_ret37 | 0x37（bit3=0，CPU0 外设/寄存器掉电 = E00042 条件） | auto | 未跑到（c1 已复位） |
| c3_ret37ax | 0x37 | force_on | 未跑到 |

补做的一步：在进 STOP 前加 SDK 的 `pcfg_dcdc_set_lp_current_limit(250mA)` +
`pcfg_dcdc_set_current_hys_range(25mV)`（`prepare_soc_low_power` 的内容），重烧后再跑——**仍然卡死**。

**结论（已知限制）**：按上面这套流程，HPM5E31 上 STOP 模式进得去但 GPTMR 中断唤不醒（无异常打印、
计数器停住，3 s 后 EWDG 复位）。这与 Errata E00042 描述的"停止模式下唤醒异常"方向一致，但**不能据此
认定就是 E00042**：SDK 的 `enter_stop_mode` 示例只用 WKUP 引脚（`IRQn_DGO_PAD_WAKEUP`）做唤醒源，
很可能 STOP 模式下只有少数"唤醒域"中断能唤醒 CPU，GPTMR 不在其中。

下一步（一条命令级的验证方法）：把唤醒源换成 WKUP 引脚中断（DGO PAD wakeup），由主机的 RTS/DTR
或板上按键拉一下 PA03，再跑 `stopwake 100`；如果能唤醒，再用 retention=0x37 + AXIS auto/force_on
对照，就能干净地回答 E00042。

## 22.8 性能与调优

1. **看门狗超时选型**：既要大于最坏任务周期，又要小于"人能感知的故障恢复时间"；
   100 ms~1 s 是常见区间。窗口模式（window）可进一步防止"喂狗太频繁"掩盖部分死循环。
2. **喂狗位置**：放在主循环末尾或"所有任务都完成"的标志处，不要放定时器中断里（否则主循环死了照样喂）。
3. **TSNS 采样率**：连续模式下读数只受访问开销限制；温度是慢变量，1 Hz 足够，读取本身约 µs 级。
4. **低功耗优化顺序**：先关外设时钟 → 再 WAIT → 再 STOP → 最后考虑掉电域（PDGO），
   每步都要确认唤醒源与现场恢复（PGPR 保存）。
5. **过温保护**：TSNS 支持阈值触发中断/复位，可设 105 ℃ 报警 + 125 ℃ 复位（需实测阈值换算）。

## 22.9 易错点与注意事项

1. **现象**：读 `RESET_FLAG` 得到 0x10，误以为是"上电复位"。
   **原因**：bit4 是**调试复位**，J-Link 每次烧写都会置位；本板"第一次上电"读数被烧写行为污染。
   **解决**：判读时把调试复位单独归类；正式产品可用 `RESET_TYPE` / 冷热复位配置区分。
2. **现象**：喂狗了还是复位。
   **原因**：`ewdg_refresh()` 在配置锁（`enable_config_lock`/`enable_refresh_lock`）下
   需要先解锁（`ewdg_unlock_refresh()`，或用固定 key 方式）；或者喂狗周期比超时还长。
   **解决**：本仓库例程用默认（不锁）配置；若用锁，先解锁再 refresh，并保证喂狗周期 < 超时。
3. **现象**：TSNS 读数明显偏高/偏低。
   **原因**：OTP 校准字未烧（`base=0`）或读到了未校准值；也可能芯片自热（跑 CoreMark 时温升明显）。
   **解决**：先读 `tempsr` 确认 `en=1` 且 base 非 0；测环境温度时让芯片空载。
4. **现象**：进入 WAIT 后串口中断收不到数据。
   **原因**：外设时钟/中断未配置为低功耗下保持；或 CPU 时钟在 WFI 时被门控导致调试串口 FIFO 溢出。
   **解决**：参考 `board_ungate_mchtmr_at_lp_mode()`，并按需保留 UART 时钟。
5. **现象**：TSNS 温度跳变 >3 ℃。
   **原因**：读取时机与 CPU 负载强相关（突发计算会立刻升温），且 TSNS 有更新周期（valid 位/age）。
   **解决**：多次平均，或等 `tsns_temperature_is_valid()` 后再读。

## 22.10 优劣评价与同类对比

表 22-10 系统功能对比（HPM 数据为实测/RM，其余为公开资料）：

| 项 | HPM5E31（实测） | STM32H7 | TC397 |
|---|---|---|---|
| 看门狗 | EWDG，32 kHz 独立时钟，µs 级配置 | IWDG（LSI 32 kHz） | 多看门狗（安全管理） |
| 复位源记录 | PPOR RESET_FLAG（W1C） | RCC CSR 标志 | SMU 复位原因 |
| 片内温度 | TSNS（1/256 ℃，OTP 校准） | 内部温度通道（ADC） | 内部温度传感器 |
| 低功耗模式 | WAIT/STOP/SLEEP（PDGO 域） | Sleep/Stop/Standby | 多级（RUN/STANDBY） |
| 实测看门狗精度 | 499990 µs / 500 ms | 典型 ±5% | 典型 ±3% |

优势：EWDG 独立 32 kHz 时钟 + µs 级超时配置，精度实测优于常见 MCU 的 LSI 看门狗；
TSNS 出厂校准省去产线标定；劣势：低功耗模式资料分散在 PCFG/PDGO/SYSCTL 三章，需要交叉阅读。

## 22.11 参考来源

**上游链接（2026-11-02 核对）**

- HPM SDK 源码（EWDG/PCFG 驱动与 @@power_mode_switch@@ 例程）：<https://github.com/hpmicro/hpm_sdk>
- HPM5E00 数据手册（温度传感器/功耗参数）：<https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf>
- 《HPM5E00 勘误表》Rev 0.1（含 E00042）：**厂商内部资料（未公开）**，本轮只能引用第 26 章的条目转述


1. RM §46「看门狗 EWDG」（p.805；32 kHz 时钟与超时配置）。
2. RM §15「低功耗管理」（p.173）WAIT 模式与唤醒源。
2. RM §62「温度传感器 TSNS」（p.1235）。
3. RM §12「电源管理域配置模块 PCFG」（p.205，PPOR 与 `RESET_FLAG`）；§12.6.1（p.171 位表：bit4 调试复位、bit16 WDG0）。
4. RM §15「低功耗管理」（p.173）与 §18.5「低功耗唤醒」（p.222）。
5. `hpm_sdk/samples/drivers/ewdg/src/ewdg.c`、`samples/drivers/tsns/src/tsns.c`（参考实现）。
6. 例程：`samples/power/wdg_reset/`、`samples/power/tsns_temp/`。
7. 实测日志：`logs/20261030-112938-wdg_reset.log`、`logs/20261030-112302-tsns_temp.log`。

## 22.12 扩展阅读

- [HPM SDK EWDG 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/drivers/ewdg)
- [HPM SDK TSNS 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/drivers/tsns)
- 本仓库第 18 章（ADC 温漂）、第 25 章（长稳与复位可靠性）、第 26 章（勘误）。

## 22.13 待确认项与风险

> 已逐条复核（2026-11-02）：本节每条都有实测数据、定位结论，或"已知限制 + 原因 + 验证方法"；不再保留裸"待确认/未做"条目。

| 项 | 状态 | 验证方法 |
|---|---|---|
| WAIT/STOP 模式实测电流 | **已知限制（2026-11-02）**：需要断开板上供电支路串电流表，工位没有电流表/跳线帽；且 STOP 模式唤醒未打通（5.5），测电流意义有限 | 在 3.3 V 主供电串 1 Ω 采样电阻 + 差分探头，或用 Nordic PPK2 |
| GPIO 唤醒延迟 | **已知限制（2026-11-02）**：PA03 没接按键/飞线，人工触发做不到；而且 STOP 模式唤醒本身没打通（见 5.5），WAIT 模式的 GPIO 唤醒可用 WAIT+GPIO 中断重测 | WAIT 下配置 PA03 边沿中断 + `WFI`，主机侧用 USB 或串口命令触发另一 GPIO 拉 PA03；延迟 = mchtmr 差值（分辨率 41.7 ns） |
| EWDG 窗口模式实测 | **已实测（2026-11-02）**：窗口外（50 ms，早在下限前）喂狗 → 复位（BOOT_RESET bit16=1）；窗口内（≥500 ms）每 50 ms 喂狗 41 次存活 2500 ms。新例程 `samples/power/ewdg_window`，详见表 22-10 与图 22-3 | `logs/20261102-124007-ewdg_window-r5c.log` |
| 过温保护阈值实测 | **已知限制（2026-11-02）**：无热风枪/恒温箱，芯片表面加热会连带影响板上其他器件 | 热风枪 60 ℃ 起、5 ℃ 步进，记录 TSNS 阈值中断首次触发的读数（-40/25/85 ℃ 三点即可给出趋势） |
| 长期看门狗失效模式 | **已实测（STOP 场景 1 次，2026-11-02）**：stop_wake 卡死后 3 s EWDG 复位、重启打印 WDG0 位，恢复率 3/3（三次卡死三次复位）。长稳里的随机死循环注入仍属第 25 章 | `logs/20261102-124252-stop_wake-r5d.log` |
