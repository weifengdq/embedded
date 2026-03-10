# tc4d7_i2c_eeprom_eui

这个工程将复制来的 UART echo 示例改成了 KIT_A3G_TC4D7_LITE 板载 24AA02E48 EEPROM 测试工程，使用 I2C0 在 400 kHz 下完成以下动作：

- 读取预编程的 EUI-48 MAC 地址
- 在 EEPROM 可写区域执行写入和回读校验
- 统计 EEPROM 写入吞吐与读取吞吐
- 恢复测试区域的原始内容，避免长期覆盖用户数据

## 硬件背景

板载器件是 Microchip 24AA02E48，容量 2 Kbit，也就是 256 字节。

- I2C 从地址固定为 0x50
- 上半区 0x80 到 0xFF 为永久写保护区
- MAC 地址存放在 0xFA 到 0xFF
- 下半区 0x00 到 0x7F 可读写
- 页写大小为 8 字节，跨页写入必须拆分

本工程实际使用的引脚如下：

- P13.1: I2C0_SCL
- P13.2: I2C0_SDA

对应的 TC4D7 pin map 符号如下：

- `IfxI2c0_SCL_P13_1_INOUT`
- `IfxI2c0_SDA_P13_2_INOUT`

## 软件设计

主逻辑位于 [Cpu0_Main.c](Cpu0_Main.c)，上电后只执行一次测试流程，然后停在空循环中，方便通过调试器观察结果。

EEPROM 访问封装位于 [I2cEepromEui.c](I2cEepromEui.c) 和 [I2cEepromEui.h](I2cEepromEui.h)。实现要点如下：

- 使用 `IfxI2c_I2c` 高层驱动初始化 I2C0
- 设备地址按 iLLD 约定左移一位，`0x50` 转成 `0xA0`
- 随机读采用“先写内部地址，再发起读”的两步流程
- 页写按 8 字节页边界拆包
- 每次页写后用 ACK polling 等待内部写周期完成
- 性能统计使用 STM 计时，结果换算成字节每秒

## 测试流程

测试区选择为 0x20 到 0x3F，这段区域在可写下半区内，不会碰到写保护区和 MAC 地址区。

启动后按顺序执行：

1. 初始化 I2C0，速率 400 kHz
2. 从 0xFA 读取 6 字节 MAC 地址到 `g_macAddr`
3. 备份 0x20 到 0x3F 的原始数据
4. 生成 32 字节测试模式并写入 EEPROM
5. 回读同一区域并比较，得到读写校验结果
6. 连续执行多轮页写，统计写入吞吐
7. 连续执行多轮随机读，统计读取吞吐
8. 将测试区恢复为原始内容，并验证恢复结果

## 主要调试变量

下载运行后，可以在调试器里重点观察这些全局变量：

- `g_macAddr[6]`: 板载 EUI-48 地址
- `g_readWriteTestPassed`: 单次写入回读校验结果
- `g_benchmarkPassed`: 吞吐测试是否完整执行
- `g_restorePassed`: 测试区恢复是否成功
- `g_allTestsPassed`: 总结果
- `g_writeBenchmarkBytesPerSecond`: 写入吞吐
- `g_readBenchmarkBytesPerSecond`: 读取吞吐
- `g_writeBenchmarkTimeUs`: 写入耗时
- `g_readBenchmarkTimeUs`: 读取耗时
- `g_readyPollCount`: 写周期 ACK polling 累计次数
- `g_lastI2cStatus`: 最近一次 I2C 操作状态
- `g_lastEepromAddress`: 最近一次访问的 EEPROM 内部地址

如果读取成功，`g_macAddr` 应该会看到类似 `44 B7 D0 C9 46 98` 这样的 6 字节值，不同板子会不同。

## 与官方示例的关系

参考工程 [ref/iLLD_TC4D7_LK_ADS_I2C_Read_Ext_Device/README.md](../ref/iLLD_TC4D7_LK_ADS_I2C_Read_Ext_Device/README.md) 只演示了从 0xFA 读取 MAC 地址。

这个工程在官方示例基础上增加了：

- 可写区域页写封装
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

这个工程不再依赖 UART 输出，运行结果通过全局变量观察。

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