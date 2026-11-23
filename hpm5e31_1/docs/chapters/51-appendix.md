# 第 51 章 附录：引脚表、接线、测量方法与日志索引

## 51.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | 全书 |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（表格由脚本从 SDK/日志自动生成） |

## 51.2 本章导览

- 引脚复用速查（本板用到的每个引脚的全部可选功能，含 IOC 宏）；
- 外设基地址与中断号索引（自动生成，见 `docs/generated/soc-peripherals.md`）；
- 硬件接线表与"没有示波器怎么测"的方法学；
- 全部实测日志索引（自动生成）；
- 常用命令速查卡（复制即用）；
- 术语与缩写表。

## 51.3 引脚复用速查

> 由 `tools/gen_appendix_tables.py` 从 `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_iomux.h` 自动生成，
> **不要手改**；ALT 编号即 `IOC_PAD_FUNC_CTL_ALT_SELECT_SET(n)` 的 n。

<!-- BEGIN:PINS -->
| 引脚 | 可选功能（ALT 编号：功能） |
|---|---|
| **PA00** | 0: GPIO_A_00<br>1: GPTMR1_COMP_0<br>2: UART0_TXD<br>7: MCAN0_TXD<br>16: PWM0_P_0<br>17: TRGM_P_00<br>24: SYSCTL_CLK_OBS_0 |
| **PA01** | 0: GPIO_A_01<br>1: GPTMR1_CAPT_0<br>2: UART0_RXD<br>7: MCAN0_RXD<br>16: PWM0_P_1<br>17: TRGM_P_01<br>24: SYSCTL_CLK_OBS_2 |
| **PA02** | 0: GPIO_A_02<br>1: GPTMR1_COMP_1<br>2: UART0_DE<br>3: UART0_RTS<br>4: I2C2_SCL<br>7: MCAN0_STBY<br>16: PWM0_P_2<br>17: TRGM_P_02<br>24: SYSCTL_CLK_OBS_1 |
| **PA03** | 0: GPIO_A_03<br>1: GPTMR1_CAPT_1<br>3: UART0_CTS<br>4: I2C2_SDA<br>7: MCAN1_STBY<br>16: PWM0_P_3<br>17: TRGM_P_03<br>24: SYSCTL_CLK_OBS_3 |
| **PA09** | 0: GPIO_A_09<br>1: GPTMR0_CAPT_1<br>2: UART2_RXD<br>4: I2C0_SDA<br>7: MCAN2_RXD<br>11: ESC0_REFCK<br>16: PWM1_P_1<br>17: TRGM_P_09 |
| **PA19** | 0: GPIO_A_19<br>1: GPTMR3_CAPT_1<br>3: UART4_CTS<br>5: SPI3_CS_3<br>11: ESC0_P0_RXD_2<br>23: SDM0_CLK_2<br>26: EUI0_DO |
| **PA20** | 0: GPIO_A_20<br>3: UART5_CTS<br>5: SPI2_SCLK<br>11: ESC0_P0_RXD_3<br>23: SDM0_DAT_1 |
| **PA21** | 0: GPIO_A_21<br>1: GPTMR3_COMP_2<br>2: UART5_DE<br>3: UART5_RTS<br>5: SPI2_CS_0<br>11: ESC0_P0_RXCK<br>21: QEO0_Z<br>23: SDM0_CLK_1<br>26: OWR0_DAT |
| **PA22** | 0: GPIO_A_22<br>1: GPTMR2_CAPT_0<br>2: UART5_RXD<br>5: SPI2_MISO<br>18: ETH0_TXER<br>21: QEO0_B<br>23: SDM0_DAT_0<br>26: OWR1_DAT |
| **PA23** | 0: GPIO_A_23<br>1: GPTMR2_COMP_0<br>2: UART5_TXD<br>5: SPI2_MOSI<br>11: ESC0_P0_RXER<br>18: ETH0_RXER<br>21: QEO0_A<br>23: SDM0_CLK_0 |
| **PA24** | 0: GPIO_A_24<br>1: GPTMR2_COMP_1<br>2: UART6_TXD<br>5: SPI3_CS_2<br>11: ESC0_P0_TXCK |
| **PA30** | 0: GPIO_A_30<br>2: UART7_RXD<br>5: SPI3_DAT2<br>11: ESC0_MDIO<br>18: ETH0_MDIO<br>20: QEI1_A |
| **PA31** | 0: GPIO_A_31<br>1: GPTMR2_COMP_3<br>2: UART7_TXD<br>5: SPI3_DAT3<br>11: ESC0_MDC<br>18: ETH0_MDC<br>20: QEI1_B |
| **PB28** | 0: GPIO_B_28<br>11: ESC0_CTR_8<br>14: XPI0_CA_D_1<br>20: QEI1_H0<br>23: SDM0_CLK_1<br>26: EUI1_DI |
| **PB29** | 0: GPIO_B_29<br>14: XPI0_CA_SCLK<br>20: QEI1_Z<br>23: SDM0_DAT_1<br>25: ETH0_EVTO_1<br>26: EUI1_DO<br>27: ESC0_EVTO_1 |
| **PC08** | 0: GPIO_C_08<br>1: GPTMR0_COMP_1<br>2: UART2_TXD<br>4: I2C0_SCL<br>5: SPI1_CS_2<br>7: MCAN2_TXD<br>10: ESC0_GPI_55<br>11: ESC0_GPO_08<br>13: PPI0_DQ_31<br>14: XPI0_CA_SCLK<br>16: PWM1_P_0<br>17: TRGM_P_08<br>18: ESC0_GPI_07 |
| **PC14** | 0: GPIO_C_14<br>2: UART3_RXD<br>5: SPI1_DAT2<br>7: MCAN3_RXD<br>10: ESC0_GPI_49<br>11: ESC0_GPO_14<br>13: PPI0_DQ_19<br>16: PWM1_P_6<br>17: TRGM_P_14<br>18: ESC0_GPI_01<br>20: QEI1_B<br>21: QEO1_B<br>27: ESC0_CTR_5 |
| **PC15** | 0: GPIO_C_15<br>1: GPTMR0_COMP_3<br>2: UART3_TXD<br>5: SPI1_DAT3<br>7: MCAN3_TXD<br>10: ESC0_GPI_48<br>11: ESC0_GPO_15<br>13: PPI0_DM_3<br>14: XPI0_CA_D_0<br>16: PWM1_P_7<br>17: TRGM_P_15<br>18: ESC0_GPI_00<br>27: ESC0_CTR_6 |
| **PD00** | 0: GPIO_D_00<br>7: MCAN0_TXD<br>10: ESC0_GPI_31<br>11: ESC0_GPO_32<br>13: PPI0_DQ_02<br>16: PWM0_P_0<br>17: TRGM_P_00<br>18: ESC0_REFCK<br>27: ESC0_CTR_0 |
| **PD01** | 0: GPIO_D_01<br>7: MCAN0_RXD<br>10: ESC0_GPI_30<br>11: ESC0_GPO_33<br>13: PPI0_DQ_03<br>16: PWM0_P_1<br>17: TRGM_P_01<br>18: ESC0_REFCK<br>27: ESC0_CTR_1 |
| **PD04** | 0: GPIO_D_04<br>7: MCAN1_RXD<br>10: ESC0_GPI_27<br>11: ESC0_GPO_36<br>13: PPI0_DQ_10<br>16: PWM0_P_4<br>17: TRGM_P_04<br>20: QEI0_H0<br>27: ESC0_CTR_4 |
| **PD05** | 0: GPIO_D_05<br>7: MCAN1_TXD<br>10: ESC0_GPI_26<br>11: ESC0_GPO_37<br>13: PPI0_DQ_13<br>16: PWM0_P_5<br>17: TRGM_P_05<br>20: QEI0_Z<br>21: QEO0_Z<br>27: ESC0_CTR_5 |
| **PF02** | 0: GPIO_F_02<br>4: I2C2_SCL<br>7: MCAN0_STBY<br>11: ESC0_P1_TXCK<br>16: PWM0_P_2<br>17: TRGM_P_02<br>18: ETH0_TXCK<br>20: QEI0_H1<br>21: QEO1_Z<br>26: EUI0_CK |
| **PF03** | 0: GPIO_F_03<br>4: I2C2_SDA<br>7: MCAN1_STBY<br>11: ESC0_P1_TXD_0<br>16: PWM0_P_3<br>17: TRGM_P_03<br>18: ETH0_TXD_0<br>20: QEI0_F<br>26: EUI0_SH |
| **PF04** | 0: GPIO_F_04<br>7: MCAN1_RXD<br>11: ESC0_P1_TXD_1<br>16: PWM0_P_4<br>17: TRGM_P_04<br>18: ETH0_TXD_1<br>20: QEI0_H0<br>26: EUI0_DI |
| **PF05** | 0: GPIO_F_05<br>7: MCAN1_TXD<br>11: ESC0_P1_TXD_2<br>16: PWM0_P_5<br>17: TRGM_P_05<br>18: ETH0_TXD_2<br>20: QEI0_Z<br>21: QEO0_Z<br>26: EUI0_DO |
| **PF06** | 0: GPIO_F_06<br>4: I2C3_SDA<br>11: ESC0_P1_TXD_3<br>16: PWM0_P_6<br>17: TRGM_P_06<br>18: ETH0_TXD_3<br>20: QEI0_B<br>21: QEO0_B |
| **PF07** | 0: GPIO_F_07<br>4: I2C3_SCL<br>11: ESC0_P1_TXEN<br>16: PWM0_P_7<br>17: TRGM_P_07<br>18: ETH0_TXEN<br>20: QEI0_A<br>21: QEO0_A |
| **PF08** | 0: GPIO_F_08<br>4: I2C0_SCL<br>7: MCAN2_TXD<br>11: ESC0_P1_RXDV<br>16: PWM1_P_0<br>17: TRGM_P_08<br>18: ETH0_RXDV<br>26: EUI1_CK |
| **PF09** | 0: GPIO_F_09<br>4: I2C0_SDA<br>7: MCAN2_RXD<br>11: ESC0_P1_RXD_0<br>16: PWM1_P_1<br>17: TRGM_P_09<br>18: ETH0_RXD_0<br>26: EUI1_SH |
| **PF10** | 0: GPIO_F_10<br>7: MCAN2_STBY<br>11: ESC0_P1_RXD_1<br>16: PWM1_P_2<br>17: TRGM_P_10<br>18: ETH0_RXD_1<br>26: EUI1_DI |
| **PF11** | 0: GPIO_F_11<br>11: ESC0_P1_RXD_2<br>16: PWM1_P_3<br>17: TRGM_P_11<br>18: ETH0_RXD_2<br>26: EUI1_DO |
| **PF12** | 0: GPIO_F_12<br>4: I2C1_SDA<br>11: ESC0_P1_RXD_3<br>16: PWM1_P_4<br>17: TRGM_P_12<br>18: ETH0_RXD_3<br>26: OWR0_DAT |
| **PF13** | 0: GPIO_F_13<br>4: I2C1_SCL<br>7: MCAN3_STBY<br>11: ESC0_P1_RXCK<br>16: PWM1_P_5<br>17: TRGM_P_13<br>18: ETH0_RXCK<br>26: OWR1_DAT |
| **PF14** | 0: GPIO_F_14<br>7: MCAN3_RXD<br>11: ESC0_P1_RXER<br>16: PWM1_P_6<br>17: TRGM_P_14<br>18: ETH0_RXER |
| **PF15** | 0: GPIO_F_15<br>7: MCAN3_TXD<br>16: PWM1_P_7<br>17: TRGM_P_15<br>18: ETH0_TXER |
| **PF16** | 0: GPIO_F_16<br>23: SDM0_DAT_0 |
| **PF18** | 0: GPIO_F_18<br>23: SDM0_DAT_1<br>24: USB0_PWR<br>25: ETH0_EVTO_0<br>27: ESC0_EVTO_0 |
| **PF19** | 0: GPIO_F_19<br>23: SDM0_CLK_1<br>24: USB0_PWR |
| **PF20** | 0: GPIO_F_20<br>23: SDM0_DAT_3<br>24: USB0_OC<br>25: ETH0_EVTO_1<br>27: ESC0_EVTO_1 |
| **PF21** | 0: GPIO_F_21<br>23: SDM0_DAT_2<br>24: USB0_ID<br>25: ETH0_EVTI_0<br>27: ESC0_EVTI_0 |
| **PF22** | 0: GPIO_F_22<br>23: SDM0_CLK_2<br>24: USB0_ID<br>25: ETH0_EVTI_1<br>27: ESC0_EVTI_1 |
| **PF23** | 0: GPIO_F_23<br>23: SDM0_CLK_3<br>24: USB0_OC |
| **PF26** | 0: GPIO_F_26<br>23: SDM0_DAT_1 |
<!-- END:PINS -->

配置示例：

```c
/* 把 PA20 配成 SPI2_SCLK 并开启内部回环（用于无外接回环测试） */
HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL = IOC_PA20_FUNC_CTL_SPI2_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
/* 把 PA24 配成 GPIO 输入并上拉 */
HPM_IOC->PAD[IOC_PAD_PA24].FUNC_CTL = IOC_PA24_FUNC_CTL_GPIO_A_24;
HPM_IOC->PAD[IOC_PAD_PA24].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
```

## 51.4 外设基地址与中断号

完整的实例/基地址/IRQn 表由脚本从 SDK 生成：`docs/generated/soc-peripherals.md`
（`python3 tools/gen_soc_tables.py` 刷新），共 79 个 IRQn。

表 51-1 常用外设速查

| 外设 | 基地址 | 关键 IRQn | 备注 |
|---|---|---|---|
| GPIO0 / FGPIO | 0xF00D_0000 / 0x0003_0000 | IRQn_GPIO0_A~F | FGPIO 无中断 |
| UART0~7 | 0xF004_0000 起，4 KB 步进 | IRQn_UART0=19 … UART7=26 | FIFO 32 字节 |
| SPI0~3 | 0xF007_0000 起 | IRQn_SPI0=31 … SPI3=34 | FIFO 8 深度 |
| I2C0~3 | 见生成表 | IRQn_I2C0=27 … I2C3=30 | FIFO 4 |
| MCAN0~3 | 0xF030_0000 起 | IRQn_MCAN0~3 | 消息 RAM 需放 AHB SRAM |
| GPTMR0~3 | 0xF000_0000 起 | IRQn_GPTMR0=11 … 14 | 32 位，4 通道 |
| PWM0/1 | 0xF042_0000 / 0xF042_4000 | IRQn_PWM0/1 | PWMv2，8 通道/实例 |
| ENET0 | 0xF140_0000 | IRQn_ENET0 | RGMII/RMII/MII |
| USB0 | 0xF142_0000 | IRQn_USB0 | HS OTG |
| ADC0/1 | 0xF010_0000 / 0xF010_4000 | IRQn_ADC0/1 | 16 bit 2 MSPS |
| XDMA / HDMA | 0xF310_0000 / 0xF00C_8000 | IRQn_XDMA / IRQn_HDMA | DS 写各 32 通道 |
| PLIC / MCHTMR | 0xE400_0000 / 0xE600_0000 | — | 中断控制器 / 24 MHz 机器定时器 |
| PLLCTLV2 / SYSCTL / PPOR / PCFG | 0xF40C_0000 等 | — | 时钟/复位/电源 |

## 51.5 接线表与测量方法

先把整板测试连接看清楚，再查单根线。图 51-1 画出了本书实测时主机与板卡之间的全部连接：
J-Link（JTAG）、CH343（调试串口）、USB0（gs_usb / CDC）、10BASE-T1S 适配器、千兆车载以太网转换盒，
以及板上两组 CAN 互连、SPI1 与 SPI3、I2C2 与 I2C3 的杜邦线。

![图 51-1 hpm5e31_1 测试连接总图（主机侧接口与板上互连）](figures/51/fig-51-1-test-setup.png)

**图 51-1** 中红色双向箭头是 120 Ω 终端电阻已接入的 CAN 两两互连，紫色是 SPI 交叉线，
黄色是 I2C 直连线。这些连接让"单板自测"成为可能：CAN 用两个 MCAN 互发、SPI/I2C 用两个控制器互测。

### 51.5.1 接线速查

表 51-2 板级接线（与第 01 章表 1-6 同源，按功能归类）

| 功能 | MCU 引脚 | 对端 | 备注 |
|---|---|---|---|
| 调试串口 | PA00/PA01 | CH343 → /dev/ttyACM0 | 921600 8N1 |
| JTAG | 专用脚 | J-Link | 4 MHz |
| CAN0/CAN1 | PD00/PD01、PD05/PD04 | 同一 CAN 总线（120 Ω×2） | 两节点测试 |
| CAN2/CAN3 | PC08/PA09、PC15/PC14 | 同一 CAN 总线（120 Ω×2） | 两节点测试 |
| LAN8651 | PA20/PA21/PA22/PA23 + PA19/PA24 | 10BASE-T1S 总线 → USB T1S 适配器 | 第 10 章 |
| YT8011AN | PF02~PF13 + PA31/PA30 + PB29/PB28 | 千兆转换盒（1000M Master） | 第 12 章 |
| USB0 | Pin67/Pin68 | 主机 USB | 第 20 章 |
| 状态灯 | PA02 + PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19 | LED（高亮） | 第 04 章 |
| I2C2↔I2C3（自测） | PD02/PD03 ↔ PD07/PD06 | 杜邦线两两相连 | 第 06 章、第 46 章 |
| SPI1↔SPI3（自测） | PC10/PC11/PC12/PC13 ↔ PA26/PA27/PA28/PA29 | 杜邦线两两相连 | 第 07 章、第 46 章 |
| 以太网（YT8011AN） | PF02~PF13、PA31/PA30、PB29/PB28 | 千兆转换盒（1000M Master）→ 主机 enp6s0 | 第 12 章、第 49 章 |
| A核调试/供电 | — | J-Link + USB | 第 02 章 |

### 51.5.2 没有示波器怎么测

| 要测的量 | 方法 | 精度/限制 |
|---|---|---|
| 代码段耗时 | `mcycle` 前后差 × (1/480 MHz) | ±2.08 ns/计数，含流水线影响 |
| 中断延迟 | GPTMR 触发点读 mcycle → ISR 首行读 mcycle 差值 | 统计 min/avg/max + 直方图 |
| 频率/占空比 | 硬件 PWM 输出接 GPTMR 捕获（TRGM 内部路由或飞线） | 受捕获时钟分辨率限制 |
| 串口吞吐/误码 | 对端（主机）发送已知序列，板端校验统计 | 受 USB-UART 缓冲影响 |
| CAN 吞吐/丢帧 | 板内两节点互发，收端计数 + 错误计数器 | 无法观测总线波形 |
| 以太网吞吐 | 主机 iperf3 打流 | 受主机网卡/USB 适配器限制 |
| 功耗 | 串联采样电阻 + 万用表（本板未做） | 【待确认】 |
| 温度 | TSNS 内部温度传感器 | 未校准，仅相对趋势 |

> 时序类"波形图"在本书中均为**示意图**（由 graphviz/matplotlib 生成），不是实测截图；
> 实测数据以串口日志为准。


### 51.5.3 调试引脚占用（JTAG）

表 51-4 JTAG/调试专用引脚（`hpm_iomux.h` ALT=24；本板 J-Link 已接）

| 引脚 | 功能 | 说明 |
|---|---|---|
| PA04 | JTAG_TDO | 调试输出；做 GPIO 会影响调试 |
| PA05 | JTAG_TDI | 调试输入 |
| PA06 | JTAG_TCK | 调试时钟 |
| PA07 | JTAG_TMS | 调试模式选择 |
| PA08 | JTAG_TRST | 调试复位（可选） |

> 结论：**PA04~PA08 在连接 J-Link 调试时不要复用为普通外设**；本书例程均避开了这 5 个脚。
> 若产品不需要 JTAG，可把这 5 个脚释放为 GPIO（本板原理图把它们引到了 2.54 mm 排针，见原理图）。

## 51.6 实测日志索引

> 由脚本扫描仓库 `logs/` 生成；文件名格式 `YYYYMMDD-HHMMSS-<例程>.log`。

<!-- BEGIN:LOGS -->
| 日志文件 | 时间 | 例程 | 大小 |
|---|---|---|---|
| 20261102-181324-gs_usb_candle.log | 2026-11-02 18:13:24 | gs_usb_candle | 229 B |
| 20261102-181330-gs-usb-5m.log | 2026-11-02 18:13:30 | gs-usb-5m | 10966 B |
| 20261102-181447-gs_usb_candle.log | 2026-11-02 18:14:47 | gs_usb_candle | 77 B |
| 20261102-181452-gs-usb-5m.log | 2026-11-02 18:14:52 | gs-usb-5m | 10905 B |
| 20261102-181536-eth_lwip.log | 2026-11-02 18:15:36 | eth_lwip | 3162 B |
| 20261102-181543-dlt_log.log | 2026-11-02 18:15:43 | dlt_log | 206 B |
| 20261102-181700-host-dlt-daemon.log | 2026-11-02 18:17:00 | host-dlt-daemon | 1220 B |
| 20261102-181701-eth_irq.log | 2026-11-02 18:17:01 | eth_irq | 185 B |
| 20261102-181720-wdg_timeout_reset.log | 2026-11-02 18:17:20 | wdg_timeout_reset | 1338 B |
| 20261102-181731-wdg_timeout_reset.log | 2026-11-02 18:17:31 | wdg_timeout_reset | 0 B |
| 20261102-181819-eth_irq-host.log | 2026-11-02 18:18:19 | eth_irq-host | 289 B |
| 20261102-181839-gs_usb_candle.log | 2026-11-02 18:18:39 | gs_usb_candle | 101 B |
| 20261102-181907-gs-usb-5m.log | 2026-11-02 18:19:07 | gs-usb-5m | 2510 B |
| 20261102-182013-gs_usb_candle.log | 2026-11-02 18:20:13 | gs_usb_candle | 28 B |
| 20261102-182016-eth_lwip.log | 2026-11-02 18:20:16 | eth_lwip | 3724 B |
| 20261102-182018-gs-usb-5m.log | 2026-11-02 18:20:18 | gs-usb-5m | 4262 B |
| 20261102-182206-gs_usb_candle.log | 2026-11-02 18:22:06 | gs_usb_candle | 220 B |
| 20261102-182213-gs-usb-tx-urb-leak.log | 2026-11-02 18:22:13 | gs-usb-tx-urb-leak | 2739 B |
| 20261102-182256-gs_usb_candle.log | 2026-11-02 18:22:56 | gs_usb_candle | 127 B |
| 20261102-182303-gs-usb-tx-urb-leak.log | 2026-11-02 18:23:03 | gs-usb-tx-urb-leak | 2673 B |
| 20261102-182304-eth_lwip.log | 2026-11-02 18:23:04 | eth_lwip | 2469 B |
| 20261102-182443-eth_lwip.log | 2026-11-02 18:24:43 | eth_lwip | 2153 B |
| 20261102-182443-t1e-tcp-b2h-host.log | 2026-11-02 18:24:43 | t1e-tcp-b2h-host | 950 B |
| 20261102-182443-t1e-tcp-h2b.log | 2026-11-02 18:24:43 | t1e-tcp-h2b | 2153 B |
| 20261102-182447-eth_irq.log | 2026-11-02 18:24:47 | eth_irq | 494 B |
| 20261102-182513-eth_lwip.log | 2026-11-02 18:25:13 | eth_lwip | 1888 B |
| 20261102-182538-eth_irq-host.log | 2026-11-02 18:25:38 | eth_irq-host | 3774 B |
| 20261102-182633-eth_lwip.log | 2026-11-02 18:26:33 | eth_lwip | 1968 B |
| 20261102-182633-t1f-tcp-b2h-host.log | 2026-11-02 18:26:33 | t1f-tcp-b2h-host | 1172 B |
| 20261102-182633-t1f-tcp-h2b.log | 2026-11-02 18:26:33 | t1f-tcp-h2b | 1968 B |
| 20261102-182715-eth_lwip.log | 2026-11-02 18:27:15 | eth_lwip | 1770 B |
| 20261102-182922-eth_lwip.log | 2026-11-02 18:29:22 | eth_lwip | 2246 B |
| 20261102-182922-t1g-tcp-b2h-host.log | 2026-11-02 18:29:22 | t1g-tcp-b2h-host | 740 B |
| 20261102-182922-t1g-tcp-h2b.log | 2026-11-02 18:29:22 | t1g-tcp-h2b | 2246 B |
| 20261102-182954-eth_lwip.log | 2026-11-02 18:29:54 | eth_lwip | 1839 B |
| 20261102-183145-eth_lwip.log | 2026-11-02 18:31:45 | eth_lwip | 2290 B |
| 20261102-183225-eth_irq-host.log | 2026-11-02 18:32:25 | eth_irq-host | 4997 B |
| 20261102-183321-b2h-1.log | 2026-11-02 18:33:21 | b2h-1 | 1826 B |
| 20261102-183321-b2h-2.log | 2026-11-02 18:33:21 | b2h-2 | 1846 B |
| 20261102-183321-b2h-3.log | 2026-11-02 18:33:21 | b2h-3 | 1829 B |
| 20261102-183321-b2h-4.log | 2026-11-02 18:33:21 | b2h-4 | 1848 B |
| 20261102-183321-b2h-5.log | 2026-11-02 18:33:21 | b2h-5 | 1848 B |
| 20261102-183321-b2h-host-1.log | 2026-11-02 18:33:21 | b2h-host-1 | 1117 B |
| 20261102-183321-b2h-host-2.log | 2026-11-02 18:33:21 | b2h-host-2 | 897 B |
| 20261102-183321-b2h-host-3.log | 2026-11-02 18:33:21 | b2h-host-3 | 1447 B |
| 20261102-183321-b2h-host-4.log | 2026-11-02 18:33:21 | b2h-host-4 | 897 B |
| 20261102-183321-b2h-host-5.log | 2026-11-02 18:33:21 | b2h-host-5 | 897 B |
| 20261102-183321-eth_lwip.log | 2026-11-02 18:33:21 | eth_lwip | 2201 B |
| 20261102-183321-h2b-1.log | 2026-11-02 18:33:21 | h2b-1 | 2201 B |
| 20261102-183321-h2b-2.log | 2026-11-02 18:33:21 | h2b-2 | 2233 B |
| 20261102-183321-h2b-3.log | 2026-11-02 18:33:21 | h2b-3 | 2295 B |
| 20261102-183321-h2b-4.log | 2026-11-02 18:33:21 | h2b-4 | 2230 B |
| 20261102-183321-h2b-5.log | 2026-11-02 18:33:21 | h2b-5 | 2044 B |
| 20261102-183350-eth_lwip.log | 2026-11-02 18:33:50 | eth_lwip | 1826 B |
| 20261102-183422-eth_lwip.log | 2026-11-02 18:34:22 | eth_lwip | 2233 B |
| 20261102-183449-eth_lwip.log | 2026-11-02 18:34:49 | eth_lwip | 1846 B |
| 20261102-183509-eth_irq-host.log | 2026-11-02 18:35:09 | eth_irq-host | 5772 B |
| 20261102-183522-eth_lwip.log | 2026-11-02 18:35:22 | eth_lwip | 2295 B |
| 20261102-183632-eth_lwip.log | 2026-11-02 18:36:32 | eth_lwip | 1829 B |
| 20261102-183705-eth_lwip.log | 2026-11-02 18:37:05 | eth_lwip | 2230 B |
| 20261102-183711-eth_irq-host.log | 2026-11-02 18:37:11 | eth_irq-host | 5777 B |
| 20261102-183731-eth_lwip.log | 2026-11-02 18:37:31 | eth_lwip | 1848 B |
| 20261102-183827-eth_irq-host.log | 2026-11-02 18:38:27 | eth_irq-host | 6139 B |
| 20261102-183847-eth_lwip.log | 2026-11-02 18:38:47 | eth_lwip | 2044 B |
| 20261102-183934-eth_irq-host.log | 2026-11-02 18:39:34 | eth_irq-host | 5771 B |
| 20261102-184001-eth_lwip.log | 2026-11-02 18:40:01 | eth_lwip | 1848 B |
| 20261102-184310-eth_irq-host.log | 2026-11-02 18:43:10 | eth_irq-host | 6569 B |
| 20261102-184416-doip_server.log | 2026-11-02 18:44:16 | doip_server | 793 B |
| 20261102-184421-eth_raw_echo.log | 2026-11-02 18:44:21 | eth_raw_echo | 679 B |
| 20261102-184421-t1-irq.log | 2026-11-02 18:44:21 | t1-irq | 904 B |
| 20261102-184421-t1-rawtx.log | 2026-11-02 18:44:21 | t1-rawtx | 679 B |
| 20261102-184421-t1-stats.log | 2026-11-02 18:44:21 | t1-stats | 0 B |
| 20261102-184421-t1-udp-b2h-host.log | 2026-11-02 18:44:21 | t1-udp-b2h-host | 1249 B |
| 20261102-184455-eth_stats.log | 2026-11-02 18:44:55 | eth_stats | 0 B |
| 20261102-184510-doip_server.log | 2026-11-02 18:45:10 | doip_server | 703 B |
| 20261102-184510-eth_irq.log | 2026-11-02 18:45:10 | eth_irq | 904 B |
| 20261102-184530-eth_lwip.log | 2026-11-02 18:45:30 | eth_lwip | 2338 B |
| 20261102-184610-eth_lwip.log | 2026-11-02 18:46:10 | eth_lwip | 2289 B |
| 20261102-184929-eth_lwip.log | 2026-11-02 18:49:29 | eth_lwip | 2362 B |
| host-acceptance-174321.log | - | - | 3083 B |
<!-- END:LOGS -->

## 51.7 命令速查卡

```bash
# 环境
cd hpm5e31_1 && source scripts/env.sh

# 编译 / 一条龙（编译+烧写+抓串口）
bash scripts/build.sh can/mcan_two_node
bash scripts/hwrun.sh can/mcan_two_node --secs 8 --send "can info"

# 只烧写 / 只抓串口
bash scripts/flash.sh build/mcan_two_node-debug/output/demo.elf
python3 scripts/serial_cap.py --secs 5 --send "ver;clk"

# 新建例程骨架
bash scripts/new_sample.sh can mcan_my_test

# 第七轮新增：Eth MCAL / DoIP / XCP-on-UDP / MQTT
bash scripts/hwrun.sh mcal/eth_raw_echo --secs 20 --send "eth;eth stat"
bash scripts/hwrun.sh mcal/eth_lwip --secs 60 --send "ip;link;iserver tcp"
bash scripts/hwrun.sh app/doip_server --secs 60 --send "doip start;doip stat"
bash scripts/hwrun.sh app/xcp_eth --secs 60 --send "xcp start;xcp stat"
bash scripts/hwrun.sh app/mqtt_telemetry --secs 60 --send "mqtt conn;mqtt stat"

# 文档
python3 tools/check_docs.py --all
bash tools/build_pdf.sh
```

## 51.8 术语与缩写

表 51-3 术语表

| 缩写 | 全称/含义 | 备注 |
|---|---|---|
| MCAN | Bosch M_CAN / CAN FD 控制器 | HPM5E31 有 4 个实例 |
| TC6 | OPEN Alliance 10BASE-T1S MAC-PHY 串行接口规范 | LAN8651 使用 |
| PLCA | Physical Layer Collision Avoidance | 10BASE-T1S 多点总线仲裁 |
| HRPWM | High-Resolution PWM | 100 ps 分辨率 |
| TRGM | Trigger MUX | 外设间触发/信号路由 |
| PLB | Programmable Logic Block | 可编程逻辑单元 |
| SDM | Sigma-Delta Modulator | ΣΔ 位流输出 |
| DMAMUX | DMA 请求路由矩阵 | 把外设请求映射到 DMA 通道 |
| ILM/DLM | Instruction/Data Local Memory | 零等待本地存储 |
| XIP | Execute In Place | 在 Flash 上直接执行 |
| ISP | In-System Programming | 串行启动/下载模式 |
| PPS | Pulse Per Second | 秒脉冲（PTP） |

## 51.9 参考来源

1. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_iomux.h`（引脚复用定义）
2. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_ip.h`、`hpm_soc_irq.h`（基地址/中断号）
3. DS Rev0.4 §2 p.12~（引脚功能描述）
4. 本仓库 `docs/generated/soc-peripherals.md`、`logs/`
5. 芯片/器件官方入口：HPM5E00 <https://www.hpmicro.com/product-center/microcontroller/hpm5e00>、
   LAN8651 <https://www.microchip.com/en-us/product/LAN8651>、
   YT8011AN（裕太微）<https://www.motor-comm.com/>、
   TCAN1044A-Q1 <https://www.ti.com/product/TCAN1044A-Q1>。
6. 工具链/调试器：J-Link <https://www.segger.com/downloads/jlink/>、OpenSSH <https://www.openssh.com/>、
   can-utils <https://github.com/linux-can/can-utils>。
7. 本轮新增章节的规范与上游：
   - DoIP：ISO 13400-2（道路车辆诊断通信 over IP）；AUTOSAR《SWS Diagnostic over IP》
     <https://www.autosar.org/standards/classic-platform/>；参考实现 EcuBus-Pro
     <https://github.com/cherry-embedded/EcuBus-Pro>。
   - XCP on Ethernet：ASAM MCD-1 XCP V1.5（含 XCPonEth 传输层）
     <https://www.asam.net/standards/detail/mcd-1-xcp/>；参考实现 XCPlite
     <https://github.com/eclipse-ecal/XCPlite>、pyxcp <https://github.com/christoph2/pyxcp>。
   - MQTT：OASIS MQTT 3.1.1 / 5.0 规范 <https://mqtt.org/specifications/>；broker
     Eclipse Mosquitto <https://github.com/eclipse/mosquitto>。
   - AUTOSAR Eth/EthTrcv：《SWS Ethernet Driver》《SWS Ethernet Transceiver Driver》
     <https://www.autosar.org/standards/classic-platform/>；lwIP <https://savannah.nongnu.org/projects/lwip/>；
     FreeRTOS <https://github.com/FreeRTOS/FreeRTOS-Kernel>。

8. 先楫官方资料入口：官网 <https://www.hpmicro.com/>、软件开发资料汇总
   <https://www.hpmicro.com/design-resources/development-software/5>、SDK 在线文档
   <https://hpm-sdk.readthedocs.io/zh-cn/latest/>、GitHub <https://github.com/hpmicro>、
   引脚与时钟在线配置 <https://tools.hpmicro.com/>、开发者知识库 <https://kb.hpmicro.com/>、
   EVK 快速上手 <https://mp.weixin.qq.com/s/CJXdNpeDWIzatPawqtclWg>、
   开发视频 <https://space.bilibili.com/592932589>（汇总见第 27 章表 27-12）。
9. 本书源码仓库：<https://github.com/weifengdq/embedded>（`hpm5e31_1/` 子目录）；书稿 PDF 按版本单独分发，不入仓库。

## 51.10 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 项 | 状态 | 说明 |
|---|---|---|
| 功耗测量 | **已知限制** | 无功耗分析仪，本板也未预留采样点。方法：断开板上供电支路串入电流表（或 INA219/INA226 模块），分别测 RUN / WAIT / STOP / PDGO 四种模式，负载取空闲与 CoreMark+4 路 CAN+以太网两档 |
| 波形图 | **已知限制（说明：波形图为示意，非示波器截图）** | 本书时序图均为示意图（脚本绘制），不是示波器截图；真实波形按各章给出的测点与判据自行测量（第 5/6/7/14/15 章有测点表） |
| 日志索引只保留最近 80 条 | **已知限制（说明：日志索引只列最近 80 条）** | `tools/gen_appendix_tables.py` 默认只列最近 80 条；更早日志可从 git 历史找回，或改脚本里的条数上限 |