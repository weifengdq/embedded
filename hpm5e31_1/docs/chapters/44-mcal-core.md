# 第 44 章 MCAL 核心驱动：Mcu / Port / Dio

## 44.1 文档信息

| 项目 | 内容 |
|---|---|
| 章标题 | MCAL 核心驱动：Mcu / Port / Dio |
| 版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 适用硬件 | hpm5e31_1（HPM5E31xGNx，24 MHz XTAL，标称 480 MHz，8 MB Flash XIP） |
| 适用软件 | HPM SDK v1.13.0、GCC RISC-V（-Wall -Wextra，0 warning）、本仓库 autosar 分支 |
| 对应例程 | samples/mcal/mcu_clock_init、samples/mcal/mcu_reset_mode、samples/mcal/port_pin_config、samples/mcal/dio_led_button |
| 实测日期 | 2026-11-02（全部数据为上板实测，日志在 logs/ 下，正文逐条给出） |
| 可信度 | 高：每个数字都能在 logs/ 的串口日志里复核；未实测的结论单独标注"对照/估算" |
| 代码位置 | mcal/Mcu/**、mcal/Port/**、mcal/Dio/**（驱动）与 4 个例程目录 |

## 44.2 本章导览

这一章写 MCAL 里最底层、也最容易被上层忽略的三个模块：Mcu 管时钟与复位，Port 管引脚，
Dio 管电平。三者是其它 10 个 MCAL 模块的地基——时钟不对，SPI 分频全错；引脚配错，CAN 发不出帧。

阅读顺序建议：

1. 先看 §44.3 的模块职责划分，弄清楚"为什么 Dio 没有 Init"；
2. §44.4 讲时钟树与 IOC/GPIO 的数据通路，配图 44-1、图 44-2；
3. §44.5 给出寄存器级对照表（表 44-3），驱动源码就按这张表写；
4. §44.6、§44.7 是 API 与 4 个例程的用法；
5. §44.8、§44.9 是上板实测数据（主频误差、回环误码、单次调用开销）；
6. §44.10 是遇到过的问题（LOOP_BACK、上下拉稳定时间、PLL 重锁、PF26 与 ADC 复用）；
7. §44.11 与 HPM SDK 直调、Infineon TC4x MCAL、NXP S32K3 RTD 做对照。

## 44.3 概述

AUTOSAR CP 对这三个模块的职责边界划得很死（见 SWS_MCUDriver / SWS_PortDriver / SWS_DIODriver）：

| 模块 | 模块 ID | 管什么 | 不管什么 |
|---|---|---|---|
| Mcu | 101 | 时钟源、PLL 倍频/分频、总线与 CPU 时钟节点、复位源读取、软件复位、低功耗模式 | 不碰引脚复用，不碰 GPIO 电平 |
| Port | 124 | 引脚复用（ALT）、方向、内部上拉/下拉、开漏、驱动强度；Port_Init 一次性配置全部引脚 | 不做运行时电平读写（除了初始电平） |
| Dio | 120 | 通道/端口/通道组的电平读写与翻转 | **没有 Dio_Init**：方向由 Port 配，Dio 只写 DO / 读 DI |

"为什么 Dio 没有 Init"是评审最容易问的点。AUTOSAR 的配置模型里，Dio 的通道映射是**实现固定、不可配置**的
（SWS_Dio_00180：mapping of the ID is implementation specific but not configurable），运行期没有任何需要初始化的状态；引脚的电平、
方向、上下拉全部属于 Port 的配置范围。所以本仓库的 `dio_led_button` 例程顺序是
`Port_Init(&Port_Config)` → `Dio_WriteChannel(...)`，而不是自己造一个 `Dio_Init`。

本仓库的模块 ID、API ID、错误码全部按 SWS 原文取（模块 ID 来自 mcal/common/Mcal_ModuleId.h，
API ID 即 SWS §8.3 各函数的 Service ID），不自定义编号。

## 44.4 工作原理

### 44.4.1 时钟树

HPM5E31 的时钟由三块硬件组成：

- **PLLCTLV2**：3 个 PLL（PLL0/PLL1/PLL2），每个 PLL 有 MFI/MFN/MFD 三个倍频寄存器和 3 个后分频器 DIV[0..2]；
  后分频比 = DIV/5 + 1，所以 DIV=5 是 /2.0，DIV=7 是 /2.4。参考时钟可选 XTAL24M 或 IRC24M。
- **SYSCTL**：时钟节点（CLOCK[0..40]），每个节点有 3 位 MUX（选 8 个时钟源之一）和 8 位 DIV（分频 = DIV+1）。
  CPU0 是节点 0，MCHTMR 是节点 1，AHB0 是节点 2，XPI0 是节点 32。
- **PPOR**：复位源标志（RESET_FLAG，W1C）和软件复位计数器（SOFTWARE_RESET，按 24 MHz 递减）。

hpm5e31_1 板上电后的实际配置（boards/hpm5e31_1/clock.c 的 `init_board_clock_source`）：

1. XTAL 24 MHz → PLL0 倍频 40 倍 = 960 MHz；
2. POSTDIV0 = /2.0 → 480 MHz（给 CPU0）；POSTDIV1 = /2.4 → 400 MHz（给外设）；
3. CPU0 节点 MUX=pll0_clk0、DIV=/1 → 480 MHz；
4. MCHTMR0 节点 MUX=osc24m、DIV=/1 → 24 MHz（BSP 的 `board_delay_us` 用它计时）；
5. AHB0（节点 2）由 boot ROM 的时钟预设给出 200 MHz，XPI0（节点 32）100 MHz。

![图 44-1 HPM5E31 时钟树与 Mcu_InitClock 的写入路径](figures/44-mcal-core/fig-44-1-clock-tree.png)

图 44-1 里的每个方框都对应驱动里一次寄存器写：PLL 参数写 PLLCTLV2，后分频写 PLL.DIV[n]，
节点写 SYSCTL.CLOCK[n]。Mcu_InitClock 的入参是 0/1 两个"时钟配置号"，具体数值在
`mcal/Mcu/cfg/Mcu_PBcfg.c` 的 `Mcu_ClockSettings[]` 里，改板只改那一张表。

### 44.4.2 引脚数据通路（IOC → GPIO → Dio）

HPM 的引脚分两级：IOC（I/O Controller，424 个 PAD）和 GPIO（DI/DO/OE 三组寄存器）。

- **IOC PADx.FUNC_CTL**：ALT_SELECT[4:0] 选复用功能（0 = GPIO），ANALOG 位选模拟通路，
  LOOP_BACK 位"强制打开输入"；
- **IOC PADx.PAD_CTL**：PE（上下拉使能）、PS（上拉/下拉）、PRS（上拉阻值）、OD（开漏）、
  SR/SPD/DS（摆率/速度档/驱动强度）、HYS（施密特）；
- **GPIO OE/DO/DI**：OE 管方向，DO 有 SET/CLEAR/TOGGLE 三个单比特写口，DI 是引脚电平。

Port 驱动做的事情就是"按配置表写 IOC + 写 GPIO OE/DO"，Dio 只读写 GPIO 的 DO/DI。
图 44-2 是 SPI1↔SPI3 杜邦线上的回环测试路径，也是本章例程的物理基础。

![图 44-2 SPI1↔SPI3 杜邦线 GPIO 回环与寄存器路径](figures/44-mcal-core/fig-44-2-port-loopback.png)

### 44.4.3 Mcu 的初始化序列

SWS_MCUDriver 图 9.1 给出标准序列：`Mcu_Init` → `Mcu_InitClock` → 轮询 `Mcu_GetPllStatus` →
`Mcu_DistributePllClock`。本驱动严格照做：

1. `Mcu_Init` 只做三件事：保存配置指针、快照 PPOR.RESET_FLAG（保证多次读一致）、清状态；
2. `Mcu_InitClock` 写 PLL 与时钟节点，**不等待锁定**（SWS_Mcu_00138 明确要求立即返回）；
3. 上层轮询 `Mcu_GetPllStatus`（读 PLL0.MFI 的 ENABLE/BUSY/RESPONSE 位）；
4. `Mcu_DistributePllClock` 再确认锁定并幂等重写节点；未锁定则报 `MCU_E_PLL_NOT_LOCKED` 并返回 E_NOT_OK。

有一个 HPM 特有的处理：写 PLL0.MFI 会触发 PLL 重新锁定，而 CPU 正跑在 PLL0 上。
所以驱动先比较寄存器现值，只有目标值不同才写（见 §44.6 代码）。这样"重新配置成同一频率"是零风险操作，
实测 `mset 0` 反复切换也不会掉串口。

## 44.5 寄存器与配置

表 44-3 是本章三个模块用到的主要寄存器（按"寄存器 / 位域 / 含义 / 驱动 API"组织，可直接对照源码；
完整的位域定义见 SDK 的 hpm_pllctlv2_regs.h / hpm_sysctl_regs.h / hpm_ppor_regs.h / hpm_ioc_regs.h / hpm_gpio_regs.h）。

| 寄存器 | 位域 | 含义 | 用它的 API | 例程位置 |
|---|---|---|---|---|
| PLLCTLV2.PLL0.MFI | MFI[6:0]、ENABLE[28]、BUSY[31]、RESPONSE[29] | 倍频整数；ENABLE/BUSY/RESPONSE 是锁定位 | Mcu_InitClock、Mcu_GetPllStatus | mcu_clock_init `mclock` |
| PLLCTLV2.PLL0.MFN/MFD | MFN[29:0]、MFD[29:0] | 倍频小数（f=24M×(MFI+MFN/MFD)） | Mcu_InitClock | Mcu_PBcfg.c |
| PLLCTLV2.PLL0.DIV[0..2] | DIV[5:0] | 后分频，分频比=DIV/5+1 | Mcu_InitClock | mcu_clock_init `mclock` |
| PLLCTLV2.PLL0.CONFIG | REFSEL[0] | 0=XTAL24M，1=IRC24M | Mcu_InitClock | Mcu.c:mcu_apply_pll |
| PLLCTLV2.XTAL | RESPONSE[29]、BUSY[31]、RAMP_TIME[19:0] | 晶振稳定/忙状态 | Mcu_GetPllStatus（辅助） | — |
| SYSCTL.CLOCK[n] | MUX[10:8]、DIV[7:0]、LOC_BUSY[30] | 时钟节点源选择与分频 | Mcu_InitClock、Mcu_GetClockNodeHz | mcu_clock_init |
| SYSCTL.RESOURCE[n] | MODE[1:0] | 外设时钟门控（1=开，2=关） | （BSP 负责） | — |
| PPOR.RESET_FLAG | FLAG[31:0]（W1C） | 复位源：bit0 brownout、bit4 debug、bit16 wdog0、bit31 software | Mcu_GetResetReason/RawValue | mcu_reset_mode |
| PPOR.SOFTWARE_RESET | COUNTER[31:0] | 24 MHz 递减，到 2 触发软复位 | Mcu_PerformReset | mcu_reset_mode `mreset` |
| IOC.PAD[n].FUNC_CTL | ALT_SELECT[4:0]、ANALOG[8]、LOOP_BACK[16] | 引脚复用/模拟/回读 | Port_Init、Port_SetPinMode | port_pin_config |
| IOC.PAD[n].PAD_CTL | PE[17]、PS[18]、PRS[21:20]、OD[8]、SR[6]、SPD[5:4]、DS[2:0]、HYS[24] | 上下拉/开漏/驱动强度 | Port_Init、Port_SetPinPull | port_pin_config `ppull` |
| GPIO.OE[n] | SET/CLEAR | 端口方向位（1=输出） | Port_SetPinDirection | port_pin_config `pdir` |
| GPIO.DO[n] | VALUE/SET/CLEAR/TOGGLE | 输出电平 | Dio_WriteChannel/FlipChannel/MaskedWritePort | dio_led_button |
| GPIO.DI[n] | VALUE | 引脚输入电平（需 LOOP_BACK=1 才回读输出） | Dio_ReadChannel/ReadPort/ReadChannelGroup | dio_led_button |

表 44-2 是 Mcu 的两个时钟配置与实测结果（§44.8 有完整采样）：

| 配置号 | PLL0 | POSTDIV | CPU0 节点 | 配置频率 | 实测（均值） | 误差 |
|---|---|---|---|---|---|---|
| `MCU_CLOCK_SETTING_0` | 24 MHz × 40 = 960 MHz | /2.0（DIV=5） | MUX=pll0_clk0，DIV=/1 | 480 MHz | 479,997,500 Hz | -5 ppm |
| `MCU_CLOCK_SETTING_1` | 同上（参数不变，不重写 PLL） | 同上 | DIV=/2 | 240 MHz | 239,997,720 Hz | -9 ppm |

表 44-4 是 `Port_PinConfig[]` 的 16 行实体（节选关键列），表 44-5 是 Dio 的通道与通道组。
两个字段值得注意：`directionChangeable` 与 `modeChangeable`。AUTOSAR 要求"运行时不可改的方向/模式"在配置里声明，
Port_SetPinDirection 对这类引脚必须报 `PORT_E_DIRECTION_UNCHANGEABLE` 且不动作。本板把 CAN 状态灯和 USR BTN
设为不可改方向，USR LED 与回环线可改。

表 44-4 Port 引脚配置（驱动不出现引脚名，全部来自这张表）：

| 逻辑号 | 引脚 | IOC PAD | GPIO | 方向 | 上下拉 | 开漏 | 初值 | 改方向 | 用途 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | PA02 | 2 | A02 | OUT | 无 | 0 | 0 | 可 | USR LED（兼 BOOT_MODE0） |
| 1 | PA03 | 3 | A03 | IN | 下拉 | 0 | 0 | 不可 | USR BTN（兼 BOOT_MODE1） |
| 2 | PC11 | 75 | C11 | OUT | 无 | 0 | 0 | 可 | 回环输出 / SPI1 nCS |
| 3 | PA27 | 27 | A27 | IN | 无 | 0 | 0 | 可 | 回环输入 / SPI3 nCS |
| 4..11 | PF26/18/20/16/22/21/23/19 | 186/178/180/176/182/181/183/179 | F* | OUT | 无 | 0 | 0 | 不可 | 8 颗 CAN 状态灯 |
| 12 | PC12 | 76 | C12 | OUT | 无 | 1 | 1 | 可 | 开漏输出（MISO 对） |
| 13 | PA28 | 28 | A28 | IN | 上拉 | 0 | 0 | 可 | 开漏测试输入（MISO 对） |
| 14 | PC10 | 74 | C10 | OUT | 无 | 0 | 0 | 可 | 回环输出（SCLK 对） |
| 15 | PA26 | 26 | A26 | IN | 无 | 0 | 0 | 可 | 回环输入（SCLK 对） |

表 44-5 Dio 通道与通道组：

| 编号 | 通道映射 | 方向（Port 决定） | 用途 |
|---|---|---|---|
| 0..15 | 与表 44-4 同序：PA02/PA03/PC11/PA27/PF26…/PC12/PA28/PC10/PA26 | 见表 44-4 | 逐通道读写 |
| 通道组 0 | GPIOF，offset=16，mask=0xFF | 输出 | CAN 灯组（覆盖 6 颗） |
| 通道组 1 | GPIOA，offset=2，mask=0x3 | 只读 | USR LED + USR BTN |
| 通道组 2 | GPIOF，offset=16，mask=0x7FF | 输出 | CAN 灯宽组（跨 8 灯 + 未用位） |

## 44.6 驱动实现与 API

### 44.6.1 API 覆盖

表 44-1 是三个模块的 API 完成度。SWS 必选 API 全部有实现；Mcu 的两个 RAM 相关 API 按 SWS 可选参数
（McuGetRamStateApi 等）保留为"显式桩"，返回 `E_NOT_OK` / `MCU_RAMSTATE_INVALID`，不静默缺失，原因见
表后的说明与 §44.13 R5：

| 模块 | API | 说明 |
|---|---|---|
| Mcu | Mcu_Init / Mcu_InitClock / Mcu_DistributePllClock / Mcu_GetPllStatus / Mcu_GetResetReason / Mcu_GetResetRawValue / Mcu_PerformReset / Mcu_SetMode / Mcu_GetVersionInfo | 扩展 Mcu_GetCoreClockHz / Mcu_GetClockNodeHz / Mcu_ClearResetFlags / Mcu_IsInitialized |
| Mcu（可选未实现） | Mcu_InitRamSection / Mcu_GetRamState | 本板 RAM 由启动代码清零，无"内容有效"标志；函数保留并返回 E_NOT_OK / MCU_RAMSTATE_INVALID，不静默缺失 |
| Port | Port_Init / Port_SetPinDirection / Port_RefreshPortDirection / Port_GetVersionInfo / Port_SetPinMode | 扩展 Port_GetPinCount / Port_GetPinConfig / Port_SetPinPull |
| Dio | Dio_ReadChannel / Dio_WriteChannel / Dio_ReadPort / Dio_WritePort / Dio_ReadChannelGroup / Dio_WriteChannelGroup / Dio_GetVersionInfo / Dio_FlipChannel / Dio_MaskedWritePort | 全部实现（Flip/MaskedWrite 是 SWS 可选 API） |

表 44-6 是开发错误码与例程注入结果（Det 计数为实测值）：

| 模块 | 错误码 | 触发条件 | 例程命令 | 实测 Det 增量 |
|---|---|---|---|---|
| Mcu | MCU_E_PARAM_CLOCK 0x0B | Mcu_InitClock 时钟号越界 | `merr` | 3（含下面两条） |
| Mcu | MCU_E_PARAM_MODE 0x0C | Mcu_SetMode 模式号越界 | `merr` | 同上 |
| Mcu | MCU_E_PARAM_POINTER 0x10 | GetVersionInfo 传 NULL | `merr` | 同上 |
| Mcu | MCU_E_UNINIT 0x0F | Init 前调用其它 API | — | 代码路径，未在例程注入 |
| Port | PORT_E_PARAM_PIN 0x0A | 引脚号 0xFFFF | `perr` | 5 |
| Port | PORT_E_PARAM_INVALID_DIRECTION 0x11 | 方向传 7 | `perr` | 同上 |
| Port | PORT_E_PARAM_INVALID_MODE 0x0D | 模式传 99 | `perr` | 同上 |
| Port | PORT_E_DIRECTION_UNCHANGEABLE 0x0B | 改不可变方向引脚 | `perr` | 同上 |
| Port | PORT_E_PARAM_POINTER 0x10 | GetVersionInfo 传 NULL | `perr` | 同上 |
| Dio | DIO_E_PARAM_INVALID_CHANNEL_ID 0x0A | 通道 0xFFFF | `derr` | 6 |
| Dio | DIO_E_PARAM_INVALID_PORT_ID 0x14 | 端口 99 | `derr` | 同上 |
| Dio | DIO_E_PARAM_INVALID_GROUP 0x1F | 未在配置里声明的通道组 | `derr` | 同上 |
| Dio | DIO_E_PARAM_POINTER 0x20 | 通道组/版本信息传 NULL | `derr` | 同上 |

### 44.6.2 关键代码：PLL 幂等配置

```c
/* mcal/Mcu/src/Mcu.c —— 只有目标值与寄存器现值不同才写 PLL，
 * 避免"重新配置同一频率"时 PLL 重锁把 CPU 时钟打断（HPM 特有） */
static Std_ReturnType mcu_apply_pll(const Mcu_ConfigType *cfg, const Mcu_PllConfigType *p)
{
    PLLCTLV2_Type *pll = (PLLCTLV2_Type *)p->pllBase;
    uint32 curMfi = PLLCTLV2_PLL_MFI_MFI_GET(pll->PLL[p->pllIndex].MFI);
    boolean pllEnabled = ((pll->PLL[p->pllIndex].MFI & PLLCTLV2_PLL_MFI_ENABLE_MASK) != 0u);

    if ((curMfi != (uint32)p->mfi) || (!pllEnabled)) {
        pll->PLL[p->pllIndex].CONFIG &= ~PLLCTLV2_PLL_CONFIG_REFSEL_MASK;   /* REFSEL=0: XTAL24M */
        pll->PLL[p->pllIndex].MFD = p->mfd;   /* RM：MFD 只能在 PLL 关闭时写 */
        pll->PLL[p->pllIndex].MFN = p->mfn;
        pll->PLL[p->pllIndex].MFI = (uint32)p->mfi;   /* 写 MFI 触发启动/重锁 */
    }
    if (!mcu_wait_pll_locked(cfg, p, p->lockTimeoutUs)) {   /* mchtmr 计时，不死等 */
        return E_NOT_OK;
    }
    /* POSTDIV0/POSTDIV1 同样只在变化时写 */
    return E_OK;
}
```

配套的"超时按 mchtmr 24 MHz 计时"也很重要：不能用空循环凑数，因为循环次数随 CPU 主频变化，
而 mchtmr 固定 24 MHz，换到 240 MHz 配置下超时仍然是 1 ms。

## 44.7 例程

四个例程覆盖"时钟/复位/引脚/电平"四件事，全部按 §6 模板写（`CMakeLists.txt` + `src/main.c` + `README.md`），
上板命令统一走 `scripts/hwrun.sh`。

| 例程 | 验证点 | 关键命令 | 日志 |
|---|---|---|---|
| mcu_clock_init | PLL/节点配置、主频实测、240 MHz 切换、Det | `mclock` `mmeasure` `mset 0\|1` `merr` | logs/20261102-135140-mcu_clock_init.log |
| mcu_reset_mode | 复位源读取、软复位、模式切换 | `minfo` `mclear` `mreset` `mmode 2` | logs/20261102-140030-mcu_reset_mode.log |
| port_pin_config | 回环误码、上下拉、开漏、运行时改方向、Det | `ploop 2000` `ppull` `pod` `perr` | logs/20261102-135603-port_pin_config.log |
| dio_led_button | LED/按键、8 灯图案、端口/通道组读写、性能、Det | `dled` `dbtn` `dleds A5` `dgroup` `dperf` `derr` | logs/20261102-135945-dio_led_button.log |

上板复跑命令（一条命令完成编译+烧写+串口抓取）：

```bash
bash scripts/hwrun.sh mcal/mcu_clock_init   --secs 30 --send 'mclock;mset 1;mclock;mmeasure;mset 0;mmeasure;merr'
bash scripts/hwrun.sh mcal/mcu_reset_mode   --secs 25 --send 'minfo;mclear;mreset'
bash scripts/hwrun.sh mcal/port_pin_config  --secs 25 --send 'pinfo;ploop 2000;ppull;pod;pdir;perr'
bash scripts/hwrun.sh mcal/dio_led_button   --secs 30 --send 'dio;dled on;dled flip;dleds A5;dgroup;dperf 1000000;derr'
```

## 44.8 实测

### 44.8.1 主频

测量方法：mchtmr 固定 24 MHz（`init_board_clock_source` 里 `clock_mchtmr0 = osc24m/1`），
在一个 10 ms 的 mchtmr 窗口内用 `rdcycle` 数 CPU 周期，`f = cycles × 24 MHz / ticks`。每次打印 5 个采样。

- 480 MHz 配置：`479,996,300 / 479,997,800 / 479,997,800 / 479,997,800 / 479,997,800`，
  均值 479,997,500 Hz，误差 **-5 ppm**；
- 240 MHz 配置（`mset 1`）：`239,997,400 / 239,997,800 / 239,997,800 / 239,997,800 / 239,997,800`，
  均值 239,997,720 Hz，误差 **-9 ppm**；
- 两次独立上板（logs/20261102-134845、135140）480 MHz 均值分别为 479,997,500 / 479,997,400 Hz，
  重复性在 ±2 kHz 内（≈4 ppm），说明误差主要来自 24 MHz 晶振本身而不是测量方法。

![图 44-3 480/240 MHz 配置值与实测值对比（误差单位 ppm）](figures/44-mcal-core/fig-44-3-clock-measure.png)

时钟树其它节点（Mcu_GetClockNodeHz 与 SDK `clock_get_frequency` 交叉验证，两者完全一致）：
cpu0 = 480,000,000 Hz，ahb0 = 200,000,000 Hz，xpi0 = 100,000,000 Hz，mchtmr0 = 24,000,000 Hz。

### 44.8.2 复位源自证

`mcu_reset_mode` 在一次串口会话里抓到了"复位前 → 复位后"两段：

```text
首启（J-Link 烧写后）：RESET: reason=MCU_RESET_UNDEFINED(3) raw=0x00000010   ← bit4 = debug reset
mclear               ：RESET: reason=MCU_RESET_UNDEFINED(3) raw=0x00000000
mreset               ：Mcu_PerformReset(): software reset in ~4 us ...
重启后               ：RESET: reason=MCU_SW_RESET(2) raw=0x80000000          ← bit31 = software
```

这四行就是"复位链路通"的证据：software 位是被 `Mcu_PerformReset` 写 `PPOR.SOFTWARE_RESET` 之后
由硬件置起来的，不是软件自己打印的常量。UART 是 USB CDC 虚拟串口，芯片复位不影响主机端句柄，
所以 `hwrun.sh` 一次抓取就能看到两次启动。

### 44.8.3 Port 回环、上下拉、开漏

- 回环（PC11→PA27、PC10→PA26）：全 0、全 1 加 2000 组 16 位 LFSR 随机图案，**误码 0**（`LOOP: n=2000 patterns=2+2000 errors=0 -> PASS`）；
- 内部上下拉：对端设高阻后，PA27/PA26 上拉读 1、下拉读 0，两组都 PASS；
  读回时 `PAD_CTL=0x01120075`（HYS=1、PRS=1(47 kΩ)、PE=1、SR=1、SPD=3、DS=5，PS=0 即下拉档）；
- 开漏：PC12 写 1（释放）时 PA28 靠内部上拉读 1，写 0 时读 0，PASS；
- 时序易错点：改成内部下拉后**立刻读会读到旧电平**，例程加 200 us 等待才通过（第一次跑就是这里 FAIL）。

### 44.8.4 Dio 电平与通道组

- USR LED（PA02）：写 1 回读 1、写 0 回读 0、`Dio_FlipChannel` 翻转正常；
- 8 颗 CAN 灯：`dleds A5` 后逐通道回读为 1,0,1,0,0,1,0,1，与图案逐位一致；
- 通道组：`Dio_ReadChannelGroup(GPIOF[23:16])` 读到 0x3A——注意这是**引脚位**不是 LED 序号，
  因为 8 颗灯分散在 PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19，其中 PF26、PF19 不在 [23:16] 内；
- 错误注入：非法通道/端口/通道组/NULL 四次调用后 Det 计数从 0 增到 6（DET 实测值）。

## 44.9 性能

用 `dperf` 命令（mchtmr 计时，`Dio_WriteChannel`/`Dio_ReadChannel` 各 100 万次）实测单次调用开销，见表 44-7。

表 44-7 Dio 单次调用开销（480 MHz 配置，1,000,000 次循环）：

| 操作 | 1,000,000 次耗时 | 单次 | 折算 480 MHz 周期 | 说明 |
|---|---|---|---|---|
| Dio_WriteChannel | 127,093,750 ns | 127 ns | ≈ 61 cycles | 含函数调用、通道合法性检查、DO SET/CLEAR 写 |
| Dio_ReadChannel | 185,000,125 ns | 185 ns | ≈ 89 cycles | 多一次 DI 读 + 分支 |
| Port 回环 2002 组 | — | — | — | 0 误码（功能指标，非时间指标） |

作为对照，HPM SDK 的 `gpio_write_pin()` 是头文件里的 static inline（直接写 DO.SET/CLEAR），
编译后约 3~6 条指令；MCAL 版本多出函数调用、DET 参数检查和通道表查表，127 ns ≈ 61 cycles 属于合理量级。
如果对时序极其敏感的代码（比如软件模拟协议），可以不经过 Dio 直接写 GPIO，这是"合规 vs 性能"的常规取舍。
把 `MCAL_DEV_ERROR_DETECT` 置 `STD_OFF` 可以把 DET 检查编译掉，但函数调用与查表开销仍在。

主频测量本身的开销：一次 `mmeasure` 是 5 个 10 ms 窗口 ≈ 50 ms，测量期间 CPU 忙等；
对 480 MHz 来说 10 ms 窗口 ≈ 4.8 M 周期，远大于 rdcycle 的 1 周期量化误差，所以 ppm 级精度可信。

## 44.10 易错点

1. **GPIO 输出读回恒 0**：HPM 的 GPIO 输出不会自动把 pad 电平送回输入总线，必须置
   `IOC PADx.FUNC_CTL.LOOP_BACK=1`（RM："force input on"）。不置位时 `Dio_ReadChannel` 对输出通道恒返回 0，
   看起来像"Dio 写不进去"，其实是读通路没开。Port 驱动在 GPIO 模式下自动置位。
2. **改内部上下拉要等稳定**：100 kΩ 下拉对 pad 电容放电要几微秒到几十微秒，写完立刻读会读到旧电平；
   例程里加 200 us 等待。第一次 `ppull` FAIL 就是这个原因，不是上下拉不生效。
3. **写 PLL 会打断 CPU**：CPU 时钟来自 PLL0，重写 MFI 会触发 PLL 重锁、CPU 短暂停摆。驱动做了幂等判断
   （目标值相同就不写）；即便如此，切频率时也不要在中断里做，且要保证串口时钟不依赖被切的节点。
4. **PF26 与 ADC0_IN01 复用**：PF26 既是 CAN0 红灯又是 ADC0 通道 1。Dio 例程驱动它没问题，
   但 ADC 例程运行时不要同时点这颗灯，否则采样值被 LED 驱动电路影响。这是"同一引脚两个 MCAL 模块"的典型冲突，
   解决方式是例程级互斥（不同时烧写），AUTOSAR 配置工具里则应报 resource conflict。
5. **PA02/PA03 兼 BOOT_MODE0/1**：复位瞬间按住 PA03（USR BTN）会进 ISP 模式，板子不跑应用。
   调试时按复位键别按着按键；Dio 例程的按键轮询只在运行期读，不影响启动。
6. **shell 命令名冲突**：BSP 已占用 `ver/uptime/reboot/clk/pin/reg/mem`，MCAL 例程命令统一加模块前缀
   （`mcu/mclock/mset`、`pinfo/ploop`、`dio/dled`），避免覆盖 BSP 命令。
7. **letter-shell 的 CMD_FUNC 与 CMD_MAIN 不是一回事**：`SHELL_TYPE_CMD_FUNC` 走 shell_ext 的参数解析
   （按类型把参数转成 size_t），传 `(argc, argv)` 的函数必须用 `SHELL_TYPE_CMD_MAIN`。遇到过一次，
   表现为 argc 恒为垃圾值、`mset 1` 永远走 usage 分支。
8. **PF26 组读不是 LED 序号**：通道组的 mask 是引脚位；把 8 颗分散的灯当连续通道组用会得到"看起来错"的值。

## 44.11 对比

### 44.11.1 与本仓库 HPM SDK 直调（表 44-8）

表 44-8 HPM SDK 直调与 AUTOSAR MCAL 对比：

| 维度 | HPM SDK 直调 | 本仓库 AUTOSAR MCAL |
|---|---|---|
| 时钟配置 | `pllctlv2_init_pll_with_freq()` + `clock_set_source_divider()`，参数直接写在应用里 | `Mcu_Init(&Mcu_Config)` + `Mcu_InitClock(0)`，参数在 `Mcu_PBcfg.c` |
| 复位 | `ppor_sw_reset(HPM_PPOR, n)` | `Mcu_PerformReset()`，带 UNINIT 检查与配置化的复位延时 |
| 复位源 | 直接读 `HPM_PPOR->RESET_FLAG` 并自己解释位含义 | `Mcu_GetResetReason()` 返回 AUTOSAR 枚举，原始值另给 `Mcu_GetResetRawValue()` |
| 引脚 | `gpio_set_pin_output()`（inline）+ 手写 IOC 寄存器 | `Port_Init(&Port_Config)`，16 个引脚一张表，支持运行时改方向/模式/上下拉 |
| 电平 | `gpio_write_pin()`（inline，~5 cycles） | `Dio_WriteChannel()`（127 ns ≈ 61 cycles，含 DET 与查表） |
| 错误处理 | 无（参数错就是跑飞） | Det 统一上报，可查询计数；量产可关掉 |
| 可移植性 | 换芯片改应用 | 换芯片只改 `*_PBcfg.c` |

### 44.11.2 与 Infineon TC4x MCAL 2.20.0 对照

本地 `C:\AURIX_TC4x_MC-ISAR_2.20.0\MCAL\Mcu` 的头文件里，API 形状与本实现一致：
`Mcu_Init(const Mcu_ConfigType*)`、`Mcu_InitClock(Mcu_ClockType)`、`Mcu_GetPllStatus()` 返回
`Mcu_PllStatusType`（MCU_PLL_LOCKED/UNLOCKED/UNDEFINED）、`Mcu_PerformReset()`、`Mcu_SetMode(Mcu_ModeType)`。
差异在硬件抽象层：TC4x 的时钟配置由 `Mcu_ClockSettingConfig` 描述 PLL/CCU/分频，寄存器在 SCU；
复位源读 `SCU_RSTSTAT`，AUTOSAR 映射表由厂商提供。本实现把"时钟配置"简化为 PLL 参数 + 节点表两张表，
 等价但更薄；TC4x 有更多低功耗模式（Standby 等），本实现只做了 RUN/SLEEP/RESET 三档。

### 44.11.3 与 NXP S32K3 RTD 6.0.0 对照

本地 `D:\NXP\SW32K3_S32M27x_RTD_R21-11_6.0.0\eclipse\plugins\Mcu_TS_*` 里，`Mcu_InitClock` 的入参同样是
`Mcu_ClockType`（配置集索引），`Mcu_GetResetReason` 从 `MC_RGM` 的 `DES` 寄存器取复位源；
Port 在 S32K3 上对应 SIUL2 的 MSCR/IMCR，Dio 对应 PDDR/PSOR/PCOR（写 1 置位、写 1 清零的分离寄存器）。
和本实现相比，S32K3 的 Port 配置项更多（输入滤波、引脚锁定、安全模式），Dio 的端口位宽是 16 位；
HPM5E31 的 GPIO 是 32 位端口、DO 有 SET/CLEAR/TOGGLE 三写口，所以 `Dio_WriteChannel` 不用做读改写，
天然原子，这一点比 S32K3 的 `PSOR/PCOR` 更方便。

三家共同点：**Mcu 管时钟/复位、Port 管引脚、Dio 管电平**的职责划分完全一致，API 名字也一致，
所以上层代码（ECU 抽象、IoHwAb）可以跨芯片复用；差别只在配置表和寄存器映射。

## 44.12 参考来源

- AUTOSAR CP R25-11《Specification of MCU Driver》（Document ID 31）§7.2、§8.2、§8.3、图 9.1；
  本地文本 `autosar_txt/AUTOSAR_CP_SWS_MCUDriver.txt`。
- AUTOSAR CP R25-11《Specification of Port Driver》（Document ID 40）§7.2 表 7.1、§8.3。
- AUTOSAR CP R25-11《Specification of DIO Driver》（Document ID 20）§7.6、§8.3。
- AUTOSAR 官网：<https://www.autosar.org/>（CP R25-11 发布页）。
- HPM5E31 用户手册 PLLCTLV2 / SYSCTL / PPOR / IOC / GPIO 章节（本地 `先楫官方 Doc 包（DS/RM/Errata）`）。
- HPM SDK v1.13.0：`soc/HPM5E00/HPM5E31/hpm_clock_drv.c`、`soc/HPM5E00/ip/hpm_pllctlv2_regs.h`、
  `soc/HPM5E00/ip/hpm_ioc_regs.h`；上游 <https://github.com/hpmicro/hpm_sdk>。
- 先楫半导体官网：<https://www.hpmicro.com/>（HPM5E31 数据手册/勘误）。
- Infineon TC4x MCAL 2.20.0（本地 `C:\AURIX_TC4x_MC-ISAR_2.20.0\MCAL\Mcu|Port|Dio`）；
  上游 ILLD/MCAL 说明 <https://github.com/Infineon/illd_release_tc4x>。
- NXP S32K3 RTD 6.0.0（本地 `D:\NXP\SW32K3_S32M27x_RTD_R21-11_6.0.0\eclipse\plugins\Mcu_TS_*|Port_TS_*|Dio_TS_*`）；
  上游 <https://www.nxp.com/design/software/development-software/automotive-software-and-tools/real-time-drivers-rtd>。
- 板级时钟定义：`boards/hpm5e31_1/clock.c`（`init_board_clock_source`）、`boards/hpm5e31_1/board.h`（`BOARD_CPU_FREQ`）。
- 例程与日志：`samples/mcal/mcu_clock_init`、`samples/mcal/mcu_reset_mode`、`samples/mcal/port_pin_config`、
  `samples/mcal/dio_led_button`；`logs/20261102-134734..140549` 下同名日志。

## 44.13 待确认项与风险

| 编号 | 状态 | 说明 |
|---|---|---|
| 44-R1 | 已实测 | 480 MHz / 240 MHz 两档主频：479,997,500 Hz（-5 ppm）/ 239,997,720 Hz（-9 ppm），复跑命令见 §44.7 |
| 44-R2 | 已实测 | 软件复位链路：`raw=0x10` → `raw=0x80000000`（MCU_SW_RESET），日志 logs/20261102-140030 |
| 44-R3 | 已实测 | 回环 2002 组图案 0 误码；上下拉、开漏 PASS；Dio 单次写 127 ns、读 185 ns |
| 44-R4 | 已定位 | `Mcu_SetMode(MCU_MODE_SLEEP)`（WFI）未上板验证：需要先配置唤醒源，而本板 GPTMR0/1 已分配给第 45 章 Gpt 例程；
复跑命令 `bash scripts/hwrun.sh mcal/mcu_clock_init --secs 20 --send 'mset 0'` 只能验证 RUN 路径，SLEEP 待与 Gpt 例程合并后再测 |
| 44-R5 | 已知限制 | `Mcu_InitRamSection` / `Mcu_GetRamState` 未实现：HPM5E31 无 RAM 内容有效标志，且启动代码负责清零；
实现路径：读 `PGPR` 备份寄存器/自建 RAM 魔数，约 30 行代码，见 Mcu_Cfg.h 注释。复跑命令：`bash scripts/build.sh samples/mcal/mcu_clock_init`（编译期开关可验） |
| 44-R6 | 已知限制 | PLL 重配只在参数不同时发生；若目标频率与当前相同但上层期望"强制重锁"，当前实现会跳过（幂等），
需要强制路径时改 `mcu_apply_pll` 的判据。复跑命令：`bash scripts/hwrun.sh mcal/mcu_clock_init --secs 20 --send 'mset 1;mset 0'` |
| 44-R7 | 已实测 | 通道组 GPIOF[23:16] 无法覆盖全部 8 颗 CAN 灯（PF26/PF19 不在组内），例程用逐通道写补足，README 已写明 |
| 44-R8 | 已知限制 | 低功耗（Standby/PMIC）模式未实现：需要 PCFG/PDGO 寄存器级支持与板级 DCDC 配合，属于后续轮次范围 |
