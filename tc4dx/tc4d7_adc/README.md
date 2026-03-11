# tc4d7_adc

`tc4d7_adc` 是基于 `tc4d7_uart_printf_echo` 改出来的 TC4D7 Lite ADC 测试工程。它保留 UART0 `printf` 输出，移除了回显逻辑，并增加了两类采样：

- 外部模拟量：AN0 上的 250K 电位器，经 TMADC0 Channel 0 连续采样
- 内部温度：MCU PMS Die Temperature Sensor，周期读取内部温度结果

程序上电后会每 500 ms 通过 UART0 打印一次 AN0 原始码、占空比/比例、电压估算值，以及内部温度原始码与摄氏度值。

## 1. 功能概览

- UART0：P14.0 TX, P14.1 RX, 115200-8-N-1
- AN0：使用 TMADC0 Channel 0 连续采样
- 内部温度：使用 PMS DTS（Die Temperature Sensor）
- 日志周期：500 ms

串口输出示例：

```text
************************************
*        TC4D7 ADC Monitor         *
************************************
UART0 TX=P14.0 RX=P14.1, 115200-8-N-1
AN0 uses TMADC0 Channel 0.
Internal temperature uses PMS DTS.
Sampling every 500 ms. Rotate the potentiometer connected to AN0.
AN0 raw=2048  ratio= 50.01%  voltage=2.501 V  DTS raw=1462  temp=32.68 C
```

## 2. ADC 工作原理

### 2.1 AN0 外部采样

本工程参考了官方示例 `ref/iLLD_TC4D7_LK_ADS_ADC_Single_Channel` 和 `ref/iLLD_TC4D7_LK_ADS_TMADC_Single_Channel`。虽然目录名里一个写 ADC、一个写 TMADC，但从示例实现可以看到，TC4D7 Lite 上这个单通道模拟输入路径实际使用的是 `IfxAdc_Tmadc` 驱动。

本工程采用的配置是：

- 模块：TMADC0
- 通道：Channel 0
- 引脚：AN0
- 模式：continuous 连续转换
- 结果寄存器：Result Register 0
- 采样时间：100 ns

初始化完成后，TMADC 持续更新通道结果，主循环只需要读取结果寄存器即可。

打印的几个字段含义：

- `raw`：12 位原始 ADC 码，范围 0 到 4095
- `ratio`：将原始码归一化到 0% 到 100%
- `voltage`：用 `raw / 4095 * 5.0V` 估算出的 AN0 电压

说明：

- 这里的电压换算默认按 5.0 V 满量程估算，适合 TC4D7 Lite 板上电位器接到 AN0、量程接近板级模拟参考的典型场景
- 如果你的硬件实际模拟参考电压不是 5.0 V，可以在 `AdcMonitor.c` 里修改 `ADCMONITOR_POTI_REFERENCE_VOLTAGE`

### 2.2 内部温度传感器

TC4D7 的内部温度不是走 TMADC 通道，而是 PMS 子系统里的 DTS 模块。当前工程直接使用 TC4xx iLLD 头文件中已经提供的 PMS DTS 接口：

- `IfxPmsPm_dtsEnable()`：使能 DTS 模拟电路
- `IfxPmsPm_dtsStartAdcConversion()`：启动转换
- `IfxPmsPm_getDtsStatus()`：查询 DTS 是否 ready
- `IfxPmsPm_getDtsTemperatureResult()`：读取原始结果
- `IfxPmsPm_getDtsTemperatureResultKv()`：读取 Kelvin 值

本工程在启动 DTS 后会丢弃前两次结果，再开始打印温度值。这和官方 DTS 驱动文档中的说明一致，有利于避开上电初始阶段的不稳定测量。

温度换算使用的是 TC4xx iLLD 在 `IfxPmsPm.h` 中给出的转换常数：

$$
T_K = \frac{DTS\_RAW}{4.781}
$$

再转换成摄氏度：

$$
T_C = T_K - 273.15
$$

因此打印出来的 `temp` 是根据 TC4xx PMS DTS 官方换算关系得到的摄氏度值。

## 3. 软件结构

- `Cpu0_Main.c`
  - 初始化串口
  - 调用 `AdcMonitor_init()`
  - 周期调用 `AdcMonitor_sample()` 并用 `printf` 打印结果
- `AdcMonitor.c`
  - 封装 TMADC 和 PMS DTS 的初始化与采样
- `Components/serialio/serialio.c`
  - 为 GCC/HighTec 提供 `write()` 重定向，因此 `printf` 直接输出到 UART0

## 4. 构建方法

工程目录：`tc4dx/tc4d7_adc`

### 4.1 配置

```powershell
./build.ps1 -Action configure -BuildType Release
```

### 4.2 编译

```powershell
./build.ps1 -Action build -BuildType Release
```

### 4.3 重新编译

```powershell
./build.ps1 -Action rebuild -BuildType Release
```

### 4.4 下载到板卡

```powershell
./build.ps1 -Action download -BuildType Release
```

### 4.5 一次完成构建和下载

```powershell
./build.ps1 -Action all -BuildType Release
```

生成产物默认位于：

- `build/tc4d7_adc.elf`
- `build/tc4d7_adc.hex`

## 5. 运行步骤

1. 将 250K 电位器连接到 AN0
2. 连接 TC4D7 Lite 开发板
3. 构建并下载固件
4. 打开串口终端，参数设置为 `115200-8-N-1`
5. 复位开发板
6. 观察串口输出，并旋转电位器

## 6. 结果判读

### 6.1 电位器读数

- 顺时针或逆时针旋转时，`raw` 和 `ratio` 应随之变化
- `ratio` 接近 0% 说明 AN0 接近低电平
- `ratio` 接近 100% 说明 AN0 接近高电平

### 6.2 温度读数

- 上电最初一两个周期，可能会看到 `DTS warming up`
- 稳定后会显示 `temp=xx.xx C`
- 这个温度是芯片内部结温，通常高于环境温度

## 7. 与参考工程的关系

本工程主要参考以下官方代码路径：

- `ref/iLLD_TC4D7_LK_ADS_ADC_Single_Channel`
- `ref/iLLD_TC4D7_LK_ADS_TMADC_Single_Channel`

实际实现上做了以下调整：

- 去掉 LED 阈值演示，改成 UART 周期打印
- 采用连续采样，主循环直接读取结果
- 增加 PMS DTS 内部温度读取
- 保留 `printf` 作为统一调试输出接口

## 8. 可调参数

如果你需要调整输出或采样行为，可以修改 `AdcMonitor.c` 里的几个宏：

- `ADCMONITOR_POTI_REFERENCE_VOLTAGE`
  - AN0 电压换算参考值，默认 3.3 V
- `ADCMONITOR_TEMP_WARMUP_SAMPLES`
  - DTS 预热丢弃样本数，默认 2

以及 `Cpu0_Main.c` 里的：

- `REPORT_PERIOD_MS`
  - 串口打印周期，默认 500 ms

## 9. 当前实现边界

- AN0 电压使用固定满量程电压换算，适合当前测试用途，但不等同于严格校准测量
- 内部温度读取采用 PMS DTS 官方换算关系，适合监控和功能验证
- 如果后续需要更高精度，可以进一步增加板级参考电压校准和多次平均滤波