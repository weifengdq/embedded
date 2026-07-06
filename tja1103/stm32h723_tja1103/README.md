# stm32h723_tja1103

这是一个基于 STM32H723ZG + NXP TJA1103 的 100BASE-T1 裸机示例工程，使用 CMake + GCC 构建，底层网络栈采用 STM32CubeH7 自带 ETH + LwIP 零拷贝路径。

当前工程除了原有的静态 IP、Ping、UDP Echo、TCP iperf 服务之外，还补充了以下内容：

- 基于 LPUART1 的 letter-shell 命令行
- TJA1103 Clause 45 寄存器读写命令
- TJA1103 诊断命令：SQI、MSE、链路训练时间、符号错误计数、链路丢失/失败、线缆测试、供电状态、温度状态、BIST 状态、环回控制
- TJA1103 低功耗/唤醒命令：Wake/Sleep 状态查看、功能模式切换、参数调节、SLEEP_REQUEST / SLEEP_ACCEPT / SLEEP_REJECT / WAKEUP_PHY_REQUEST / WU_SMI 触发
- 中文化并扩充后的说明文档

这版已经在当前台架上完成编译、下载、串口命令验证、Ping 验证和 iperf 验证。

## 1. 工程目标

工程定位不是“最小能跑”的 RMII 例子，而是一个可直接用于板级联调和 PHY 诊断的参考工程。目标包括：

- 保留 STM32H7 硬件 ETH MAC 的高吞吐路径
- 让 TJA1103 的 Clause 45 管理接口可直接在串口命令行中观察和操作
- 把常见诊断项纳入固件，减少每次联调都要手工查寄存器的重复劳动
- 为后续做 TC10 休眠/唤醒、环回测试、Built-In Self-Test 提供统一入口

## 2. 当前已实现功能

### 2.1 网络基础功能

- 静态 IPv4 地址：192.168.0.68/24
- 网关：192.168.0.1
- ICMP Ping 可达
- UDP Echo 服务器：端口 7
- TCP iperf 服务器：端口 5001

### 2.2 串口命令行

- 串口：LPUART1
- 波特率：921600
- letter-shell 采用中断接收 + 主循环轮询处理
- 命令通过 `SHELL_EXPORT_CMD` 导出到专用链接段

### 2.3 TJA1103 诊断功能

已纳入命令行的诊断项：

- Signal Quality Indicator，SQI
- Mean Square Error，MSE
- Cable Test
- Link training time
- Local receiver / Remote receiver / Follower silent 计时器
- Symbol error counter
- Link losses / link failures / link status drops / link availability drops
- 温度告警状态
- 供电监测状态
- PMA local / PMA remote / xMII PHY / xMII MAC loopback
- BIST 基础状态与连续接收统计入口

### 2.4 低功耗和唤醒功能

已纳入命令行的低功耗相关项：

- Wake/Sleep 状态寄存器读取
- Wake/Sleep 配置寄存器读取和改写
- Wake/Sleep 参数寄存器读取和改写
- `sleep req` 发起休眠请求
- `sleep accept` 接受休眠请求
- `sleep reject` 拒绝休眠请求
- `sleep wakephy` 触发 `WAKEUP_PHY_REQUEST`
- `sleep wusmi` 通过 `ALWAYS_ACCESSIBLE.WU_SMI` 触发 SMI 唤醒请求

说明：

- 命令行已经具备“发起”和“观测”低功耗流程的能力。
- 是否真的进入 `SLEEP_STATUS=1`，还依赖链路对端、当前 Wake/Sleep 模式、链路静默条件以及板级供电/引脚连接。
- 在本次台架上，命令写寄存器是成功的，但由于链路未进入手册要求的 sleep 条件，实际没有观察到 `sleep=1`。

## 3. 硬件连接说明

### 3.1 ST-Link / 调试串口

- ST-Link 已连接，可直接下载
- 虚拟串口使用 COM80
- LPUART1 PA9 是 MCU TX，应连接到 ST-Link VCP RX
- LPUART1 PA10 是 MCU RX，应连接到 ST-Link VCP TX
- SWD 连接：PA13 -> SWDIO，PA14 -> SWCLK

### 3.2 RMII / PHY 连接

| 功能 | STM32H723 引脚 | 连接对象 |
| --- | --- | --- |
| PHY 复位 | PC0 | TJA1103 nRST |
| RMII REF_CLK | PA1 | TJA1103 REF_CLK |
| RMII MDIO | PA2 | TJA1103 MDIO |
| RMII CRS_DV | PA7 | TJA1103 CRS_DV |
| RMII TX_EN | PB11 | TJA1103 TX_EN |
| RMII TXD0 | PB12 | TJA1103 TXD0 |
| RMII TXD1 | PB13 | TJA1103 TXD1 |
| RMII MDC | PC1 | TJA1103 MDC |
| RMII RXD0 | PC4 | TJA1103 RXD0 |
| RMII RXD1 | PC5 | TJA1103 RXD1 |

### 3.3 台架环境

当前台架条件如下：

- TJA1103 100BASE-T1 口通过百兆车载以太网转换盒连接 PC
- PC 地址：192.168.0.2
- 板卡地址：192.168.0.68
- iperf 工具：`ref/iperf.exe`

### 3.4 LED 指示引脚

TJA1103 的 GPIO 功能配置寄存器 `GPIO0_FUNC_CONFIG` 到 `GPIO11_FUNC_CONFIG` 是同构的。只要某个 GPIO 在当前 xMII pinning 下可用，就可以通过：

- `FUNC_SELECT = LED`
- `SIGNAL_SELECT = LED0_A / LED0_B / LED1_A / LED1_B / LED2_A / LED2_B / LED3_A / LED3_B`

把该引脚复用成 LED 输出。

从功能定义上，可用于 LED 的 GPIO 名称包括：

- `GPIO0/TMS/CONFIG0`
- `GPIO1/TDI`
- `GPIO2/TDO/CONFIG1`
- `GPIO3/CONFIG2`
- `GPIO4/CONFIG3`
- `GPIO5/CONFIG4`
- `GPIO6/CONFIG5`
- `GPIO7/CONFIG6`
- `GPIO8/TX_TCLK`
- `GPIO9/TCK`
- `GPIO10`
- `GPIO11`

但要注意两点：

1. 这是“功能可支持”的 GPIO 集合，不代表你的当前接口模式下这些脚都是空闲的。
2. 在 TJA1103A 上，很多 GPIO 与 MII/RMII/RGMII/JTAG/strap 信号复用，实际能不能拿来做 LED，要看你当前接口模式和板级连接。

对当前工程这类 RMII 设计，尤其要注意：

- `GPIO8` 复用 `RXC/TX_TCLK`
- `GPIO9` 复用 `TX_ER/TCK`，只有不占用 `TX_ER` 时才适合做 GPIO/LED
- `GPIO10` 复用 `TXD2`
- `GPIO11` 复用 `TXD3`
- `GPIO2/3/4` 也会与 RX 方向信号复用

所以在当前板级上，是否真的能拿这些脚做 LED，不由软件单独决定，而是取决于：

- 你选用的 TJA1103A 还是 TJA1103B
- 当前 xMII 模式
- 板子是否把对应引脚实际引出
- 这些脚是否已经被 RMII/RGMII/JTAG/strap 占用

### 3.5 LED 能指示什么内容

EPHY 侧有 4 个 LED trigger controller：

- `EPHY_LED_TRIGGER0`
- `EPHY_LED_TRIGGER1`
- `EPHY_LED_TRIGGER2`
- `EPHY_LED_TRIGGER3`

每个 trigger controller 的 `FUNCTION` 都可以配置为以下内容之一：

- `0`: `Link_availability`
- `1`: `Link_status`
- `2`: `Role`，Leader=1 / Follower=0
- `3`: `PHY Active`，AFE 运行中
- `4`: `Frame reception`，`RX_DV=1`
- `5`: `Symbol errors`
- `6`: `Frame/test transmission`，`TX_EN=1`
- `7`: `Frame activity`，RX 或 TX 任一活动

GPIO 侧选择的是 LED 输出通道，而不是直接选择上述功能值。也就是说：

- 先用 `EPHY_LED_TRIGGERx` 定义 trigger 的“含义”
- 再在 `GPIOx_FUNC_CONFIG` 里把 GPIO 绑定到 `LED0_A/B ... LED3_A/B`

因此一个完整 LED 路径通常是：

1. 设置 `EPHY_LED_TRIGGER0..3` 的 `FUNCTION`
2. 设置 `LEDx_CONFIG` / `LEDx_TRIG_SOURCE` / `LEDx_TRIG01_CONFIG` / `LEDx_TRIG23_CONFIG`
3. 将某个可用 GPIO 的 `FUNC_SELECT` 设为 `LED`
4. 用 `SIGNAL_SELECT` 选择 `LED0_A/B` 到 `LED3_A/B`

如果将来你要做最简单的板级 LED，推荐优先做两类：

- Link status
- Frame activity

这样最适合百兆车载以太网 bring-up 阶段快速判断：

- 是否真正训练成功并可用
- 是否存在实时收发活动

## 4. 启动过程

固件上电后主要执行以下流程：

1. 初始化 MPU 和 D-Cache
2. 初始化 HAL、系统时钟、GPIO、LPUART1、LwIP
3. 通过 MDIO 扫描并识别 TJA1103 PHY 地址
4. 对 TJA1103 重新下发运行配置
5. 初始化 letter-shell，并在串口打印欢迎信息
6. 启动 UDP Echo 和 TCP iperf 服务器
7. 在主循环中轮询处理 LwIP 和 Shell

串口启动日志示例：

```text
stm32h723_tja1103 boot

letter:/$
=== STM32H723 + TJA1103 Shell ===
Type 'help' for command list

Network ready IP=192.168.0.68 Mask=255.255.255.0 GW=192.168.0.1
UDP echo server on port 7
TCP iperf server on port 5001
ETH link up: 100M/full PHYAD=5
```

## 5. PHY 默认行为

当前默认配置重点如下：

- 通过 PC0 控制 TJA1103 硬复位
- 启用 Clause 45 管理访问
- 使用 RMII 反向时序配置
- 默认启用 autonomous mode
- 使能 100BASE-T1 广告能力
- 默认为 slave-preferred
- 启用 PPS 输出配置
- 启用部分计数器和诊断寄存器

可修改的主要编译期配置位于：

[LWIP/Target/eth_custom_phy_interface.h](LWIP/Target/eth_custom_phy_interface.h)

主要宏包括：

- `USER_PHY_CFG_AUTONOMOUS_MODE`
- `USER_PHY_CFG_MASTER_SLAVE`
- `USER_PHY_CFG_PPS_ENABLE`
- `USER_PHY_CFG_PPS_GPIO_INDEX`
- `USER_PHY_CFG_PPS_PHASE_NS`

主从模式支持：

- `USER_PHY_MASTER_SLAVE_SLAVE_PREFERRED`
- `USER_PHY_MASTER_SLAVE_MASTER_PREFERRED`
- `USER_PHY_MASTER_SLAVE_SLAVE_FORCE`
- `USER_PHY_MASTER_SLAVE_MASTER_FORCE`

## 6. 构建与下载

工程根目录提供了 PowerShell 构建脚本：

```powershell
.\build.ps1 build Debug
.\build.ps1 rebuild Debug
.\build.ps1 gen Debug
.\build.ps1 flash Debug
```

也支持显式指定工具链路径：

```powershell
.\build.ps1 build Debug -ToolchainBinDir C:\Toolchains\gcc-arm-none-eabi\bin
.\build.ps1 flash Debug -ToolchainBinDir C:\Toolchains\gcc-arm-none-eabi\bin -CubeProgrammerCli C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe
```

支持的 action：

- `build`
- `clean`
- `distclean`
- `rebuild`
- `gen`
- `flash`
- `help`

下载方式：

- 使用 STM32CubeProgrammer CLI
- 使用 ST-Link SWD 下载

本次实测下载环境：

- ST-LINK SN：003D001D3433510737363934
- ST-LINK FW：V3J15M7
- 目标芯片：STM32H723 Rev Z

## 7. 串口使用方法

串口参数：

- 端口：COM80
- 波特率：921600
- 数据位：8
- 校验：None
- 停止位：1

上电或复位后，串口会显示 letter-shell 提示符：

```text
letter:/$
```

输入 `help` 可查看导出的命令列表。

## 8. 命令手册

### 8.1 基础命令

- `mcu`
  - 查看 MCU 时钟、UID、Flash 容量、复位原因
- `uptime`
  - 查看上电运行时间
- `reset`
  - MCU 软件复位

### 8.2 PHY 信息和寄存器访问

- `phyinfo`
  - 查看 TJA1103 概要信息
  - 包括 PHY 地址、PHY ID、链路状态、AN 状态、SQI、MSE、Wake 状态摘要等
- `role`
  - 查询当前主从角色配置摘要
- `role master-pref`
  - 设为 master preferred
- `role master-force`
  - 设为 master force
- `role slave-pref`
  - 设为 slave preferred
- `role slave-force`
  - 设为 slave force
- `master pref|force`
  - 主角色快捷设置命令
- `slave pref|force`
  - 从角色快捷设置命令
- `t1cfg`
  - 查询常见 100BASE-T1 选项
  - 当前会显示：
    - role
    - auto operation
    - low power
    - polarity mode
    - detected polarity
    - link available / link status
    - local / remote receiver status
    - scrambler status
- `t1cfg auto on|off`
  - 控制 `PHY_CONFIG.AUTO_OPERATION`
- `t1cfg lowpower on|off`
  - 控制 `PMA_CONTROL1.LOW_POWER`
- `t1cfg polarity auto|nocorrect|swap|swap-nocorrect`
  - 控制 `PHY_CONFIG.POLARITY_SWAP` 和 `PHY_CONFIG.POLARITY_CORRECT_DISABLE`
- `t1cfg restart`
  - 重新触发 `AN_CTRL1.RESTART` 并执行 `PHY_CONTROL.START_OPERATION`
- `c45read <devad> <reg>`
  - 读取 Clause 45 寄存器
- `c45write <devad> <reg> <val>`
  - 写 Clause 45 寄存器

示例：

```text
c45read 30 0x8182
c45write 30 0x8182 0x8032
```

### 8.3 诊断命令

- `sqi`
  - 读取 `SIGNAL_QUALITY`，显示 current SQI、worst SQI、warn threshold、valid 位
- `mse`
  - 读取 `MSE` 寄存器
- `mse on`
  - 使能 MSE 计算
- `mse off`
  - 关闭 MSE 计算
- `cable`
  - 运行 `CABLE_TEST`
- `train`
  - 显示：
    - `LINK_TRAINING_TIMER`
    - `LOC_RCVR_STATUS_TIMER`
    - `REM_RCVR_STATUS_TIMER`
    - `FOLLOWER_SILENT_TIMER`
- `symerr`
  - 显示：
    - `SYMBOL_ERROR_COUNTER`
    - `LINK_STATUS_DROPS`
    - `LINK_AVAILABLE_DROPS`
    - `LINK_LOSSES`
    - `LINK_FAILURES`
- `temp`
  - 读取 `TEMP_STATUS`
- `supply`
  - 读取：
    - `AO_SYSTEM_SUPPLY_STATUS`
    - `CORE_SUPPLY_STATUS`
    - `VDDIO_SUPPLY_STATUS`
    - `VREGD_SUPPLY_STATUS`
    - `VREGA_SUPPLY_STATUS`
- `bist status`
  - 查看 BIST 路由、发生器、检查器、载荷、DA/SA、计数器等完整当前配置
- `bist cfg show`
  - 显示当前 BIST 配置
- `bist cfg check xmii|ephy`
  - 选择 checker 观察 xMII 侧还是 EPHY 侧数据
- `bist cfg tx normal|ephy`
  - 配置 generator 是否拦截 EPHY TX 路径
- `bist cfg rx normal|xmii`
  - 配置 checker 是否拦截 XMII RX 路径
- `bist cfg da <mac>`
  - 配置 generator 的目的 MAC，例如 `02:00:00:00:00:01`
- `bist cfg sa <mac>`
  - 配置 generator 的源 MAC
- `bist cfg etype <hex>`
  - 配置 Ethernet Type，例如 `0x0800`
- `bist cfg payload fixed <byte>`
  - 配置固定载荷字节
- `bist cfg payload ramp <byte>`
  - 配置递增载荷起始字节
- `bist cfg payload prbs`
  - 选择 PRBS 载荷
- `bist cfg sizemode fixed|inc|random`
  - 配置载荷长度模式
- `bist cfg psize <value>`
  - 配置 `PAYLOAD_SIZE`
- `bist cfg prbs 7|9|11|15`
  - 配置 PRBS 类型
- `bist cfg seed <hex>`
  - 配置 `BIST_LFSR_SEED`
- `bist cfg seedmode perframe|running`
  - 配置 LFSR 种子是每帧重装还是连续滚动
- `bist cfg good <count>`
  - 配置生产模式下 good frame 数量
- `bist cfg bad <count>`
  - 配置生产模式下 bad frame 数量
- `bist cfg wait <usec>`
  - 配置 checker 生产模式超时值
- `bist cfg ipg fixed|inc|random <len>`
  - 配置 IPG 模式和长度
- `bist cfg preamble <len>`
  - 配置前导码字节数
- `bist genstart prod|cont`
  - 启动 generator，生产模式或连续模式
- `bist genstop`
  - 通过 `STOP` 停止 generator
- `bist chkstart prod|cont`
  - 启动 checker，生产模式或连续模式
- `bist chkstop`
  - 停止 checker
- `bist rxstart`
  - 启动当前工程内置的连续接收统计预设：`check=ephy tx=ephy rx=xmii`
- `bist rxstop`
  - 停止内置接收统计预设
- `bist clr`
  - 保持当前 checker 配置，仅置位 `STATISTIC_CNT_RST` 清零统计

### 8.4 环回命令

- `loopback off`
  - 关闭所有已支持的环回位
- `loopback pma-local`
  - 开启 PMA local loopback
- `loopback pma-remote`
  - 开启 PMA remote loopback
- `loopback phy`
  - 开启 xMII PHY loopback
- `loopback mac`
  - 开启 xMII MAC loopback

说明：

- 环回属于 test mode，需要先打开受保护的 TEST_ENABLE 链路
- 工程内部已自动处理常见保护位
- 环回会影响正常业务链路，测试结束后应执行 `loopback off`

### 8.5 低功耗与唤醒命令

- `wake`
  - 显示：
    - `WAKE_SLEEP_STATUS`
    - `WAKE_SLEEP_CONFIG`
    - `WAKE_SLEEP_PARAMETERS`
    - `ALWAYS_ACCESSIBLE`
- `wake clear`
  - 清除 `WAKE_SLEEP_STATUS` 中的可写 1 清零状态位，然后再次显示状态
- `wakecfg off`
  - 关闭 Wake/Sleep 功能
- `wakecfg basic`
  - 设为 Basic Wake/Sleep 模式
- `wakecfg tc10`
  - 设为 TC10 Wake/Sleep 模式
- `wakecfg io on|off`
  - 打开/关闭 `WU_IO_ENABLE`
- `wakecfg smi on|off`
  - 打开/关闭 `WU_SMI_ENABLE`
- `wakecfg fwdphy on|off`
  - 打开/关闭 `FWD_WU_PHY_TO_WU_IO`
- `wakecfg fwdmdi on|off`
  - 打开/关闭 `FWD_WUPWUR_TO_WU_IO`
- `wakecfg noauto on|off`
  - 打开/关闭 `NO_AUTO_ON_WAKE`
- `wakecfg autoidle on|off`
  - 打开/关闭 `AUTO_SLEEP_ON_IDLE`
- `wakecfg autosilence on|off`
  - 打开/关闭 `AUTO_SLEEP_ON_SILENCE`
- `wakecfg autoreject on|off`
  - 打开/关闭 `AUTO_REJECT`
- `sleep startop`
  - 写 `PHY_CONTROL.START_OPERATION`
  - 当 `NO_AUTO_ON_WAKE=1` 时，唤醒后需要手工执行这个操作，PHY 才会从 active standby 继续恢复到正常工作态
- `wakeparam ack <0-7>`
  - 配置 `SLEEP_ACK_TIMER`
- `wakeparam idle <0-7>`
### 9.1 当前板子现状

当前板上的 `WAKE_IN_OUT` 和 `INH` 都是悬空的，因此本次工程虽然已经具备：

- Wake/Sleep 相关寄存器配置能力
- Basic / TC10 模式切换能力
- `SLEEP_REQUEST` / `SLEEP_ACCEPT` / `SLEEP_REJECT` / `WAKEUP_PHY_REQUEST` / `WU_SMI` 触发能力

但还不具备完整、可观察的板级休眠/唤醒演示条件。

### 9.2 这两个引脚分别做什么

根据数据手册：

- `WAKE_IN_OUT`
  - 类型：`IO`
  - 供电域：`VDDA_AO`
  - 含义：`local/forwarding wake-up input/output`
  - 作用：
    - 本地唤醒输入
    - 也可用于把 PHY 侧唤醒事件转发到外部
- `INH`
  - 类型：`O`
  - 供电域：`VDDA_AO`
  - 含义：`inhibit high-side switch output for Vreg control`
  - 作用：
    - 用于控制外部稳压器/电源使能
    - 典型用途是在睡眠时关闭主电源域，在唤醒时重新拉起

注意事项：

- `RST_N`、`WAKE_IN_OUT`、`INH` 都属于 `VDDA_AO` 域
- 它们的逻辑电平参考 `VDDA_AO`，不是 `VDDIO`
- 若 MCU IO 电压和 `VDDA_AO` 不同，不要直接硬连，必须做电平兼容处理

### 9.3 要实现 TC10，板级建议怎么连

最小可演示连接建议如下：

1. `VDDA_AO` 必须由独立的 standby 电源持续供电
2. `INH` 接到外部主电源稳压器的 `EN`/`SHDN` 管脚
3. `WAKE_IN_OUT` 接到 MCU 的 always-on GPIO，或者外部唤醒源
4. 如果 MCU 与 `VDDA_AO` 同为 3.3 V 且都是 always-on 域，可在评估板阶段直接连接
5. 如果 MCU IO 电压与 `VDDA_AO` 不同，建议使用双向电平转换或开漏 + 上拉兼容方案
6. 如要观察行为，建议把 `INH` 和 `WAKE_IN_OUT` 各自引到测试点，便于示波器观测

推荐的实验性连接方式：

- `INH` -> 外部 LDO / DC-DC 的 `EN`
- `WAKE_IN_OUT` -> MCU AO GPIO
- MCU AO GPIO 同时具备：
  - 输入模式：观察 PHY 是否转发 wake 事件
  - 输出模式：向 PHY 注入本地 wake request

如果你暂时不做真实电源门控，也可以先做“观测版”接线：

- `INH` 只接测试点或通过高阻值分压/缓冲接到 MCU 输入
- `WAKE_IN_OUT` 接 MCU GPIO

这种方式无法完整验证“关断主电源再拉起”的场景，但可以先验证：

- `WAKE_SLEEP_STATUS`
- `WU_IO_RECEIVED`
- `WU_PHY_RECEIVED`
- `WUP_RECEIVED` / `WUR_RECEIVED`
- `INHIBIT_STATUS`

### 9.4 休眠/唤醒涉及哪些寄存器或引脚

#### 关键引脚

- `WAKE_IN_OUT`
- `INH`
- `RST_N`
- MDI 差分线 `TRX_P/TRX_M`

#### 关键寄存器

- `WAKE_SLEEP_CONTROL` `0x8180`
  - `WAKEUP_PHY_REQUEST`
  - `SLEEP_REJECT`
  - `SLEEP_ACCEPT`
  - `SLEEP_REQUEST`
- `WAKE_SLEEP_STATUS` `0x8181`
  - `WU_IO_RECEIVED`
  - `WU_PHY_RECEIVED`
  - `WUP_RECEIVED`
  - `WUR_RECEIVED`
  - `AUTO_SLEEP_EVENT`
  - `SLEEP_FAILED`
  - `LPS_RECEIVED`
  - `SLEEP_STATUS`
- `WAKE_SLEEP_CONFIG` `0x8182`
  - `FUNCTION_SELECT`
  - `WU_IO_ENABLE`
  - `WU_SMI_ENABLE`
  - `FWD_WU_PHY_TO_WU_IO`
  - `FWD_WUPWUR_TO_WU_IO`
  - `SLEEP_SILENT_CHECK`
  - `WUR_IN_SEND_LPS`
  - `AUTO_REJECT`
  - `AUTO_SLEEP_ON_IDLE`
  - `AUTO_SLEEP_ON_SILENCE`
  - `NO_AUTO_ON_WAKE`
- `WAKE_SLEEP_PARAMETERS` `0x8184`
  - `SLEEP_ACK_TIMER`
  - `AUTO_SLEEP_IDLE_TIMER`
  - `AUTO_SLEEP_SILENCE_TIMER`
  - `MIN_SILENT_TIMER`
- `ALWAYS_ACCESSIBLE` `0x801F`
  - `WU_SMI`
  - `LIMITED_ACCESS`
  - `INHIBIT_STATUS`
- `PHY_CONTROL` `0x8100`
  - `START_OPERATION`

#### 与 TC10 数据线握手相关的链路事件

- `WUP`
- `WUR`
- `LPS`
- `SLEEP_SILENT`

### 9.5 完整休眠流程是什么样

下面给的是“将来板级连好以后”的完整流程。

#### 流程 A：Basic Wake/Sleep

1. 保证 `VDDA_AO` 始终供电
2. 把 `WAKE_IN_OUT` 和 `INH` 正确接好
3. 用 `wakecfg basic` 进入 basic 模式
4. 按需要使能：
   - `wakecfg io on`
   - `wakecfg smi on`
5. 按需要调整参数：
   - `wakeparam ack <n>`
   - `wakeparam idle <n>`
   - `wakeparam silence <n>`
   - `wakeparam min <n>`
6. 停止业务流量，确保链路满足静默条件
7. 执行 `sleep req`
8. 轮询 `wake`，观察：
   - `LPS_RECEIVED`
   - `SLEEP_STATUS`
   - `INHIBIT_STATUS`
9. 若进入休眠成功：
   - `SLEEP_STATUS=1`
   - `INH` 可能发生去使能，从而关闭主电源域
10. 唤醒时，选择一种本地唤醒方式：
    - 外部拉动 `WAKE_IN_OUT`
    - `sleep wakephy`
    - `sleep wusmi`
11. 若配置了 `NO_AUTO_ON_WAKE=1`，醒来后还需执行：
    - `sleep startop`

#### 流程 B：TC10 Sleep/Wake

1. 对端也必须支持并启用同类流程
2. 本端执行 `wakecfg tc10`
3. 按需要设置：
   - `wakecfg io on`
   - `wakecfg smi on`
   - `wakecfg fwdphy on`
   - `wakecfg fwdmdi on`
4. 根据系统策略配置：
   - `wakecfg noauto on|off`
   - `wakecfg autoreject on|off`
   - `wakeparam ack <n>`
   - `wakeparam idle <n>`
   - `wakeparam silence <n>`
   - `wakeparam min <n>`
5. 停止应用数据发送，进入静默条件
6. 执行 `sleep req`
7. PHY 在数据线上与对端进行 TC10 握手，内部会经历类似：
   - `SEND_LPS`
   - `SLEEP_SILENT`
   - `SLEEP`
8. 用 `wake` 观察：
   - `LPS_RECEIVED`
   - `SLEEP_FAILED`
   - `SLEEP_STATUS`
   - `WUR_RECEIVED`
   - `WUP_RECEIVED`
9. 唤醒来源可以是：
   - `WAKE_IN_OUT` 本地唤醒
   - `WU_SMI`
   - `WAKEUP_PHY_REQUEST`
   - 对端通过 MDI 发送的 wake-up 事件
10. 若启用了转发，PHY 可把唤醒事件转发到 `WAKE_IN_OUT`
11. 若 `NO_AUTO_ON_WAKE=1`，执行 `sleep startop`

### 9.6 现在如果你想先做“半实物”验证，建议步骤

在你把引脚真正连起来之前，先做以下验证即可：

```text
wakecfg tc10
wakecfg smi on
wake
sleep req
wake
sleep wusmi
wake
sleep startop
```

可以验证这些点：

- 管理寄存器可读可写
- 模式切换有效
- `WU_SMI` 路径可写入
- `START_OPERATION` 路径可写入

不能验证的点：

- 真实 `INH` 供电控制
- 真实 `WAKE_IN_OUT` 外部电平事件
- 真实 TC10 over data-line 完整休眠/唤醒闭环

### 9.7 当前台架实际结果

本次台架测试结果如下：

- `WAKE_IN_OUT` 和 `INH` 当前都悬空
- `wakecfg basic` / `wakecfg tc10` 可切换
- `wakecfg smi on|off` 可切换
- `sleep req`、`sleep wusmi`、`sleep startop` 命令均可执行
- `wake clear` 可清除状态位
- 但未观察到 `WAKE_SLEEP_STATUS.sleep=1`

这说明：

- 软件命令路径和寄存器访问路径是通的
- 但当前板级和对端条件不足以形成真正的休眠/唤醒闭环

## 10. BIST 说明

当前 BIST 不再只是“基础入口”，而是已经扩成一套可用于后续联调的 generator/checker 工具。

### 10.1 已支持能力

- 读出完整 BIST 当前配置
- 配置 DA / SA / EtherType
- 配置 payload 类型、固定字节、PRBS、payload size、good/bad frame plan、wait timer
- 配置 route：checker 侧、generator TX 侧、checker RX 侧
- 启动 / 停止 generator
- 启动 / 停止 checker
- 连续模式接收统计
- 生产模式状态读取
- 统计计数器清零

### 10.2 推荐的最小使用方式

#### 只看当前配置

```text
bist status
```

#### 配置 generator 参数

```text
bist cfg da 02:00:00:00:00:01
bist cfg sa 02:00:00:00:00:02
bist cfg etype 0x0800
bist cfg payload fixed 0x5A
bist cfg psize 0x40
bist cfg good 10
bist cfg bad 1
```

#### 启动 generator / checker

```text
bist genstart prod
bist chkstart prod
bist status
```

#### 连续统计模式

```text
bist rxstart
bist status
bist clr
bist rxstop
```

### 10.3 当前板上已实际验证的 BIST 命令

已在本次台架上实际验证以下命令可执行并回显：

- `bist cfg show`
- `bist cfg good 10`
- `bist cfg etype 0x0800`
- `bist cfg payload fixed 0x5A`
- `bist cfg da 02:00:00:00:00:01`

验证后已恢复默认配置：

- `payload_cfg=0x20A5`
- `etype=0x0000`
- `DA=00:00:00:00:00:00`
- `good=100`

### 10.4 还需要什么条件才能做真正的 BIST 链路验证

完整 BIST 收发验证仍取决于你后续采用哪种路径：

- 仅本地 xMII/EPHY 内部路径验证
- 配合环回模式验证
- 配合对端 PHY 做链路级验证

如果将来要做真正的 generator -> PHY -> line -> PHY/checker 闭环，建议把下面几样固定下来：

- 目标 route 方案
- 是否开启 loopback
- 是否走 production mode 或 continuous mode
- 对端是否参与或仅做本地链路自检
- 但当前链路环境没有满足 TJA1103 进入 Sleep 所需的条件

最常见原因包括：

- 对端未参与 Wake/Sleep 流程
- 当前链路仍在收发活动，不满足静默条件
- 板级未把相关唤醒/抑制引脚连到可观测或可控路径
- 使用 TC10 时，对端没有完成相同握手

### 9.4 若要稳定演示真正的休眠/唤醒

建议满足以下前提：

- 对端 PHY 或媒体转换器支持并参与相同模式的 Wake/Sleep
- 板级供电设计允许观察 `INH` 控制效果
- `WAKE_IN_OUT` 有实际连线或观测点
- 业务流量停止，满足手册要求的 silence 条件
- 如使用 TC10，确认对端也启用 TC10 并能处理 LPS / WUP / WUR

## 10. BIST 说明

当前 BIST 命令定位为“基础联调入口”，不是完整量产测试脚本。

已支持：

- 读 BIST 控制寄存器和状态寄存器
- 打开连续接收统计路径
- 查看 good/bad/rxer 计数器
- 清零连续模式统计

暂未做的部分：

- 发生器完整参数配置接口
- DA/SA/PRBS/载荷长度全部命令化
- 生产模式 good/bad frame plan 的完整命令包装

原因很简单：

- 当前板上没有把这部分作为首要验证目标
- 先把状态观测和基础接收统计跑通，更符合当前联调阶段的投入产出比

后续如果需要，可以继续扩成完整 BIST 控制台。

## 11. 网络与串口实测结果

本次实际验证结果：

### 11.1 编译与下载

- `cmake --build --preset Debug --target stm32h723_tja1103` 通过
- `build.ps1 flash Debug` 下载通过

### 11.2 启动日志

- 成功打印启动 banner
- 成功打印 shell banner
- 成功打印网络初始化信息
- PHY 链路最终显示 `ETH link up: 100M/full PHYAD=5`

### 11.3 串口命令验证

已实际验证以下命令可执行并返回结果：

- `help`
- `mcu`
- `phyinfo`
- `role`
- `master pref`
- `slave pref`
- `t1cfg`
- `wake`
- `wakecfg basic`
- `wakecfg tc10`
- `wake clear`
- `sqi`
- `mse`
- `train`
- `symerr`
- `temp`
- `supply`
- `bist status`
- `bist cfg show`
- `bist cfg good 10`
- `bist cfg etype 0x0800`
- `bist cfg payload fixed 0x5A`
- `bist cfg da 02:00:00:00:00:01`
- `sleep startop`

实际样例结果包括：

- `SQI raw=0x4044 valid=1 current=4 worst=4 warn_limit=0`
- `role=slave-pref ... base_t1=0x8000`
- `master pref` 后 `base_t1=0xC000`，链路在当前台架上会掉线
- 恢复 `slave pref` 后链路重新 `ETH link up: 100M/full PHYAD=5`
- `Link training: 20.875 ms`
- `TEMP_STATUS=0x00A1`
- `VDDIO_SUPPLY_STATUS=0x0003 level=3`
- `BIST_PROD_STATUS=0x0000 done=0 fail=0`

### 11.4 低功耗演示尝试

已实际尝试：

- `wake clear`
- `wakecfg basic`
- `wakecfg smi on`
- `wakecfg tc10`
- `sleep req`
- `sleep wusmi`
- `sleep startop`

结果：

- 寄存器写入成功
- 当前板上 `WAKE_IN_OUT`、`INH` 都悬空
- 但未观察到 `sleep=1`
- 尝试后网络仍正常

### 11.5 网络验证

- Ping：4/4 成功，往返时间 < 1 ms
- 再次 Ping：2/2 成功
- TCP iperf：5 秒测试约 92.7 Mbit/s

## 12. 主机侧测试命令

### 12.1 Ping

```powershell
ping 192.168.0.68 -n 4
```

### 12.2 UDP Echo

可用任意 UDP 工具向端口 7 发包，例如 `ncat`：

```powershell
ncat -u 192.168.0.68 7
```

### 12.3 iperf

仓库已自带 `ref/iperf.exe`：

```powershell
C:\Users\weife\STM32Cube\Repository\STM32Cube_FW_H7_V1.13.0\ref\iperf.exe -c 192.168.0.68 -p 5001 -t 5
```

本次实测结果：

```text
55.3 MBytes / 5.0 sec = 92.7 Mbits/sec
```

## 13. 工程关键实现点

### 13.1 Shell 接入方式

- `Shell/shell_port.c` 负责 UART 移植层和命令实现
- `Shell/letter-shell/src` 为 letter-shell 核心源码
- 链接脚本添加了 `shellCommand` 段
- `main.c` 在初始化完成后调用：
  - `Shell_Init(&hlpuart1)`
  - `Shell_PrintBanner()`
- 主循环中调用 `Shell_Process()`

### 13.2 PHY 管理接口

- TJA1103 底层访问仍复用现有 `USER_PHY_*` 抽象
- Shell 没有复制 MDIO/C45 访问逻辑，而是通过：
  - `ethernetif_get_user_phy()`
  - `USER_PHY_ReadC45()`
  - `USER_PHY_WriteC45()`
  - `USER_PHY_ModifyC45()`
  访问同一个 PHY 对象

### 13.3 test mode 保护位

根据 TJA1103 手册，线缆测试、环回、BIST 等功能都受 TEST_ENABLE 层级保护。

当前工程在执行对应命令时，会按需要打开：

- `PORT_CONTROL.EN`
- `PORT_INFRA_CONTROL.EN`
- `INFRA_CONFIG.TEST_ENABLE`
- `PORT_FUNC_ENABLE.TEST_ENABLE`
- `PORT_FUNC_ENABLE.BIST_ENABLE`
- `PHY_CONFIG.TEST_ENABLE`

这比直接裸写目标寄存器更稳妥，也更接近手册要求的流程。

## 14. 当前限制与注意事项

- 工程仍是 NO_SYS LwIP，依赖主循环轮询。
- 当前命令集已经覆盖主要诊断项，但 BIST 仍是基础版本，不是完整量产测试框架。
- 低功耗命令已经具备配置和触发能力，但真正的休眠/唤醒成功还依赖对端与板级条件。
- `WAKE_IN_OUT`、`INH` 是否可完整演示，取决于你的板级连接，不是软件单方面决定。
- 环回和 BIST 会改变正常数据通路，使用完应恢复默认配置。
- 若后续修改 D2/RAM 布局，仍要重新检查 ETH DMA 描述符、RX pool 和 LwIP heap 的放置关系。
- 当前工程的零拷贝 RX pool 放在 D2 SRAM，LwIP heap 放在 D1 RAM，这个布局已经过实测验证，不建议随意改回。

## 15. 推荐后续扩展

如果后续要继续把它打磨成更完整的 TJA1103 调试平台，建议优先做这些：

- 补全 BIST 发生器参数命令
- 增加 `wakecfg` 对更多 TC10 辅助位的封装
- 若板级支持，增加 `WAKE_IN_OUT` / `INH` 的 GPIO 观测或中断联动
- 增加脚本化串口回归测试
- 增加更细的 PHY 状态寄存器解码输出

当前版本已经足够支撑：

- 板级 bring-up
- PHY 基础诊断
- 网络吞吐验证
- Wake/Sleep 配置联调
- 后续继续深入 TJA1103 特性开发
