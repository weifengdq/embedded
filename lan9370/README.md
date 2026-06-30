# LAN9370

- [LAN9370](#lan9370)
  - [板子简介](#板子简介)
  - [STM32H723 基础配置](#stm32h723-基础配置)
    - [实际接线](#实际接线)
    - [CubeMX](#cubemx)
    - [工程说明](#工程说明)
    - [编译和烧录](#编译和烧录)
  - [测试](#测试)
    - [ping](#ping)
    - [iperf](#iperf)
    - [Master Slave](#master-slave)
    - [端口分组](#端口分组)
    - [其它功能说明](#其它功能说明)


## 板子简介

![image-20260630155238889](README.assets/image-20260630155238889.png)

如上图:

- 贴的是 LAN9370-I/KCX, 集成100BASE-T1 PHY的5端口AVB/TSN千兆位以太网交换芯片,  VQFN-64 封装
- 右侧绿色端子:
  - 4x 100BASE-T1
  - 1路外部供电 8V~36V (板子可以用排针的单3V3 或 这个外置电源供电)
- 左侧 2x14P 2.54mm排针:
  - RGMII/RMII
    - RGMII, 如果使用, 建议画板子直接对插上, 不建议任何形式的杜邦线或飞线
    - RMII, 建议 10cm 以内杜邦线
      - RMII 命名和丝印的对应关系: CRS_DV -> RCTL, TX_EN -> TCTL, REFCLKI -> RCLK, REFCLKO -> TCLK
      - 如果 RMII 外部接PHY, 如 LAN8720, 是直连, 即 TX-TX, RX-RX
      - 如果 RMII 外部接MAC, 如 MCU, 是交叉连接, TX-RX, CRS_DV-TX_EN, 两个 REFCLKI 可以接到同一时钟源如 MCU 的 MCO 50MHz 输出
  - SPI, 用于 LAN9370 的管理和配置, 测试 12.5MHz 能用, 建议一开始先 1MHz 及以下试
  - SMI, 也就是 MDC MDIO, 可用于连接和配置外部 PHY
  - 3V3单电源供电
- 原理图和测试工程开源到了 https://github.com/weifengdq/embedded, 欢迎 star

LAN9370 特性如下表:

![image-20260630161034806](README.assets/image-20260630161034806.png)

## STM32H723 基础配置

### 实际接线

LAN9370:

- Port 1, 默认 Slave, 悬空
- Port 2, 默认 Master, 接外部车载以太网设备 192.168.0.68
- Port 3, 默认 Slave, 悬空
- Port 4, 默认 Slave, 悬空, 接百兆车载以太网转换盒后到 PC, 192.168.0.2
- Port 5, RMII, 和 MCU 相当于 MAC-MAC, 需要交叉连接
- SMI, 悬空不用
- SPI, 接 MCU 用于配置
- NRST, 低电平复位, 接 MCU

![image-20260630162246230](README.assets/image-20260630162246230.png)

具体到引脚:

| STM32H723 信号 | STM32 引脚 |  LAN9370 信号  | 说明                        |
| :------------: | :--------: | :------------: | --------------------------- |
|      nRST      |    PC3     |      nRST      | LAN9370 硬件复位（低有效）  |
|    SPI1 SCK    |    PG11    |      SCK       | SPI 时钟                    |
|   SPI1 MISO    |    PG9     |      MISO      | SPI 数据（LAN9370 → STM32） |
|   SPI1 MOSI    |    PD7     |      MOSI      | SPI 数据（STM32 → LAN9370） |
|    SPI1 nCS    |    PG10    |       CS       | SPI 片选（软件 GPIO 控制）  |
|  MCO2 (50MHz)  |    PC9     | REFCLKI (RCLK) | RMII 参考时钟               |
|  ETH_REF_CLK   |    PA1     |       -        | 50MHz MCO2 环回             |
|   ETH_CRS_DV   |    PA7     |  TX_EN (TCTL)  | RMII 接收控制               |
|    ETH_RXD0    |    PC4     |      TXD0      | RMII 接收数据0              |
|    ETH_RXD1    |    PC5     |      TXD1      | RMII 接收数据1              |
|   ETH_TX_EN    |    PB11    | CRS_DV (RCTL)  | RMII 发送控制               |
|    ETH_TXD0    |    PB12    |      RXD0      | RMII 发送数据0              |
|    ETH_TXD1    |    PB13    |      RXD1      | RMII 发送数据1              |
|   LPUART1 TX   |    PA9     |       -        | 调试串口 (2Mbps, COM80)     |
|   LPUART1 RX   |    PA10    |       -        | 调试串口                    |
|      MDC       |    PA2     |       -        | **悬空不用**                |
|      MDIO      |    PC1     |       -        | **悬空不用**                |

### CubeMX

![image-20260630163832447](README.assets/image-20260630163832447.png)

注意事项:

- 大部分都是默认配置, 仅用于最开始生成基础CMake工程, 其中的配置并不是最终使用的, 仅供参考, 不要再在 CubeMX 上修改或生成代码
- PC9 因为要输出 50MHz, GPIO 的输出速率需要调整为 Very High
- MDC MDIO 悬空不接, 所有配置都是通过 SPI 进行的
- SPI 片选实际用的是软件 GPIO 控制

时钟树, 外部25MHz无源晶振, PLL2P 配成 50MHz 给 MCO2 输出:

![image-20260630164240948](README.assets/image-20260630164240948.png)

### 工程说明

LAN9370 数据手册截止目前需要NDA才能拿到, 我这里也没有, 硬件是参考官方板子, 软件是从 Linux 内核等地方扒拉下来的部分代码, 寄存器等并不算全面, 但最终也算能通能用. 对于 VLAN PTP gPTP Mirror StaticMAC 线缆诊断 信号质量SQI LED控制等, 只是命令占位.

裸机 LwIP, 192.168.0.108.

调试串口除了打印日志外, 也移植了 letter shell, 可以在调试串口进行命令交互, 但其中部分命令因为并没有详细的数据手册和寄存器参考, 仅仅占坑罢了.

![image-20260630164750917](README.assets/image-20260630164750917.png)

STM32H7系列相比之前的F系列有MPU/Cache配置的注意事项, 详细可参考工程里面的README文件.

### 编译和烧录

工程用 PowerShell + CMake 管理, 理论上不需要打开任何IDE, 用命令行即可进行编译下载:

- CMake ≥ 3.22 + Ninja
- arm-none-eabi GCC 工具链, 我之前安装过STM32CubeIDE, 所以用了里面自带的 `C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin`, 工程里 build.ps1 可以用 `-ToolchainBinDir <路径>`: 指定工具链 bin 目录
- STM32CubeProgrammer CLI (用于烧录), 安装 STM32CubeProgrammer 软件后, 路径记录一下, 工程里 build.ps1 可以用 `-CubeProgrammerCli <路径>`: 指定 STM32CubeProgrammer CLI 路径

编译 `.\build.ps1 build`, ST-Link烧录 `.\build.ps1 flash`, 都可以加 Debug 和 Release 参数

部分截图:

![image-20260630165839322](README.assets/image-20260630165839322.png)

![image-20260630170000971](README.assets/image-20260630170000971.png)

## 测试

### ping

默认没有开 VLAN, 所有端口互通, 从 PC 的 192.168.0.2 开始 ping 192.168.0.108 和 192.168.0.68, 也就是从 port4 ping  port2 和 RMII的port5

![image-20260630170717486](README.assets/image-20260630170717486.png)

### iperf

release 版本编译和下载后, 在 5001 端口测试, 约 90Mbit/s, 对应 RMII MAC-MAC 交叉连接的结果.

![image-20260630171306227](README.assets/image-20260630171306227.png)

### Master Slave

命令行对连接百兆车载以太网转换盒的 Port4 进行配置, 用 master/slave 命令, 从默认的 Slave -> Master, 发现不再能 ping 通, 改回 Slave 后正常工作

![image-20260630172150557](README.assets/image-20260630172150557.png)

### 端口分组

用 portgroup 命令把某几个Port划分到同一组, 无需 VLAN 使能，即可实现端口分组隔离, 如

```bash
## bit 0~4 => port 1~5
portgroup 2 0x18    # Port2只转发到Port4+5  (0x18=24=bit3+bit4)
portgroup 4 0x12    # Port4只转发到Port2+5  (0x12=18=bit1+bit4)
portgroup 4 0x10    # Port4只转发到Port5    (0x10=16=bit4)
portgroup 4 0x1F    # Port4恢复全互通
```

G下面设置 `portgroup 4 0x10` 让 Port4只转发到Port5, 这样能ping通Port5的 192.168.0.108, 不再能ping通Port2的192.168.0.68, 之后 `ortgroup 4 0x1F`, Port4恢复全互通

![image-20260630173112997](README.assets/image-20260630173112997.png)

### 其它功能说明

对于 VLAN PTP gPTP Mirror StaticMAC 线缆诊断 信号质量SQI LED控制等, 只是命令占位, 没有详细的数据手册和寄存器说明, 也没有继续尝试, 这些功能暂时搁置.

##  Github原理图与测试工程

原理图和测试工程开源到以下链接的 lan9370 文件夹, 欢迎 Star:

[https://github.com/weifengdq/embedded](https://github.com/weifengdq/embedded)



