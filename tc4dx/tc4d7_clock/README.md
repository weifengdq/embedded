# tc4d7_clock

这个工程从 tc4d7_uart_printf_echo 复制而来，保留 UART0 printf 输出能力，并将主循环改为周期性打印 TC4D7 当前时钟链路和常用外设主时钟。

## 功能

- 外部 25 MHz 无源晶振输入说明
- 打印 XTAL -> SYSPLL -> 系统时钟源 的链路
- 打印 CPU、SRI、SPB、FSI、STM 等系统派生时钟
- 打印 SPI、I2C、UART、CAN、GETH 的根时钟和模块时钟

## 输出说明

串口输出会周期性打印两部分内容：

1. System clock chain
   - XTAL / fOSC0
   - PLL 输入源
   - SYSPLL VCO 频率
   - SYSPLL 输出频率
   - 当前系统时钟源与 CPU、SRI、SPB、FSI、STM 频率
2. Peripheral root and kernel clocks
   - PERPLL1 / PERPLL2 / PERPLL3
   - QSPI、I2C、ASCLIN、MCAN、GETH 的根时钟与模块时钟

## 构建

在工程目录执行：

```powershell
.\build.ps1 -Action rebuild -BuildType Release
```

下载到板卡：

```powershell
.\build.ps1 -Action download -BuildType Release
```

## 串口

- UART0 TX: P14.0
- UART0 RX: P14.1
- 波特率: 115200
- 格式: 8-N-1