- [hpm5e31\_lite Projects](#hpm5e31_lite-projects)
  - [公共板级信息](#公共板级信息)
  - [公共构建方法](#公共构建方法)
  - [各工程测试结果](#各工程测试结果)
    - [hpm5e31\_hello\_world](#hpm5e31_hello_world)
    - [hpm5e31\_coremark](#hpm5e31_coremark)
    - [hpm5e31\_gpio](#hpm5e31_gpio)
    - [hpm5e31\_cherryusb\_cdc\_acm\_vcom](#hpm5e31_cherryusb_cdc_acm_vcom)
    - [hpm5e31\_rmii\_dp83848](#hpm5e31_rmii_dp83848)
  - [板子购买](#板子购买)


# hpm5e31_lite Projects

本目录当前包含 8 个基于自定义板 hpm5e31_lite 的 HPM5E31 工程：

| 工程目录 | 参考来源 |
| --- | --- |
| hpm5e31_hello_world | hpm_sdk/samples/hello_world |
| hpm5e31_coremark | hpm_sdk/samples/coremark |
| hpm5e31_gpio | hpm_sdk/samples/drivers/gpio |
| hpm5e31_cherryusb_cdc_acm_vcom | hpm_sdk/samples/cherryusb/device/cdc_acm/cdc_acm_vcom |
| hpm5e31_rmii_dp83848 | hpm_sdk/samples/lwip/lwip_iperf + lwip_udpecho |

## 公共板级信息

- 板卡名：hpm5e31_lite
- MCU：HPM5E31IPB1
- 外部晶振：24MHz 无源晶振
- 调试串口：UART0
- UART0 引脚：PA00 TXD，PA01 RXD
- PC 调试串口：COM13
- RGMII 双板联调串口：旧板 COM13，新板 COM61
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
- flash 脚本已预置 JTAGConf -1,-1 自动探测参数，当前板子下载时不再停在 JTAGConf 提示符等待手工回车。
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
- USB CDC ACM 枚举：通过
- 当前代码行为：下载后会初始化 USB0 设备 CDC ACM，并在 UART0 打印启动日志。
- 当前实测结果：修正板级 USB 初始化后，Windows 已枚举出新的 USB CDC ACM 虚拟串口 COM62。

```text
COM1   通信端口
COM13  JLink CDC UART Port
COM62  USB 串行设备
```

- 设备信息：VID_34B7，PID_FFFF，PNPDeviceID 为 USB\VID_34B7&PID_FFFF&MI_00\7&A4712DB&1&0000。
- DTR 联调：主机打开 COM62 并拉起 DTR 后，设备端成功返回首字节 h，说明 CDC 数据通路已经打通。
- 修复点：boards/hpm5e31_lite/board.c 中启用了 usb_phy_using_internal_vbus()，并增加了 usb_phy_disable_dp_dm_pulldown()，解决了设备侧 VBUS 感知和 DP/DM 下拉导致的未枚举问题。

### hpm5e31_rmii_dp83848

- Debug 构建：通过（DLM 71.7%，FLASH 12.9%）
- Debug 烧录：通过
- 调试器与串口：J-Link，UART0 对应 COM13
- 工程用途：HPM5E31 通过 ENET0 RMII 连接外部 DP83848IVV，提供静态 IP 192.168.0.100、UDP echo、iperf TCP/UDP 测试
- 硬件连线：PF03/04/07/08/09/10 + PA30/PA31 + PF14(reset) + **PF02 外部 50MHz 时钟**
- 时钟方案：DP83848 模块板载 50MHz 有源晶振，输出同时连接 DP83848 和 HPM5E31 PF02（共享时钟）
- PHY 地址：1（DP83848 默认地址）
- 软件修复：
  - `src/ethernetif_local.c`：TX pbuf 链连续拷贝（原 SDK 只拷贝最后一段）、RX OOM 清理
  - `src/lwiperf_local.c`：tcp_err 悬空 PCB、settings 分片累计、UDP close 超时清理、pbuf_clone OOM
  - `board.c`：外部时钟模式下仍配置 PLL2→50MHz 供 MAC 总线
- 实测结果：
  - PHY 初始化：通过（`Enet phy init passed !`）
  - 链路协商：100Mbps Full duplex（自动协商）
  - **ping：5/5，0% 丢失，<1ms RTT** ✅
  - **UDP echo（端口 7）：通过** ✅
  - **iperf UDP 10 Mbps：10.0 Mbps，0% 丢包，0.000ms jitter** ✅
  - **iperf UDP 60 Mbps：60.0 Mbps，0% 丢包** ✅
  - **iperf UDP 85 Mbps：84.6 Mbps** ✅（接近 100Mbps 线速上限）
  - **iperf TCP：94.6 Mbps** ✅（改进前 ~134 kbps，提升 700 倍）
- 测试总结：

| 测试项 | 改进前 | 改进后 |
|--------|--------|--------|
| Ping 丢包 | 10~20%（ARP+频偏） | 0% |
| UDP 10 Mbps 丢包 | 10.4% | 0% |
| UDP 50 Mbps | 板复位 | 49.3 Mbps 稳定 |
| TCP 带宽 | ~134 kbps | **94.6 Mbps** |
- 详细说明：见 [hpm5e31_rmii_dp83848/README.md](hpm5e31_rmii_dp83848/README.md)

## 板子购买

【闲鱼】https://m.tb.cn/h.RIyl3ab?tk=VjKRgSvZf7l CZ009 「我在闲鱼发布了【先楫 HPM5E31IPB1 最小系统板, 手贴了几块, 已】」
点击链接直接打开