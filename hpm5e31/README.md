# hpm5e31_lite Projects

本目录当前包含 5 个基于自定义板 hpm5e31_lite 的 HPM5E31 工程：

| 工程目录 | 参考来源 |
| --- | --- |
| hpm5e31_hello_world | hpm_sdk/samples/hello_world |
| hpm5e31_coremark | hpm_sdk/samples/coremark |
| hpm5e31_gpio | hpm_sdk/samples/drivers/gpio |
| hpm5e31_cherryusb_cdc_acm_vcom | hpm_sdk/samples/cherryusb/device/cdc_acm/cdc_acm_vcom |
| hpm5321_rgmii_mac_to_mac | hpm_sdk/samples/lwip/lwip_iperf |

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

### hpm5321_rgmii_mac_to_mac

- Debug 构建：Role A 通过，Role B 通过
- Debug 下载：通过
- 工程用途：两块 HPM5E31 自定义板通过 ENET0 RGMII 做 MAC-to-MAC 直连，基于 lwIP iperf 做双板互通和吞吐测试。
- 当前代码配置：已恢复到 fixed-link 100Mbps 全双工、BOARD_ENET_RGMII_TX_DLY=0、BOARD_ENET_RGMII_RX_DLY=0，便于继续在稳定基线上排查 TCP；本地 SDK 的 lwiperf.c 还带有 TCP 24-byte settings header 分片修复，以及本轮新增的 TCP PCB/ethernetif 诊断探针。
- 当前稳定基线：fixed-link 100Mbps 全双工；这是当前唯一完成双向 UDP 实测且稳定复现的档位。
- 默认角色/IP 规划：
	- Role A：192.168.10.13
	- Role B：192.168.10.61
- 实测板卡映射：
	- Role A：J-Link SN 24060502，调试串口 COM13
	- Role B：J-Link SN 1120000012，调试串口 COM61
- 参考命令：

```powershell
./build.ps1 -Action rebuild -Config Debug -Role A
./build.ps1 -Action rebuild -Config Debug -Role B
./tools/serial_iperf_test.ps1 -Protocol udp -OutputPath C:/Windows/Temp/hpm5e31_udp_report.txt
./tools/serial_iperf_test.ps1 -Protocol udp -ClientBoard B -OutputPath C:/Windows/Temp/hpm5e31_udp_b_to_a_report.txt
./tools/serial_iperf_test.ps1 -Protocol tcp -OutputPath C:/Windows/Temp/hpm5e31_tcp_report.txt
./tools/serial_iperf_test.ps1 -Protocol tcp -ClientBoard A -OutputPath C:/Windows/Temp/hpm5e31_tcp_a_to_b_report.txt
```

- 100Mbps 基线结果：两块板都能稳定打印 Link Status Up、Link Speed 100Mbps、静态 IP 正确。
- 100Mbps UDP 实测结果：
	- A(COM13) -> B(COM61)：type=7，remote_port=5001，total_bytes=119760606，duration_ms=10501，kbits_per_s=91237
	- B(COM61) -> A(COM13)：type=7，remote_port=5001，total_bytes=120558312，duration_ms=10501，kbits_per_s=91845
- 100Mbps TCP 调试进展：
	- 已在本地 SDK 的 lwiperf.c 中修正 TCP server 对 24-byte settings header 分片的处理；修正前会出现 server type=3、client type=5，且 total_bytes=24 的早期退出。
	- 新增 PPOR reset 和自定义 exception_handler 诊断后，仍未观察到硬复位来源或 machine-mode trap，TCP 失败路径可以继续收敛到 lwIP 会话本身。
	- B(COM61) -> A(COM13) 最新复测结果：client 侧为 type=2，remote_port=5001，total_bytes=10974，duration_ms=9172，kbits_per_s=8。
	- 同一轮 client 侧 lwIP PCB 诊断显示：`state=ESTABLISHED(4)`，`snd_wnd=11680`，`snd_buf=706`，`snd_queuelen=9`，并同时存在 `unsent=1`、`unacked=1`；ethernetif 计数显示 `tx_busy=0`，实际只发出 11 帧 / 3598 字节、收到 7 帧 / 420 字节，说明问题已经从“DMA 队列忙”缩小为“连接已建立，但有一个未确认段和一个未发送段长期卡在 TCP 队列里”。
	- A(COM13) -> B(COM61) 最新复测结果：client 侧可正常结束，type=1，remote_port=5001，total_bytes=13164，duration_ms=10500，kbits_per_s=8；但 server 侧仍以 type=2 超时结束，remote_port=49153，total_bytes=1608，duration_ms=9511，kbits_per_s=0。
	- 同一轮 server 侧 lwIP PCB 诊断显示：`state=ESTABLISHED(4)`，`snd_buf=11680`，`snd_queuelen=0`，`unsent=0`，`unacked=0`；ethernetif 计数显示 server 侧实际收到了 21 帧 / 8160 字节，但 lwiperf 只累计到 1608 字节，说明有相当一部分数据已进入 lwIP/netif 层，却没有让 server iperf 会话继续完整推进。
	- 当前结论：100Mbps TCP 已基本排除“描述符拿不到”“硬复位”“机器态异常”这三类问题，现阶段更像是 TCP 会话在 ESTABLISHED 状态下卡在 ACK/FIN 或 server receive progression 的上层处理问题。
- 1Gbps 调优结果：
	- TX_DLY=0、RX_DLY=7 时，两块板都能稳定打印 Link Speed 1000Mbps，说明 1Gbps fixed-link 已能起链。
	- 在旧的 100Mbps UDP 发送速率下，A(COM13) -> B(COM61) 可完成：type=7，remote_port=5001，total_bytes=120879640，duration_ms=10501，kbits_per_s=92090；这只能证明 1Gbps 配置没有立刻失稳，还不能代表 1Gbps 真正吞吐能力。
	- 将 UDP client 发送速率提高到 800Mbps 后，当前已试的四组 delay 组合 `TX/RX=0/7`、`7/7`、`15/7`、`15/15` 都无法稳定跑完；症状表现为 client 侧或双侧在 `Iperf session started.` 后重新回到启动菜单。
	- 这些 1Gbps 高负载失败场景里，PPOR reset flag/status 仍为 0，自定义 exception_handler 也没有抓到 machine-mode trap，说明当前 delay 小矩阵尚未找到可支撑高吞吐稳定传输的组合。
- 结论：当前双板 RGMII MAC-to-MAC 的可用稳定档仍是 100Mbps fixed-link 双向 UDP；100Mbps TCP 现在已经缩小到“ESTABLISHED 状态下 TCP 队列/receive progression 卡死”的问题面；1Gbps fixed-link 虽然已经能起链，但本轮试过的 `0/7`、`7/7`、`15/7`、`15/15` 都不能支撑 800Mbps UDP 稳定运行，后续需要继续扩大 RGMII delay 搜索范围，或进一步深挖 TCP/高负载下上层协议推进为何停住。

## 当前目录状态

- hpm5e31_hello_world 的板级目录已重命名为 boards/hpm5e31_lite
- 已删除原 boards/hpm5e31_lite/doc 目录
- 已删除原 boards/hpm5e31_lite/README_en.rst
- 已删除原 boards/hpm5e31_lite/README_zh.rst
