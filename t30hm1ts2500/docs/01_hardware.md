# 硬件说明 — S2500_1 最小系统板

原理图: `docs/datasheet/S2500_1_refsch.pdf`

> **目标读者**：硬件工程师、现场接线人员。
> **读完能做到**：正确接线、正确判断 LED、正确量端接阻抗、看懂上电时序。

原理图文件：`docs/datasheet/S2500_1_refsch.pdf`。
芯片数据手册：`docs/datasheet/T30HM1TS2500-D.PDF`（文本版 `T30HM1TS2500-D.txt` 便于搜索）。

## 本篇目录

| 章节 | 内容要点 |
| --- | --- |
| 1 | 关键器件（BOM 级） |
| 2 | P1 排针定义（2×5） |
| 3 | LED 与 DIO 功能码 |
| 4 | 其它引脚（IRQ / RST / WAKE / INH / CLK_O） |
| 5 | 上电时序 |
| 6 | 与 MCU 的电平匹配 |
| 7 | 上板检查清单（10 项） |
| 8 | 常见错误与排查 |

---

## 1. 关键器件

| 器件 | 型号 | 说明 |
| --- | --- | --- |
| MAC-PHY | T30HM1TS2500MWTWG (QFNW20 4×4) | 10BASE-T1S + MAC, OA-SPI |
| 晶振 | X322525MOB4SI (25 MHz) | XI/XO, C6/C7 = 18 pF |
| 共模扼流圈 | ACT1210E-241-2P | 240 Ω @100 MHz, 可选贴装 |
| 耦合电容 | 100 nF/50 V ×2 (C10/C11) | 隔直 |
| 终端电阻 | R12/R13 = 49.9 Ω | **终端节点使用**，中间节点应拆除 |
| 电源滤波 | FB1 BLM18PG121SN1D + 4.7 µF + 100 nF | VCC |
| 连接器 | J1 `c50126v-4p0g56` | T1S_P / T1S_N |
| 调试排针 | P1 2×5 2.54 mm | 见下表 |

## 2. P1 引脚 (2×5, 从丝印 1 脚开始)

| Pin | 信号 | Pin | 信号 |
| --- | --- | --- | --- |
| 1 | CS_N | 2 | MISO |
| 3 | 3V3 | 4 | SCLK |
| 5 | IRQ_N | 6 | DIO1 (绿 LINK) |
| 7 | DIO0 (黄 ACT) | 8 | RST_N |
| 9 | GND | 10 | MOSI |

> 与各 MCU 的接线见下表。注意 MOSI/SCLK 的位置在 P1 上是 10/4 脚，
> 与部分“标准 SPI 排针”顺序不同。

### 2.1 各 MCU 侧接线（2026-11-20 当前平台）

| MCU | CS | MISO | SCLK | MOSI | IRQ | RST | SPI 外设 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **STM32H723ZGT6** | **PG10** | **PG9** | **PG11** | **PD7** | **PD5** | **PD6** | **SPI1** | 当前 STM32 平台；Cortex-M7 550 MHz；USART1 PA9/PA10 115200；25 MHz HSE |
| HPM5361 | PA26 | PA28 | PA27 | PA29 | PA12 | PA10 | SPI1 | RISC-V 480 MHz；UART2 115200 |
| S32K312 | PTB5 | PTB4 | PTD15 | PTD16 | PTE9 | PTD14 | LPSPI0 | 默认 **15.14 MHz 硬件 SPI**（软件 SPI 可回退）；LPUART0 115200 |
| TC387 | P15.2 | P15.7 | P15.8 | P15.6 | P15.5 | P15.4 | QSPI2 | TriCore 300 MHz；ASCLIN0 115200 |
| RK3506 | GPIO0_B3 | GPIO0_B2 | GPIO0_B0 | GPIO0_B1 | GPIO0_A6 | GPIO0_A2 | Linux SPI | UART 1.5 Mbit/s；t1s0 = 192.168.10.15 |

> **历史平台（已停用）**：STM32F407 的接线为 CS=PA15 / MISO=PC11 / SCLK=PC10 / MOSI=PC12 /
> IRQ=PD0 / RST=PD1（SPI3）。该板已物理损坏，2026-11-20 起由 STM32H723 接替；旧接线仅用于
> 核对历史测试记录，**不要照此接线新板**。

## 3. LED

| LED | 颜色 | 引脚 | 限流 | 极性 | 默认功能 |
| --- | --- | --- | --- | --- | --- |
| LED2 | 黄 (KT-0805Y) | DIO0 | R2 = 2.2 kΩ | **高电平点亮** | ACT：TX/RX 活动 (FN=0xF) |
| LED1 | 绿 (KT-0805G) | DIO1 | R1 = 4.7 kΩ | **高电平点亮** | LINK：PLCA 状态 (FN=0x5) |

驱动通过 `DIOCFG0` 配置：`VALx = 1` 使能高电平有效。
可用 shell 切换：

```
s2500 led 15 5     # DIO0 = TX&RX 活动, DIO1 = PLCA 状态
s2500 led 4 4      # 两路都显示 Link Control
s2500 led off
```

DIO 功能码见 `s2500_regs.h` 的 `S2500_DIOFN_*`（Table 11）。

> 无 PLCA 网络时，DIO1 的 PLCA 状态恒为 0，绿灯不亮；此时建议 FN=0x4 (Link Control)。

## 4. 其它

* **IRQ_N**: R3 = 10 kΩ 上拉到 3V3，低有效，开漏输出。
* **RST_N**: R9 = 10 kΩ 上拉到 3V3，低有效；模块无复位芯片，上电靠内部 POR。
* **WAKE**: T1 连接器第 13 脚经 R7/R8/C8/C9 与 SW1 到 VBAT，可做本地唤醒测试。
* **INH**: 未使用（悬空）。
* **CLK_O**: 25 MHz 输出，默认由 `TWEAKS2.CLKO_ENA` 使能；本仓库默认关闭以降低 EMI。
* **VBAT**: 板上直接接 3V3（R6 = 0 Ω），因此 Sleep 模式电流测试仅在 3.3 V 下有效。

## 5. 上电时序

VCC/VIO 上电后器件进入 Standby → Boot（约 5.3 ms）→ Normal；
`STATUS0.RESETC` 置 1 表示可配置。驱动 `s2500_init()` 会等待该位。

## 6. 与 MCU 的电平匹配

S2500 的 VIO 与 SPI 电平必须与 MCU 一致。本板 VIO 直接接 3V3，
所有 MCU 均为 3.3 V IO，可直接连接。若 MCU 为 1.8 V IO，需电平转换。

---

## 7. 上板检查清单

新板第一次上电，按顺序做，**任何一步不符合都先停下来**：

| # | 检查项 | 方法 | 期望 |
| --- | --- | --- | --- |
| 1 | 电源无短路 | 断电，万用表量 3V3 对 GND | > 1 kΩ（不为 0） |
| 2 | 上电电压 | 上电后量 VCC / VIO | 3.3 V ± 5% |
| 3 | 电流 | 串联电流表 | 典型几十 mA 量级，不应发烫 |
| 4 | 晶振起振 | 示波器探 XI/XO（10× 探头） | 25 MHz 正弦/方波 |
| 5 | 复位释放 | 量 RST_N 与 3V3 | 上电后应为高（10 kΩ 上拉） |
| 6 | SPI 通断 | 万用表通断档量 P1 到 MCU 引脚 | 6 根线全通，无短路 |
| 7 | 总线阻抗 | 断电，量 T1S_P ↔ T1S_N | 约 50 Ω（两端各 100 Ω 并联） |
| 8 | 端接是否过量 | 同上，多板在线时 | < 20 Ω = 端接过多，拆中间节点 |
| 9 | LED | 上电后 `s2500 led 15 5` | 黄灯随流量闪，绿灯 PLCA 同步后常亮 |
| 10 | 芯片可访问 | `s2500 reg 0 0` | 0x00000011 |

## 8. 常见错误与排查

| 现象 | 可能原因 | 处置 |
| --- | --- | --- |
| 上电后电流很大 / 发烫 | 电源反接、VCC-VIO 短路、芯片焊反 | 立即断电，量 VCC/VIO 对 GND |
| IDVER 读到 0xFFFFFFFF | MISO 未接；VIO 没上电；CS_N 一直高 | 量 MISO；量 VIO；示波器看 CS |
| IDVER 读到 0，偶尔正常 | 上电时序太快，芯片还在 Boot | 上电后延时 ≥ 6 ms 再访问，或等 RESETC=1 |
| 黄灯不亮 | DIO0 功能码没设；LED 极性设反 | `s2500 led 15 5`；确认 `VALx=1`（高电平点亮） |
| 绿灯不亮 | 没有 PLCA（DIO1 FN=0x5 时无 PLCA 就恒 0） | 改用 FN=0x4（Link Control），或开 PLCA |
| 总线完全不通 | 端接过量（最常见）；P/N 接错；线断 | 量 50 Ω；换一对线；见 §7 第 7–8 项 |
| SQI 只有 0–1 | 端接过多导致阻抗过低 | 只保留总线两端 R12/R13 |
| HDD 报 SHORT_OPEN=2 | 同上（"多余终端"） | 同上 |
| HDD 报 1W_SHORT=1 | 单线对地/单线短路，或门限敏感 | 断电量单线对地电阻 |
| 睡眠后叫不醒 | 唤醒音/WAKE 通路没接；VBAT 实际是 3V3 | 见 [`docs/11_test_examples.md`](11_test_examples.md) 例 14 |
| 板子发热但能通信 | 25 MHz 时钟输出使能（`CLKO_ENA`）驱动外部负载 | `s2500 analog ... 0 ...` 关掉 CLK_O |

## 9. 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 总线规划与端接规则 | [`docs/02_bus_plan.md`](02_bus_plan.md) |
| SPI 事务格式 | [`docs/03_spi_protocol.md`](03_spi_protocol.md) |
| 现场排障（总线/串口/调试器） | [`docs/10_hardware_troubleshooting.md`](10_hardware_troubleshooting.md) |
| 命令手册 | [`docs/08_shell_commands.md`](08_shell_commands.md) |
