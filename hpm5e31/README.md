# hpm5e31_lite Projects

本目录当前包含 4 个基于自定义板 hpm5e31_lite 的 HPM5E31 工程：

| 工程目录 | 参考来源 |
| --- | --- |
| hpm5e31_hello_world | hpm_sdk/samples/hello_world |
| hpm5e31_coremark | hpm_sdk/samples/coremark |
| hpm5e31_gpio | hpm_sdk/samples/drivers/gpio |
| hpm5e31_cherryusb_cdc_acm_vcom | hpm_sdk/samples/cherryusb/device/cdc_acm/cdc_acm_vcom |

## 公共板级信息

- 板卡名：hpm5e31_lite
- MCU：HPM5E31IPB1
- 外部晶振：24MHz 无源晶振
- 调试串口：UART0
- UART0 引脚：PA00 TXD，PA01 RXD
- PC 调试串口：COM13
- 用户 LED：PA02，高电平点亮
- 用户按键：PA03，按下高电平
- 调试器：JLink
- 默认 JLink 路径：C:\Program Files\SEGGER\JLink_V844
- 默认 SDK 路径：D:\hpm\embedded\hpm5e31\sdk_env_v1.11.0\hpm_sdk

每个工程都带有独立的 boards/hpm5e31_lite 板级目录和 build.ps1 脚本，不需要修改 SDK 本体。

## 公共构建方法

在任意工程目录下使用 PowerShell 执行：

```powershell
.\build.ps1 -Action configure -Config Debug
.\build.ps1 -Action build -Config Debug
.\build.ps1 -Action rebuild -Config Debug
.\build.ps1 -Action rebuild -Config Release
.\build.ps1 -Action flash -Config Debug
.\build.ps1 -Action gdbserver -Config Debug
.\build.ps1 -Action clean -Config Debug
.\build.ps1 -Action clean -Config Release
```

说明：

- Debug 和 Release 对应不同的构建目录。
- clean 只清理当前 Config 对应的构建目录；如果 Debug 和 Release 都清理，需要各执行一次。
- flash 默认下载当前 Config 对应的 demo.elf。
- gdbserver 会启动 JLinkGDBServerCL，用于外部 GDB 调试。

如果 SDK 或 JLink 路径变化，可覆盖脚本参数：

```powershell
.\build.ps1 -Action rebuild -Config Debug -SdkDir 'D:\path\to\hpm_sdk' -JLinkDir 'C:\Program Files\SEGGER\JLink_V844'
```

## 各工程测试结果

### hpm5e31_hello_world

- Debug 构建：通过
- Release 构建：通过
- Debug 下载：通过
- 串口验证：通过
- 实测结果：向 COM13 发送 copilot，串口成功回显 copilot

### hpm5e31_coremark

- Debug 构建：通过
- Release 构建：通过
- Debug 下载：通过
- 串口验证：通过
- 实测结果：COM13 抓到 CoreMark 输出，结果如下

```text
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 306424088
Total time (secs): 12.767670
Iterations/Sec   : 2349.684728
Iterations       : 30000
Compiler version : GCC13.2.0
Correct operation validated.
CoreMark 1.0 : 2349.684728 / GCC13.2.0 / STACK
```

- 备注：Debug 和 Release 构建时都出现了上游 CoreMark 源码的 maybe-uninitialized 编译警告，但不影响链接和运行。

### hpm5e31_gpio

- Debug 构建：通过
- Release 构建：通过
- Debug 下载：通过
- 串口验证：部分通过
- 代码行为：
	- 默认未按键时，LED 按 1s 周期翻转
	- 按下 PA03 后，LED 切换为 100ms 周期翻转
	- 松开 PA03 后，恢复为 1s 周期翻转
- 实测结果：COM13 抓到默认状态输出，确认程序启动并持续按 1s 周期输出日志

```text
gpio example on hpm5e31_lite
LED: PA02 active-high, KEY: PA03 active-high
blink period: 1000ms
led on, blink period: 1000ms
led off, blink period: 1000ms
led on, blink period: 1000ms
led off, blink period: 1000ms
```

- 未完成实测：本次没有远程按下实体按键，因此 100ms 快闪分支未做实物观察，但代码已按 PA03 高电平按下逻辑实现。

### hpm5e31_cherryusb_cdc_acm_vcom

- Debug 构建：通过
- Release 构建：通过
- Debug 下载：通过
- USB CDC ACM 枚举：未确认
- 当前代码行为：下载后会初始化 USB0 设备 CDC ACM，并在 UART0 打印启动日志。
- 当前实测结果：Windows 串口枚举列表中只看到 COM13 的 JLink CDC UART Port，没有看到新增的 USB CDC ACM 虚拟串口。

```text
COM1   通信端口
COM13  JLink CDC UART Port
```

- 结论：CherryUSB 工程已完成构建和下载验证，但 USB 虚拟串口未在当前 PC 上枚举成功。优先检查 USB0 数据线是否已连接到当前 PC，以及板上 USB0 口的硬件连接。

## 当前目录状态

- hpm5e31_hello_world 的板级目录已重命名为 boards/hpm5e31_lite
- 已删除原 boards/hpm5e31_lite/doc 目录
- 已删除原 boards/hpm5e31_lite/README_en.rst
- 已删除原 boards/hpm5e31_lite/README_zh.rst
