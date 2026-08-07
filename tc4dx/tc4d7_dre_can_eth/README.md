# tc4d7_lwip_iperf

这个工程基于 tc4d7_lwip_ping 改造，用于在 KIT_A3G_TC4D7_LITE 上验证 lwIP 静态 IP + iperf TCP 吞吐测试。

工程特点：

- 保留 UART0 printf 输出，串口参数为 115200-8-N-1。
- 通过 I2C0 读取板上 24AA02E48 EEPROM 中的 EUI-48 作为以太网 MAC 地址。
- 使用 TC4D7 Lite Kit 的 GETH0 Port0 + RMII + DP83825I。
- 使用 lwIP 2.2.1，NO_SYS 模式，静态 IP 为 192.168.0.100。
- 板端启动 lwIP 自带的 iperf TCP server，默认监听 5001 端口。

## 目录来源

- lwIP 栈源码来自 tc4dx/ref/lwip-2.2.1。
- TC4D7 以太网底层初始化参考 tc4dx/ref/iLLD_TC4D7_lite_kit_ADS_ETH_Demo。
- EEPROM MAC 读取逻辑参考 tc4dx/tc4d7_i2c_eeprom_eui。
- iperf 应用参考 tc4dx/ref/contrib-2.1.0/examples/lwiperf 以及 lwIP 2.2.1 自带的 src/apps/lwiperf。

## 网络参数

- IP 地址：192.168.0.100
- 子网掩码：255.255.255.0
- 网关：192.168.0.1

## 硬件连接

默认按 TC4D7 Lite 板卡原生连接使用：

- GETH0 Port0
- RMII
- PHY：DP83825I
- EEPROM：24AA02E48，I2C0，SCL=P13.1，SDA=P13.2
- 调试串口：UART0，TX=P14.0，RX=P14.1

## 构建环境

已按工程脚本默认值配置以下工具：

- CMake 3.24 或更新版本
- Ninja
- AURIX GNU Toolchain
- AURIXFlasher.exe（如需下载）

build.ps1 的默认工具链路径为：

- C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin

如果你的安装路径不同，可在命令中覆盖 ToolchainBin 参数。

## 构建步骤

在工程目录 tc4dx/tc4d7_lwip_iperf 下执行：

```powershell
.\build.ps1 -Action rebuild -BuildType Debug
```

如果只需要普通构建：

```powershell
.\build.ps1 -Action build -BuildType Debug
```

如果需要 Release：

```powershell
.\build.ps1 -Action rebuild -BuildType Release
```

生成物位于 build 目录，关键文件包括：

- tc4d7_lwip_iperf.elf
- tc4d7_lwip_iperf.hex
- tc4d7_lwip_iperf.map

## 下载到板卡

若本机已安装 AURIXFlasher，并且默认路径有效，可直接执行：

```powershell
.\build.ps1 -Action download -BuildType Debug
```

或一步完成构建加下载：

```powershell
.\build.ps1 -Action all -BuildType Debug
```

如果默认下载工具路径不对，可以手动指定：

```powershell
.\build.ps1 -Action download -BuildType Debug -FlashTool "你的AURIXFlasher.exe路径"
```

## 运行与测试

1. 上电后打开串口终端，设置为 115200-8-N-1。
2. 复位板卡。
3. 观察串口日志，正常情况下会打印：
   - 启动横幅
   - EEPROM MAC 读取结果
   - 实际使用的 MAC 地址
  - iperf server 启动提示
  - 链路建立后的速率和双工信息
4. 将 PC 网口配置到同一网段，例如：
   - IP：192.168.0.10
   - 子网掩码：255.255.255.0
   - 网关：192.168.0.1
5. 将 PC 与板卡通过交换机或直连方式连接。
6. 在 PC 侧使用 iperf2 或兼容模式的 iperf 客户端执行：

```powershell
iperf -c 192.168.0.100 -p 5001 -t 10
```

如果使用 iperf3，请切换到兼容 iperf2 的工具，lwIP 自带 lwiperf 不是 iperf3 协议实现。

7. 如需反向观察多次测试结果，可重复执行：

```powershell
iperf -c 192.168.0.100 -p 5001 -i 1 -t 10
```

如果链路协商正常，板卡将作为 TCP server 接收 PC 发起的 iperf 会话，并在串口输出每次测试的统计结果。

## 串口日志说明

常见输出含义：

- EEPROM MAC read succeeded.
  表示已从 24AA02E48 成功读取 EUI-48。
- EEPROM MAC read failed, using fallback MAC address.
  表示 EEPROM 访问失败，工程回退到本地管理 MAC 地址继续启动网络。
- lwIP started, waiting for link and iperf TCP clients.
  表示协议栈已初始化完成，iperf TCP server 已启动并等待客户端连接。
- lwIP iperf server started on TCP port 5001.
  表示板端已成功开始监听默认 iperf 端口。
- ETH link up: 100M full duplex.
  表示 DP83825I 已完成协商，GETH MAC 已切换到对应速率和双工模式。
- ETH link down.
  表示当前网线拔出或链路协商失效，GETH 收发已被停止。
- iperf result: server done, remote ...
  表示一次 iperf TCP 测试完成，串口打印了总字节数、持续时间和平均带宽。

## 故障排查

如果 iperf 连不上或带宽异常，优先检查以下项目：

1. PC 与板卡是否在同一网段。
2. 网线、交换机和供电是否正常。
3. DP83825I 链路是否已经协商成功。
4. 串口日志是否显示 EEPROM 或 PHY 初始化失败。
5. 本机防火墙是否阻止 ICMP。
6. 构建是否使用了正确的工具链与 linker script。
7. PC 侧是否使用 iperf2/兼容客户端，而不是 iperf3。

## 当前实现备注

- 当前版本的以太网底层继承自 tc4d7_lwip_ping，保留了已验证可工作的静态 IP、DP83825I 和 EEPROM MAC 读取路径。
- 以太网初始化阶段没有调用 GETH AXI SRAM 的 VMT clear 流程。
- 原因是当前板卡/工程组合下，AXI MBIST clear 会导致启动卡死，绕过该步骤后 GETH0 Port0 + lwIP 工作正常。
- iperf 当前以 TCP server 方式运行在 5001 端口，适合让 PC 作为 client 发起吞吐测试。

## 实现说明

- 主循环采用 NO_SYS 轮询模式：
  - Ifx_Lwip_pollTimerFlags()
  - Ifx_Lwip_pollReceiveFlags()
- 1 ms 周期由 STM0 中断推进，用于驱动 lwIP 定时器。
- PHY 链路状态每 100 ms 轮询一次，链路状态变化时同步更新 GETH MAC 速率和双工模式。
- 应用层通过 lwiperf_start_tcp_server_default() 启动 lwIP iperf TCP server。