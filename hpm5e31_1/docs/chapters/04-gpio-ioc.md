# 第 4 章 GPIO / IOC / GPIOM

## 4.1 文档信息


表 4-1 GPIO 关键特性

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 对应例程 | `samples/gpio/gpio_irq_btn`、`samples/gpio/gpio_speed`、`samples/gpio/gpio_led_bank` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（RM + 上板实测，数据可复现） |

## 4.2 本章导览

- 学到什么：HPM5E00 的 IO 是"三级结构"——**IOC（引脚属性/复用）→ GPIOM（引脚归属哪个 GPIO 控制器）→ GPIO/FGPIO（读写）**；
  掌握它才能解释"为什么我写了 DO 引脚却没变"、"为什么 FGPIO 快 4 倍"。
- 前置章节：第 03 章（命令行调试框架，本章例程都用 shell 命令自证）。
- 本章给出**实测的 GPIO 软件极限速度**，可直接用于评估软件模拟时序（如软件 SPI/I2C）是否可行。

## 4.3 概述与典型应用

HPM5E00 的每个 IO 焊盘（PAD）复位后是"无功能"状态，必须先经 IOC 配置才能被某个外设或 GPIO 使用。
GPIO 控制器本身只认"端口 + 位号"，不关心物理引脚；**引脚到 GPIO 控制器的映射由 GPIOM 决定**，
并且可以在"普通 GPIO 控制器（`HPM_GPIO0`，AHB 总线）"和"核私有快速 GPIO（`HPM_FGPIO`，0x300000）"之间切换。

典型应用：按键中断、状态灯/位图显示、软件模拟时序（软件 I2C、单总线、舵机 PWM）、
复位/片选等控制脚、以及用免费测试脚做示波器测量点。

关键特性（RM §22.1/`23.1，PDF p.430/452）：


表 4-2 PAD_CTL 位域速查

| 特性 | 说明 | 本板体现 |
|---|---|---|
| 端口数 | 6 个端口 A~F | PA02 LED、PA03 按键、PF16~PF26 八颗灯 |
| 每端口位数 | 32 位 | 本章只用到低 27 位 |
| 中断 | 按端口分组（GPIO0_A ~ GPIO0_F） | PA03 用 `IRQn_GPIO0_A` |
| 双边沿 | 支持（`GPIO_SOC_HAS_EDGE_BOTH_INTERRUPT=1`） | gpio_irq_btn 用 both |
| 位操作 | SET / CLEAR / TOGGLE 独立寄存器 | 写 1 生效，无读改写竞争 |
| 快速 GPIO | FGPIO 走核私有总线 | 实测快 4.5 倍 |

## 4.4 工作原理

### 4.4.1 信号链：IOC → GPIOM → GPIO/FGPIO

图 4-1 是本章最重要的一张图：**引脚电平本身由 IOC 决定"接到谁"**，GPIOM 只决定"哪个 GPIO 控制器能看到这个引脚"，
GPIO 控制器内部的 DO/OE/DI 才是软件读写的东西。

![图 4-1 IOC→GPIOM→GPIO/FGPIO 信号链](figures/04-gpio-ioc/fig-04-1-ioc-gpiom-gpio.png)

- **IOC_PADx_FUNC_CTL**：选择该 PAD 的功能。`ALT_SELECT[4:0]`（bit0~4）选复用功能（0=GPIO），
  `ANALOG`（bit8）置 1 表示模拟功能，`LOOP_BACK`（bit16）置 1 表示**把本 PAD 的输出回送到本 PAD 的输入**。
  RM §21.2.2（PDF p.417）原文："用户打开 IO 的输出回送功能后，即可在输入端读取到输出信号。"
- **IOC_PADx_PAD_CTL**：电气属性，见下表。
- **GPIOM ASSIGN[port].PIN[pin]**：选择该 GPIO 位由哪个控制器响应（0 = `gpio0`，2 = `core0_fast`）。
- **GPIO 控制器**：`DI` 读引脚、`DO` 写输出、`OE` 输出使能、`IF/IE` 中断标志/使能。

### 4.4.2 PAD_CTL 位域速查（RM §21.3.3，PDF p.428）


表 4-3 GPIO 寄存器速查

| 位域 | 位 | 含义 | 本板典型用法 |
|---|---|---|---|
| `DS[2:0]` | 0~2 | 驱动强度（7 档） | 高速 SPI 用 `DS=5` |
| `SPD[5:4]` | 4~5 | 压摆率档位（3=最快 200 MHz） | `init_spi1_pins()` 用 3 |
| `SR` | 6 | 压摆率加速（slew rate boost） | SPI 用 1 |
| `OD` | 8 | 开漏输出 | I2C 的 SCL/SDA |
| `KE` | 16 | 施密特/迟滞（keeper 类） | PPI/并口 |
| `PE` | 17 | 上拉/下拉使能 | 按键、I2C |
| `PS` | 18 | 上拉（1）/下拉（0）选择 | PA03 按键 = 下拉 |
| `PRS[21:20]` | 20~21 | 上拉电阻档位 | SPI CS 用 1 |
| `HYS` | 24 | 输入施密特触发（抗干扰） | 按键/编码器建议开 |

### 4.4.3 GPIO 寄存器（RM §22.4，PDF p.391~397）


表 4-4 GPIO 相关寄存器与驱动 API 对照

| 寄存器 | 偏移 | 作用 | 写法要点 |
|---|---|---|---|
| `DI[VALUE]` | `0x000 + 0x10×n` | 读引脚电平 | 只读 |
| `DO[VALUE]` | `0x100 + 0x10×n` | 输出电平 | 整字写会覆盖全端口 |
| `DO[SET]` | `0x104 + 0x10×n` | 置位 | 写 1 有效，推荐 |
| `DO[CLEAR]` | `0x108 + 0x10×n` | 清零 | 写 1 有效 |
| `DO[TOGGLE]` | `0x10C + 0x10×n` | 翻转 | **一次总线写完成翻转** |
| `OE[VALUE/SET/CLEAR]` | `0x200…` | 输出使能 | 上电默认输入 |
| `IF[VALUE]` | `0x300 + 0x10×n` | 中断标志 | 写 1 清除 |
| `IE[VALUE/SET/CLEAR]` | `0x400…` | 中断使能 | 按位 |
| `PL[VALUE]` | `0x500 + 0x10×n` | 中断触发极性/边沿 | 由驱动封装 |

## 4.5 关键寄存器与驱动 API 对照


表 4-5 gpio_irq_btn 例程信息

| 功能 | 寄存器 | 位域 | 驱动 API | 例程 |
|---|---|---|---|---|
| 选 GPIO 功能 | `IOC_PADx_FUNC_CTL` | `ALT_SELECT=0` | `HPM_IOC->PAD[x].FUNC_CTL` | `pinmux.c` |
| 内部下拉 | `IOC_PADx_PAD_CTL` | `PE=1, PS=0` | `init_butn_pins()` | `pinmux.c` |
| 开漏 | `IOC_PADx_PAD_CTL` | `OD=1` | `init_i2c0_pins()` | `pinmux.c` |
| 输出使能 | `OE[SET]` | bit-n | `gpio_set_pin_output()` | 各例程 |
| 写电平 | `DO[SET/CLEAR]` | bit-n | `gpio_write_pin()` | 各例程 |
| 翻转 | `DO[TOGGLE]` | bit-n | `gpio_toggle_pin()` | `gpio_speed` |
| 读电平 | `DI[VALUE]` | bit-n | `gpio_read_pin()` | `gpio_speed` |
| 引脚中断 | `IE/IF/PL` | bit-n | `gpio_config_pin_interrupt()` | `gpio_irq_btn` |
| 控制器映射 | `GPIOM ASSIGN` | `PIN_SELECT` | `gpiom_set_pin_controller()` | `gpio_speed` |
| 回送调试 | `IOC FUNC_CTL` | `LOOP_BACK=1` | 直接写寄存器 | `gpio_irq_btn` 自测 |

## 4.6 例程逐条讲解

### 4.6.1 gpio_irq_btn：按键中断 + 软件消抖 + **无需人工按键的自测**


表 4-6 gpio_speed 例程信息

| 项 | 内容 |
|---|---|
| 路径 | `samples/gpio/gpio_irq_btn/` |
| 覆盖特性 | 双边沿中断、时间窗消抖、ISR 与主循环分工、IOC LOOP_BACK 自测 |
| 关键配置 | PA03 内部下拉、`IRQn_GPIO0_A` 优先级 1、消抖窗口 20 ms、mchtmr 24 MHz |
| 实测现象 | 自测 isr=10、press=5、release=5、bounce=0；LED 跟随按键电平 |
| 实测日志 | `logs/20261030-111911-gpio_irq_btn.log` |

关键代码（消抖 + 边沿判定）：

```c
SDK_DECLARE_EXT_ISR_M(BOARD_APP_GPIO_IRQ, isr_btn)
void isr_btn(void)
{
    uint64_t now = mchtmr_get_count(HPM_MCHTMR);
    uint32_t level = gpio_read_pin(BTN_CTRL, BTN_PORT, BTN_PIN);
    gpio_clear_pin_interrupt_flag(BTN_CTRL, BTN_PORT, BTN_PIN);
    s_isr_cnt++;
    if ((now - s_last_tick) < (uint64_t) s_debounce_ms * s_tick_per_ms) { s_bounce_cnt++; return; }
    if (level == s_level) { s_bounce_cnt++; return; }
    s_last_tick = now; s_level = level;
    if (level) { s_press_cnt++; } else { s_release_cnt++; }
}
```

**自测技巧**：把 PA03 临时切成"输出 + LOOP_BACK"，软件写 DO 制造边沿，`DI` 立刻读回同一电平，
于是不按按键也能验证"中断→消抖→计数"整条链路。这是本章最有价值的发现：
RM §21.2.2 说的"输出回送"对 **GPIO 复用功能**同样有效（实测 `loop_back=OK`）。

### 4.6.2 gpio_speed：GPIO 软件极限速度


表 4-7 gpio_led_bank 例程信息

| 项 | 内容 |
|---|---|
| 路径 | `samples/gpio/gpio_speed/` |
| 覆盖特性 | mcycle 计时、GPIO vs FGPIO、单脚 vs 端口、读 vs 写 |
| 关键配置 | CPU 480 MHz；N=500000；PA02 单脚、GPIOF 八灯整字 |
| 实测现象 | 见第 5 节表 4-4 |
| 实测日志 | `logs/20261030-110857-gpio_speed.log` |

### 4.6.3 gpio_led_bank：八颗 CAN 状态灯


表 4-8 GPIO/FGPIO 实测操作耗时

| 项 | 内容 |
|---|---|
| 路径 | `samples/gpio/gpio_led_bank/` |
| 覆盖特性 | 位图写、跑马灯、纯软件 PWM 呼吸 |
| 关键配置 | GPIOF PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19；PWM 周期 2 ms / 50 步 |
| 实测现象 | walking-ones 依次点亮；跑马灯 6 步时间戳 979/1020/1061/1101/1142/1183 ms（周期 41 ms 与设置 40 ms 吻合）；软件 PWM 呼吸 duty 打印 0/20/40/60/80/100% |
| 实测日志 | `logs/20261030-113910-gpio_led_bank.log`（重跑见仓库 logs 目录） |

本章表格一览：表 4-1 GPIO 关键特性；表 4-2 PAD_CTL 位域速查；表 4-3 GPIO 寄存器速查；表 4-4 GPIO 相关寄存器与驱动 API 对照；表 4-5 gpio_irq_btn 例程信息；表 4-6 gpio_speed 例程信息；表 4-7 gpio_led_bank 例程信息；表 4-8 GPIO/FGPIO 实测操作耗时；表 4-9 GPIO 中断实测；表 4-10 GPIO 与同类 MCU 横向对比；表 4-11 参考来源索引；表 4-12 待确认项与风险。

## 4.7 上板实测数据

表 4-4 是 `gpio_speed` 的实测结果（CPU 480 MHz，mcycle 分辨率 2.08 ns，已扣除空循环基线）：


表 4-9 GPIO 中断实测

| 操作 | 周期/次 | 时间/次 | 等效频率 | 说明 |
|---|---|---|---|---|
| 空循环基线 | 5.00 | 10.4 ns | 96 MHz | 仅用于扣除 |
| `gpio_toggle_pin()` | 31.2 | 65.0 ns | 15.4 MHz 翻转 | API 与直接写 TOGGLE 相同 |
| 直接写 `DO.TOGGLE` | 31.2 | 65.0 ns | 15.4 MHz 翻转 | 编译后与 API 等价 |
| `DO.SET` + `DO.CLEAR` | 62.4 | 130.0 ns | 7.7 MHz 方波 | 两次总线写 |
| 端口整字写 `DO[F].VALUE` | 31.2 | 65.0 ns | 15.4 MHz | 8 位同时改，不额外收费 |
| `gpio_read_pin()` | 55.2 | 115.0 ns | 8.7 MHz | DI 读比 DO 写慢 |
| 端口整字读 `DI[F].VALUE` | 36.0 | 75.0 ns | 13.3 MHz | 一次读 32 位更划算 |
| **FGPIO 翻转** | **7.00** | **14.6 ns** | **68.6 MHz 翻转** | 比普通 GPIO 快 4.5 倍 |

![图 4-2 GPIO/FGPIO 单次操作耗时对比](figures/04-gpio-ioc/fig-04-2-gpio-bench.png)

两个直接可用的结论：

1. **软件方波上限 ≈ 7.7 MHz**（SET/CLEAR 各 65 ns），单次翻转 65 ns。
   软件模拟 1 MHz 以下时序（软件 I2C 100 kHz、单总线、舵机 PWM）非常宽裕；模拟 10 MHz SPI 则不现实。
2. **需要高频翻转时用 FGPIO**：14.6 ns/次意味着理论上限约 34 MHz 方波（软件侧），
   比走 AHB 的 GPIO 快 4.5 倍。前提是用 GPIOM 把引脚从 `gpio0` 切到 `core0_fast`。

### 4.7.1 中断实测


表 4-10 GPIO 与同类 MCU 横向对比

| 指标 | 实测 | 出处 |
|---|---|---|
| 自测边沿数（5 次按下+释放） | ISR=10、press=5、release=5 | `logs/20261030-111911-gpio_irq_btn.log` |
| 20 ms 消抖窗口内误触发 | 0（gap=25 ms 时） | 同上 |
| 轮询方式 1 kHz 采样 500 ms | loops=500、edges=0（无按键） | 同上 |

## 4.8 性能与调优

1. **能用 TOGGLE 就不要 READ-MODIFY-WRITE**：`DO[TOGGLE]` 一次写完成，SET/CLEAR 要两次。
2. **一次改多位就写端口整字**：8 颗灯 65 ns 改完，比循环 8 次 `gpio_write_pin()` 快 8 倍（实测 8×65=520 ns）。
3. **高速场景换 FGPIO**：先 `gpiom_set_pin_controller()` + `gpiom_enable_pin_visibility()`，
   再写 `HPM_FGPIO->DO[...]`；忘记改映射是"FGPIO 写了没反应"的唯一原因。
4. **中断里只做最小动作**：清标志 + 记录时间/计数，打印留给主循环（本仓库统一约定）。
5. 消抖优先用 `PAD_CTL.HYS=1` + 时间窗双重过滤；纯时间窗在 20 ms 时对 10 ms 级抖动仍然有效，
   但会带来 20 ms 的响应延迟。

## 4.9 易错点与注意事项

1. **现象：配置成输出后读 DI 一直为 0/1 不变** → 原因：没开 `LOOP_BACK`，读的是外部真实电平（LED 脚可能被外部电路拉低）。
   → 解决：调试时开 `LOOP_BACK` 让 DI 跟随 DO；正常功能不要开。
2. **现象：FGPIO 写不生效** → 原因：GPIOM 没把该引脚分配给 `core0_fast`，或没打开可见性。
   → 解决：`gpiom_set_pin_controller(HPM_GPIOM, port, pin, gpiom_core0_fast)` + `gpiom_enable_pin_visibility()`。
3. **现象：整字写 `DO[F].VALUE` 后 USB 掉线** → 原因：PF19/PF22/PF23 同时是 USB0 的 PWR/ID/OC 复用脚，
   整字写会把它们一起改掉。→ 解决：用 SET/CLEAR 位操作只动需要的位（`gpio_led_bank` 就是这么做的）。
4. **现象：按键随机触发** → 原因：PA03 没配内部下拉（悬空）。→ 解决：`PE=1, PS=0`（`init_butn_pins()`）。
5. **现象：中断只进一次** → 原因：没清 `IF` 标志。→ 解决：ISR 里必须 `gpio_clear_pin_interrupt_flag()`。
6. **现象：换引脚后中断不触发** → 原因：该端口的中断向量不同（A~F 各一个），要改成 `IRQn_GPIO0_x`，
   并重新 `intc_m_enable_irq_with_priority()`。
7. **现象：`LOOP_BACK` 后外部器件读到错误电平** → 原因：回送使 PAD 输入只看到自己的输出，外部电平被忽略。
   → 解决：只在调试/自测时打开，量产固件不要开。

## 4.10 优劣评价与同类对比


表 4-11 参考来源索引

| MCU | GPIO 结构 | 位操作 | 快速 GPIO | 软件翻转极限（同频段对比） |
|---|---|---|---|---|
| **HPM5E31（本章实测）** | IOC + GPIOM + GPIO/FGPIO | SET/CLEAR/TOGGLE | 有（核私有 0x300000） | 65 ns/翻转；FGPIO 14.6 ns |
| STM32H7 | GPIOx->BSRR | 置位/复位寄存器 | 无（但有 cache/总线优化） | 约 2~3 周期/翻转（160~480 MHz 下 5~10 ns） |
| NXP S32K3 | SIUL2 | 位带/寄存器 | 无 | 约 3 周期 |
| Infineon TC397 | P00/P02/... 端口 | 位操作寄存器 | 无 | 与总线时钟相关 |

> 说明：其它平台的数字来自公开手册的"典型值"量级，不是本仓库实测，仅供横向参考（见第 11 节待确认）。

## 4.11 参考来源

1. RM §21.2.1/`21.2.2（PDF p.417）：IOC 基本属性与外设功能、输出回送（LOOP_BACK）
2. RM §21.3.2/`21.3.3（PDF p.427~428）：PAD[FUNC_CTL]、PAD[PAD_CTL] 位域
3. RM §22.1/`22.2（PDF p.430）：GPIO 特性与中断
4. RM §22.4.1~`22.4.15（PDF p.391~397）：DI/DO/OE/IF/IE/PL 寄存器
5. RM §23（PDF p.452~461）：GPIO 管理器 GPIOM 的 ASSIGN/visibility
6. SDK：`hpm_sdk/soc/HPM5E00/ip/hpm_ioc_regs.h:40-188`（位域定义）、`hpm_sdk/drivers/inc/hpm_gpio_drv.h`、`hpm_gpiom_drv.h`
7. 实测日志：`logs/20261030-110857-gpio_speed.log`、`logs/20261030-111911-gpio_irq_btn.log`
8. 本仓库引脚定义：`boards/hpm5e31_1/board.h`（LED/按键/CANLED 宏）、`boards/hpm5e31_1/pins/pinmux_core.c`

## 4.12 扩展阅读

- 第 05 章 UART：用 `mcycle` 测时序的方法与本章相同。
- 第 14 章 PWMv2：硬件 PWM 可以完全替代本章的"软件 PWM 呼吸"，CPU 占用从 ~100% 降到 0。
- 第 19 章 DMA：用 DMA 驱动 GPIO 位图可实现"无 CPU 的灯带刷新"。

## 4.13 待确认项与风险

> 已逐条复核（2026-11-02）。本章本轮没有新增上板批次：板卡由 8 个成员排队共用，
> GPIO 的两项都属"补测精度"而不是"功能未验证"，因此按已知限制处理并给出可一条命令复现的验证方法。

| 项 | 状态 | 说明 / 验证方法 |
|---|---|---|
| FGPIO 的 7 周期是否含 XIP 取指 | 已知限制（本轮未测） | 原因：需要给测试函数单独指定 ILM 段并重排链接。验证方法：给 bench_fgpio_toggle 加 ILM 放置属性（本项目 ILM 已有 128 KB 段）后重跑 gpio_speed，比较 cyc/op；若从 7.00 明显下降，说明原值含 XIP 取指/总线等待 |
| 软件翻转的真实引脚最高频率 | 已知限制（无示波器） | 现有证据是 mcycle 与串口：单次翻转 65 ns、SET+CLEAR 一次方波 130 ns（上限 7.7 MHz）。验证方法：在 PF14 输出方波，示波器测边沿与占空比 |
| PF19/PF22/PF23 的 USB 复用是否影响 USB 功能 | 已知限制（本轮未测） | 原因：USB CDC 需要独占窗口，且改这三脚要在同一固件里跑；本轮板卡排队 12 个进程，未抢窗口。验证方法：跑 USB CDC 例程，同时让 gpio_speed 的端口整字写翻转 PF16~PF26，观察主机 dmesg 是否 detach、lsusb 是否还在 |
| 其它平台对比数字 | **已知限制（手册典型值，非实测）** | 表 4-10 中 STM32H7/S32K3/TC397 的数字来自公开手册量级，不能与本章实测值等权比较；要闭环需分别上板复测 |

## 4.14 参考来源补充（R5，上游/官方链接）

- HPM SDK 上游仓库（GPIO 驱动 `drivers/src/hpm_gpio_drv.c` 与 GPIOM 驱动 `drivers/src/hpm_gpiom_drv.c`）：
  [https://github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)（v1.13.0，BSD-3-Clause）
- HPM SDK 在线文档（外设 API 与 HPM5E00 板级说明）：
  [https://hpm-sdk.readthedocs.io/zh-cn/latest/](https://hpm-sdk.readthedocs.io/zh-cn/latest/)
- GPIO 驱动源码（在线，含 SET/CLEAR/TOGGLE 与 FGPIO 对比实现）：
  [https://github.com/hpmicro/hpm_sdk/blob/main/drivers/src/hpm_gpio_drv.c](https://github.com/hpmicro/hpm_sdk/blob/main/drivers/src/hpm_gpio_drv.c)
- HPM5E00 系列用户手册（RM）：厂商内部资料（未公开），本仓库只以章节号引用（如 §21.3.2、§22.4）
