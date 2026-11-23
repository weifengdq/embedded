# mcu_clock_init —— AUTOSAR Mcu 时钟/PLL 初始化与主频实测

一句话：用 Mcu_Init / Mcu_InitClock / Mcu_GetPllStatus / Mcu_DistributePllClock 配 HPM5E31 的
PLL0 与 CPU 时钟节点，并用 mchtmr(24 MHz 固定) + rdcycle 实测 CPU 主频、给出 ppm 误差。

## 硬件要求 / 接线

| 项目 | 要求 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31，24 MHz XTAL，标称 480 MHz） |
| 串口 | CH343 /dev/ttyACM0 @921600 8N1（USR 串口） |
| 外部接线 | 无（纯片内时钟测试） |
| 注意 | 晶振/PLL 参数只在 `mcal/Mcu/cfg/Mcu_PBcfg.c` 里，改板改那里 |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/mcu_clock_init
bash scripts/hwrun.sh mcal/mcu_clock_init --secs 30 \
     --send 'mclock;mset 1;mclock;mmeasure;mset 0;mmeasure;mver;merr'
```

| 命令 | 作用 |
|---|---|
| `mcu` | 模块信息 + 可用命令 |
| `mclock` | PLL0 寄存器（MFI/MFN/MFD/DIV）与时钟树（cpu0/ahb0/xpi0/mchtmr0），Mcu_GetClockNodeHz 对照 SDK `clock_get_frequency` |
| `mmeasure [us]` | 5 次实测主频，打印均值/最小/最大/ppm 误差 |
| `mset 0\|1` | `Mcu_InitClock(MCU_CLOCK_SETTING_n)` + `Mcu_DistributePllClock()`：0=480 MHz，1=240 MHz（CPU 节点 /2） |
| `mver` | `Mcu_GetVersionInfo` |
| `merr` | 故意触发 3 条 Dev Error，打印 Det 计数增量 |

## 实测现象（串口输出）

```text
=== mcu_clock_init: AUTOSAR Mcu clock/PLL on HPM5E31 ===
Mcu_InitClock(0) -> E_OK
Mcu_GetPllStatus() -> MCU_PLL_LOCKED
Mcu_DistributePllClock() -> E_OK
PLL0: status=LOCKED MFI=40 MFN=0 MFD=240000000 DIV0=5(/2.0) DIV1=7(/2.4)
node      Mcu_GetClockNodeHz   SDK clock_get_frequency
cpu0       480000000 Hz         480000000 Hz
ahb0       200000000 Hz         200000000 Hz
xpi0       100000000 Hz         100000000 Hz
mchtmr0     24000000 Hz          24000000 Hz   (固定 24 MHz)
MEAS: core avg=479997500 Hz min=479996300 max=479997800 cfg=480000000 Hz err=-5 ppm
letter:/$ mset 1
Mcu_InitClock(1) -> E_OK, pll=LOCKED
after switch: cfg=240000000 Hz, sdk=240000000 Hz
MEAS: core avg=239997720 Hz min=239997400 max=239997800 cfg=240000000 Hz err=-9 ppm
letter:/$ mset 0
MEAS: core avg=479997400 Hz min=479995800 max=479997800 cfg=480000000 Hz err=-5 ppm
DET: before=0 after=3 delta=3
```

日志：
- `logs/20261102-135140-mcu_clock_init.log`（480 MHz + 240 MHz 切换，本文引用）
- `logs/20261102-134845-mcu_clock_init.log`（重复性验证）
- `logs/20261102-134734-mcu_clock_init.log`（首次跑通）

## 关键点

- **PLL 只在参数变化时才写**：写 PLL0.MFI 会触发重新锁定，若 CPU 正跑在 PLL0 上会短暂停摆；
  驱动里先比对 MFI/MFN/MFD，只有不同才写（见 `Mcu.c:mcu_apply_pll`），`mset 0` 切回 480 MHz 才稳定。
- **Mcu_DistributePllClock 的语义**：HPM5E31 没有独立的"PLL 分配门"，分配由 SYSCTL 时钟节点 MUX 完成；
  驱动按 SWS_Mcu_00142 先查锁定状态，未锁定立即返回 E_NOT_OK 并报 `MCU_E_PLL_NOT_LOCKED`。
- **测量方法**：mchtmr 固定 24 MHz（`init_board_clock_source` 里 `clock_mchtmr0 = osc24m/1`），
  在一个 10 ms 的 mchtmr 窗口内数 rdcycle，`f = cycles / (ticks/24e6)`；窗口越长越准，10 ms 下 5 次重复性 ±2 kHz。
- 误差主要来自 mchtmr 与 CPU 的时钟源不同（24 MHz 晶振 vs PLL0 倍频），-5 ppm 即约 -2.4 kHz，属于晶振精度量级。
- `-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0` 关掉 BSP 自己的时钟打印，避免与本例程输出混淆。

## 待确认项

- 已实测：480 MHz 与 240 MHz 两档主频、PLL 锁定状态、Det 错误路径（3 条）。
- 已知限制：`Mcu_SetMode(MCU_MODE_SLEEP)`（WFI）没有在例程里演示（需要先配唤醒源），代码保留实现。
