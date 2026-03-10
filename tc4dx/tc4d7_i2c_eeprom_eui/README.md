# tc4d7_i2c_eeprom_eui

这个工程将复制来的 UART echo 示例改成了 KIT_A3G_TC4D7_LITE 板载 24AA02E48 EEPROM 测试工程，默认先初始化 UART0 调试输出，再使用 I2C0 在 400 kHz 下完成以下动作：

- 读取预编程的 EUI-48 MAC 地址
- 在 EEPROM 可写区域执行写入和回读校验
- 统计 EEPROM 写入吞吐与读取吞吐
- 恢复测试区域的原始内容，避免长期覆盖用户数据
- 通过 UART0 默认输出每个阶段和最终结果

如果当前板卡上的 EEPROM 用户区写入被硬件阻止，工程会自动识别为只读模式：

- 保留 MAC 读取功能
- 跳过写入校验和恢复步骤
- 对 0x20 到 0x3F 测试窗口只执行读取吞吐测试
- 串口和全局变量都会明确报告 `read-only mode`

## 硬件背景

板载器件是 Microchip 24AA02E48，容量 2 Kbit，也就是 256 字节。

按数据手册定义：

- I2C 从地址固定为 0x50
- 上半区 0x80 到 0xFF 为永久写保护区
- MAC 地址存放在 0xFA 到 0xFF
- 下半区 0x00 到 0x7F 可读写
- 页写大小为 8 字节，跨页写入必须拆分

按当前 KIT_A3G_TC4D7_LITE 实板测试结果：

- MAC 地址读取稳定成功
- 对 0x20 到 0x3F 的写事务可以收到 ACK
- 但写后读回数据保持不变，仍为原始值
- 因此当前板卡应按“用户区只读”处理，而不是按“下半区可写”假设使用

这说明数据手册描述的是器件通用能力，而当前开发板上的器件状态或板级条件使用户区写入实际上不可用。软件因此实现了自动降级为只读模式，而不是把它误判成 I2C 通讯故障。

本工程实际使用的引脚如下：

- P14.0: UART0_TX
- P14.1: UART0_RX
- P13.1: I2C0_SCL
- P13.2: I2C0_SDA

对应的 TC4D7 pin map 符号如下：

- `IfxI2c0_SCL_P13_1_INOUT`
- `IfxI2c0_SDA_P13_2_INOUT`

## 软件设计

主逻辑位于 [Cpu0_Main.c](Cpu0_Main.c)，上电后会先初始化 UART0，随后只执行一次测试流程，然后停在空循环中，方便同时通过串口和调试器观察结果。

EEPROM 访问封装位于 [I2cEepromEui.c](I2cEepromEui.c) 和 [I2cEepromEui.h](I2cEepromEui.h)。实现要点如下：

- 使用 UART0 115200-8-N-1 默认输出启动信息、阶段信息和结果汇总
- 使用 `IfxI2c_I2c` 高层驱动初始化 I2C0
- 设备地址按 iLLD 约定左移一位，`0x50` 转成 `0xA0`
- 随机读采用“先写内部地址，再发起读”的两步流程
- 写入路径使用更保守的单字节写 + ACK polling，优先保证兼容性
- 如果写事务 ACK 正常但数据保持不变，自动判定为只读模式
- 性能统计使用 STM 计时，结果换算成字节每秒

## 测试流程

测试区选择为 0x20 到 0x3F。按照器件手册，这段区域位于理论可写下半区；但按当前板卡实测结果，这段区域虽然可读，却表现为只读。

启动后按顺序执行：

1. 初始化 I2C0，速率 400 kHz
2. 从 0xFA 读取 6 字节 MAC 地址到 `g_macAddr`
3. 备份 0x20 到 0x3F 的原始数据
4. 生成 32 字节测试模式并尝试写入 EEPROM
5. 回读同一区域并比较，得到读写校验结果
6. 如果写入有效，继续统计写入吞吐与读取吞吐
7. 如果写入无效但读正常，自动切换到只读模式，对 0x20 到 0x3F 只统计读取吞吐
8. 只有在写入成功时才恢复测试区原始内容

## 串口输出

默认串口是 UART0：

- TX: P14.0
- RX: P14.1
- 波特率: 115200
- 格式: 8-N-1

上电后会输出：

- 工程标题和引脚信息
- I2C 初始化结果
- MAC 地址读取结果
- EEPROM 写入回读校验结果
- 吞吐测试结果
- 恢复测试区结果
- 最终汇总信息

## 主要调试变量

下载运行后，可以在调试器里重点观察这些全局变量：

- `g_macAddr[6]`: 板载 EUI-48 地址
- `g_readWriteTestPassed`: 单次写入回读校验结果
- `g_benchmarkPassed`: 吞吐测试是否完整执行
- `g_restorePassed`: 测试区恢复是否成功
- `g_allTestsPassed`: 总结果
- `g_readOnlyModeDetected`: 是否进入只读模式
- `g_writeBenchmarkBytesPerSecond`: 写入吞吐
- `g_readBenchmarkBytesPerSecond`: 读取吞吐
- `g_writeBenchmarkTimeUs`: 写入耗时
- `g_readBenchmarkTimeUs`: 读取耗时
- `g_readyPollCount`: 写周期 ACK polling 累计次数
- `g_lastI2cStatus`: 最近一次 I2C 操作状态
- `g_lastEepromAddress`: 最近一次访问的 EEPROM 内部地址
- `g_mismatchIndex`: 首个失配字节偏移
- `g_expectedByte`: 期望写入值
- `g_actualByte`: 回读实际值
- `g_backupByte`: 写入前原始值

如果读取成功，`g_macAddr` 应该会看到类似 `44 B7 D0 C9 46 98` 这样的 6 字节值，不同板子会不同。

## 与官方示例的关系

参考工程 [ref/iLLD_TC4D7_LK_ADS_I2C_Read_Ext_Device/README.md](../ref/iLLD_TC4D7_LK_ADS_I2C_Read_Ext_Device/README.md) 只演示了从 0xFA 读取 MAC 地址。

这个工程在官方示例基础上增加了：

- 可写区域写入校验与只读模式判定
- 回读一致性校验
- STM 计时测速
- 测试后自动恢复原始数据
- CMake 显式补回被 `.cproject` 排除的 I2C 和 pin map 源文件

最后一点很重要，因为这个目录是从 UART 工程复制出来的，原始 `.cproject` 会把 `Libraries/iLLD/TC4xx/Tricore/I2c` 和 `Libraries/iLLD/TC4xx/Tricore/_PinMap` 排除掉，不显式补回的话，I2C 头文件和 pin map 符号不会参与构建。

## 构建与下载

在工程目录执行：

```powershell
.\build.ps1 -Action rebuild -BuildType Debug
```

下载到开发板：

```powershell
.\build.ps1 -Action download -BuildType Debug
```

如果你需要 Release 版：

```powershell
.\build.ps1 -Action rebuild -BuildType Release
```

## 运行说明

这个工程默认使用 UART 输出结果，同时保留全局变量便于调试器查看。

建议步骤：

1. 上电并下载程序
2. 复位后停在主循环
3. 在调试器里查看 `g_allTestsPassed`
4. 再查看 `g_macAddr`、`g_writeBenchmarkBytesPerSecond`、`g_readBenchmarkBytesPerSecond`

如果 `g_macReadPassed` 为假，优先检查：

- 板子是否为 KIT_A3G_TC4D7_LITE
- I2C0 引脚是否被外设占用
- EEPROM 器件是否正常焊接供电
- 当前构建是否确实使用了 BGA292 COM 的 I2C pin map