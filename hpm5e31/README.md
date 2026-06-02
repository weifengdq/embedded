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
- 当前代码配置：源码当前已恢复到 fixed-link 100Mbps 全双工、BOARD_ENET_RGMII_TX_DLY=0、BOARD_ENET_RGMII_RX_DLY=0 的稳定基线；本地 SDK 的 lwiperf.c 保留了 TCP 24-byte settings header 分片修复和当前轮次的 recv/close/PCB 诊断，ethernetif.c 的 low_level_output() 也已改为把整条 pbuf 链完整拷入 DMA buffer，避免链式 pbuf 时仅保留最后一段 payload。
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
	- 进一步对比后确认，baremetal ethernetif 原始 TX 路径对链式 pbuf 的处理有问题；当一个 TCP 报文由多个 pbuf 组成时，descriptor 最终只会保留最后一段 payload。修正为“整条 pbuf 链完整复制到 DMA buffer”后，A->B 的 TCP 症状从早期 abort/空转显著改善为可正常结束。
	- tools/serial_iperf_test.ps1 也已从“固定等待 1.5s”改为“明确等待 server 打出 Iperf session started. 再启动 client”，并在 TCP 模式下额外等待 server 侧 EOF/report，避免把脚本竞态误判为协议栈问题。
	- 调试过程中还发现，lwiperf 的 tcp_err() 回调不能继续访问已失效的 conn_pcb；修正这一路径后，前面那种“reset flag/status 仍为 0，但程序直接跳回启动 banner”的自引入破坏已明显收敛。
	- A(COM13) -> B(COM61) 最新复测结果：client 侧可正常结束，type=1，remote_port=5001，total_bytes=20464，duration_ms=10548，kbits_per_s=8；server 侧已经能稳定看到 `tcp accept` 和连续的 `tcp recv data`，说明 server receive path 已经真正跑起来，而不是停在 accept 之前。
	- B(COM61) -> A(COM13) 最新复测结果：client 侧仍为 type=2，remote_port=5001，total_bytes=10244，duration_ms=9047，kbits_per_s=8。
	- 同一轮 client 侧 lwIP PCB 诊断仍显示：`state=ESTABLISHED(4)`，`snd_wnd=11680`，并同时存在 `unsent=1`、`unacked=1`；目前更像是 B->A 方向在少量 MSS 窗口后仍会卡在 ACK/发送推进上，而不是最初那个“24-byte header 分片”或“脚本竞态”问题。
	- 当前结论：100Mbps TCP 这条线已经确认了两个真实问题并修正掉了它们：`low_level_output()` 的链式 pbuf TX 缺陷，以及调试版 lwiperf tcp_err() 对悬空 PCB 的访问；剩余问题已经收敛为“吞吐很低，且 B->A 方向仍会在 ESTABLISHED 状态下卡住”。
- 1Gbps 调优结果：
	- TX_DLY=0、RX_DLY=7 时，两块板都能稳定打印 Link Speed 1000Mbps，说明 1Gbps fixed-link 已能起链。
	- 在旧的 100Mbps UDP 发送速率下，A(COM13) -> B(COM61) 可完成：type=7，remote_port=5001，total_bytes=120879640，duration_ms=10501，kbits_per_s=92090；这只能证明 1Gbps 配置没有立刻失稳，还不能代表 1Gbps 真正吞吐能力。
	- 将 UDP client 发送速率提高到 800Mbps 后，当前已试的四组 delay 组合 `TX/RX=0/7`、`7/7`、`15/7`、`15/15` 都无法稳定跑完；症状表现为 client 侧或双侧在 `Iperf session started.` 后重新回到启动菜单。
	- 在完成 TX pbuf-chain 修正后，又补做了一轮更保守的 1Gbps 阈值验证：临时切到 `TX/RX=0/7`、UDP 200Mbps、A(COM13) -> B(COM61)，结果仍然是 client/server 双侧回到启动 banner、`report not found`。这说明 1Gbps 不稳定并不只是“800Mbps 发送率太激进”，而是在更低负载下也仍然存在。
	- 上述 1Gbps 200Mbps 试验结束后，源码已经恢复回当前 100Mbps 基线，避免仓库停留在临时试验配置。
	- 这些 1Gbps 高负载失败场景里，PPOR reset flag/status 仍为 0，自定义 exception_handler 也没有抓到 machine-mode trap，说明当前 delay 小矩阵尚未找到可支撑高吞吐稳定传输的组合。
- 结论：当前双板 RGMII MAC-to-MAC 的可用稳定档仍是 100Mbps fixed-link 双向 UDP；100Mbps TCP 这轮已经确认并修正了两个实际缺陷，但剩余症状仍是“吞吐很低，且 B->A 会在 ESTABLISHED 状态下卡住”；1Gbps fixed-link 虽然已经能起链，但在完成 TX 修正后，`0/7` 即使把 UDP 发送率降到 200Mbps 也依旧不稳定，因此后续除了继续扩大 RGMII delay 搜索范围，还需要继续深挖高负载下为何会无 trap/无复位地重新回到启动流程。

## 当前目录状态

- hpm5e31_hello_world 的板级目录已重命名为 boards/hpm5e31_lite
- 已删除原 boards/hpm5e31_lite/doc 目录
- 已删除原 boards/hpm5e31_lite/README_en.rst
- 已删除原 boards/hpm5e31_lite/README_zh.rst
