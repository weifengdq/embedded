# tc4d7_lwip_ping

这个工程基于 tc4d7_uart_printf_echo 改造，用于在 KIT_A3G_TC4D7_LITE 上验证 lwIP 静态 IP + ping。

工程特点：

- 保留 UART0 printf 输出，串口参数为 115200-8-N-1。
- 通过 I2C0 读取板上 24AA02E48 EEPROM 中的 EUI-48 作为以太网 MAC 地址。
- 使用 TC4D7 Lite Kit 的 GETH0 Port0 + RMII + DP83825I。
- 使用 lwIP 2.2.1，NO_SYS 模式，静态 IP 为 192.168.0.100。

## 目录来源

- lwIP 栈源码来自 tc4dx/ref/lwip-2.2.1。
- TC4D7 以太网底层初始化参考 tc4dx/ref/iLLD_TC4D7_lite_kit_ADS_ETH_Demo。
- EEPROM MAC 读取逻辑参考 tc4dx/tc4d7_i2c_eeprom_eui。

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

在工程目录 tc4dx/tc4d7_lwip_ping 下执行：

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

- tc4d7_lwip_ping.elf
- tc4d7_lwip_ping.hex
- tc4d7_lwip_ping.map

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
   - lwIP 启动提示
4. 将 PC 网口配置到同一网段，例如：
   - IP：192.168.0.10
   - 子网掩码：255.255.255.0
   - 网关：192.168.0.1
5. 将 PC 与板卡通过交换机或直连方式连接。
6. 在 PC 侧执行：

```powershell
ping 192.168.0.100
```

如果链路协商正常，板卡将通过 lwIP 的 ICMP 处理流程自动回复 ping。

## 串口日志说明

常见输出含义：

- EEPROM MAC read succeeded.
  表示已从 24AA02E48 成功读取 EUI-48。
- EEPROM MAC read failed, using fallback MAC address.
  表示 EEPROM 访问失败，工程回退到本地管理 MAC 地址继续启动网络。
- lwIP started, waiting for link and ICMP echo requests.
  表示协议栈已初始化完成，正在等待 PHY 链路和 ping 请求。

## 故障排查

如果 ping 不通，优先检查以下项目：

1. PC 与板卡是否在同一网段。
2. 网线、交换机和供电是否正常。
3. DP83825I 链路是否已经协商成功。
4. 串口日志是否显示 EEPROM 或 PHY 初始化失败。
5. 本机防火墙是否阻止 ICMP。
6. 构建是否使用了正确的工具链与 linker script。

## 实现说明

- 主循环采用 NO_SYS 轮询模式：
  - Ifx_Lwip_pollTimerFlags()
  - Ifx_Lwip_pollReceiveFlags()
- 1 ms 周期由 STM0 中断推进，用于驱动 lwIP 定时器。
- PHY 链路状态每 100 ms 轮询一次，链路状态变化时同步更新 GETH MAC 速率和双工模式。