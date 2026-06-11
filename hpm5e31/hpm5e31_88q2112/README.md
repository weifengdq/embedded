# hpm5e31_88q2112

## 1. 项目定位

本工程用于验证 HPM5E31 通过 ENET0 RGMII 接口连接 **Marvell 88Q2112** (1000BASE-T1 车载以太网 PHY) 的以太网基线，要求全部在当前工程内完成，不修改 SDK 目录。

目标功能：

- 静态 IP `192.168.0.99`，可 ping 通主机 `192.168.0.2`
- UDP echo server（端口 5005）
- iperf TCP/UDP server（端口 5001）
- 1000BASE-T1 自动协商

## 2. 88Q2112 简介

**Marvell 88Q2112** (产品型号 88Q2112-A2-NYD2A000) 是一款符合 IEEE 802.3bp 标准的 **1000BASE-T1** 车载以太网 PHY，主要特点：

| 特性       | 说明 |
| ---------- | ---- |
| 速率       | 1000 Mbps（单对双绞线） |
| 接口       | RGMII / SGMII |
| MDIO       | IEEE 802.3 Clause 45 (C45) |
| 封装       | 48-pin QFN |
| PHY ID     | OUI=0x002B, Model=0x09 |
| 供应商     | Marvell (现属 Infineon) |

与普通千兆以太网 PHY（如 RTL8211F）不同，88Q2112 使用 **C45 MDIO** 协议进行寄存器访问，且仅支持 **1000BASE-T1**（单对双绞线），不支持传统 1000BASE-T（四对双绞线）。

参考资料：

- [英飞凌 88Q2112 产品页面](https://www.infineon.com/part/88Q2112-A2-NYD2A000)
- [Linux 内核驱动 marvell-88q2xxx.c](https://github.com/torvalds/linux/blob/master/drivers/net/phy/marvell-88q2xxx.c)
- 本地数据手册：`D:\hpm\embedded\hpm5e31\bak\infineon-brightlane-88q2110-88q2112-datasheet-datasheet-en.pdf`

## 3. 硬件连接

### 3.1 HPM5E31 ↔ 88Q2112 RGMII 连接

| 88Q2112 信号      | HPM5E31 引脚 | 功能 |
| ----------------- | ------------ | ---- |
| ETH_RXD2          | PF11         | RGMII 接收数据 2 |
| ETH_RXDV (RX_CTL) | PF08         | RGMII 接收数据有效 |
| ETH_TXD0          | PF03         | RGMII 发送数据 0 |
| ETH_RXD0          | PF09         | RGMII 接收数据 0 |
| ETH_TXEN (TX_CTL) | PF07         | RGMII 发送使能 |
| ETH_RXD3          | PF12         | RGMII 接收数据 3 |
| ETH_RXCK          | PF13         | RGMII 接收时钟 |
| ETH_TXD3          | PF06         | RGMII 发送数据 3 |
| ETH_TXCK          | PF02         | RGMII 发送时钟 |
| ETH_TXD2          | PF05         | RGMII 发送数据 2 |
| ETH_RXD1          | PF10         | RGMII 接收数据 1 |
| ETH_TXD1          | PF04         | RGMII 发送数据 1 |
| ETH_MDC           | PA31         | MDIO 管理时钟 |
| ETH_MDIO          | PA30         | MDIO 管理数据 |
| ETH_NRST (复位)   | PF26         | PHY 复位 (GPIO) |

### 3.2 88Q2112 Strapping 引脚配置

用户预估默认 strapping：

- **PHYAD[2:0] = 111 → 地址 7**
- **1000BASE-T1 Slave**
- **RGMII 模式**

> ⚠️ 上电复位后请通过 MDIO 扫描确认实际 PHY 地址（扫描 0~31），程序会自动检测并打印结果。

### 3.3 千兆车载以太网转换盒

车载以太网通过千兆车载以太网转换盒（设为 Master 模式）连接到电脑以太网卡（192.168.0.2）。

> ⚠️ 88Q2112 默认为 **Slave** 模式，转换盒需设为 **Master** 模式才能建立链接。如果 PN 连线有误（正负反接），链接将无法建立。

## 4. 工程架构

```
hpm5e31_88q2112/
├── CMakeLists.txt              # 顶层 CMake 配置
├── build.ps1                   # 构建/烧录脚本
├── README.md                   # 本文件
├── .gitignore
├── boards/
│   └── hpm5e31_lite/
│       ├── CMakeLists.txt      # 板级 CMake (定义 __USE_88Q2112=1)
│       ├── board.h             # 板级宏定义 (RGMII 引脚, 时钟等)
│       ├── board.c             # 板级初始化 (时钟, 引脚, PHY 复位)
│       ├── pinmux.h            # 引脚复用声明
│       ├── pinmux.c            # 引脚复用配置 (RGMII 12+2 线)
│       └── hpm5e31_lite.yaml   # 板级配置文件
├── common/
│   ├── netinfo.h               # IP/MAC 地址配置
│   └── single/
│       ├── common.h            # lwIP 通用头文件
│       ├── common.c            # ENET 初始化 + 88Q2112 PHY 驱动
│       ├── netconf.h           # 网络配置头文件
│       ├── netconf.c           # 网络配置实现 (静态IP/DHCP)
│       └── osal.h              # OS 抽象层
├── inc/
│   ├── common_cfg.h            # PHY 地址, 缓冲配置
│   ├── lwipopts_app.h          # lwIP 参数配置
│   └── app/
│       ├── lwiperf_local.h     # 本地 iperf 声明
│       └── udp_echo.h          # UDP echo 声明
├── ports/
│   └── baremetal/single/
│       ├── ethernetif.c        # lwIP 网络接口
│       ├── ethernetif.h
│       ├── lwipopts.h          # baremetal lwIP 参数
│       └── arch/
│           ├── cc.h            # 编译器适配
│           ├── sys_arch.c      # lwIP 系统抽象层
│           └── sys_arch.h
└── src/
    ├── lwip.c                  # 应用入口 main()
    ├── fatal_probe.c           # 异常捕获
    ├── lwiperf_local.c         # 本地 iperf 实现 (符号前缀 app_lwiperf_*)
    └── app/
        └── udp_echo.c          # UDP echo 服务
```

### 4.1 关键设计决策

| 决策 | 说明 |
| ---- | ---- |
| PHY 驱动位置 | `common/single/common.c`（本地，不修改 SDK） |
| MDIO 访问方式 | C45 通过 C22 间接寄存器 13/14 实现 |
| PHY 地址扫描 | 上电扫描 0~31，自动检测 88Q2112（PHYID1=0x002B） |
| 初始化序列 | 来自 Linux 内核 `mv88q2110_init_seq0/1` |
| RGMII Delay | MAC 侧 TX=0, RX=0（88Q2112 内部自带延时） |
| 静态 IP | `192.168.0.99/255.255.255.0` |
| iperf 实现 | 本地 `lwiperf_local.c`，符号重命名为 `app_lwiperf_*` |

## 5. 88Q2112 寄存器配置

### 5.1 MDIO C45 访问

88Q2112 使用 IEEE 802.3 **Clause 45** MDIO 协议。HPM5E31 的 ENET 控制器原生支持 C22，C45 访问通过 C22 间接寄存器实现：

| C22 寄存器 | 用途 |
| ---------- | ---- |
| 13 (0x0D)  | MMD 访问控制（devad + function） |
| 14 (0x0E)  | MMD 地址/数据 |

**C45 写操作流程：**
1. 写 regaddr → REG14，写 (0x0000 \| devad) → REG13（设置地址）
2. 写 data → REG14，写 (0x4000 \| devad) → REG13（写入数据）

**C45 读操作流程：**
1. 写 regaddr → REG14，写 (0x0000 \| devad) → REG13（设置地址）
2. 写 (0xC000 \| devad) → REG13，读 REG14（读取数据）

### 5.2 初始化序列

来自 Linux 内核 `marvell-88q2xxx.c` 的 `mv88q2110_init_seq0/1`：

**Seq0**（MMD=3 PCS）：
| 寄存器 | 值 | 说明 |
| ------ | -- | ---- |
| 0xFFE4 | 0x07B5 | 硬件配置字 |
| 0xFFE4 | 0x06B6 | 硬件配置字 |

**Seq1**（MMD=3 PCS + MMD=7 AN）：
| MMD | 寄存器 | 值 | 说明 |
| --- | ------ | -- | ---- |
| 3   | 0xFFDE | 0x402F | PCS 配置 |
| 3   | 0xFE34 | 0x4040 | 速率/模式 |
| 3   | 0xFE2A | 0x3C1D | 时序配置 |
| 3   | 0xFE34 | 0x0040 | 速率/模式 |
| 7   | 0x8032 | 0x0064 | 自动协商 |
| 7   | 0x8031 | 0x0A01 | 自动协商 |
| 7   | 0x8031 | 0x0C01 | 自动协商 |
| 3   | 0xFFDB | 0x0010 | 完成 |

初始化后额外配置：
- 使能温度传感器 (PCS 0x8032 = 0x6001)
- 确保 PHY 未进入低功耗模式 (PMAPMD CTRL1 LPOWER=0)

### 5.3 链路状态读取

| 寄存器 | 说明 |
| ------ | ---- |
| C22 BMSR (reg 1) | 基本链路状态（bit 2 = link up） |
| C45 AN_STAT (MMD 7, 0x8001) | 本地/远端接收状态 |
| C45 AN_STAT2 (MMD 7, 0x803A) | 协商速率（bit 14 = 1000BT1） |

## 6. 测试说明

### 6.1 构建与烧录

```powershell
# 配置工程
.\build.ps1 configure Debug

# 构建
.\build.ps1 build Debug

# 构建 + 烧录
.\build.ps1 flash Debug

# Release 构建
.\build.ps1 flash Release

# 启动 GDB Server（调试用）
.\build.ps1 gdbserver Debug
```

### 6.2 串口输出

串口参数：**UART0, PA00 TXD, PA01 RXD, 115200-8-N-1** (COM13)

预期输出：
```
board init:   1
reset flag:   0x...
88Q2112 MDIO scan:
  addr  7: ID1=0x002B ID2=0x0980 <== 88Q2112!
88Q2112 init at PHY addr 7
  PHYID1=0x002B
  PHYID2=0x0980
  Init seq0...
  Init seq1...
88Q2112 init complete.
Enet phy init passed !
LwIP Version: 2.2.1
Local IP:  192.168.0.99
Host IP:   192.168.0.2
Static IP: 192.168.0.99
Netmask : 255.255.255.0
Gateway : 0.0.0.0
UDP echo server started on port 5005
TCP iperf server started on port 5001
UDP iperf server started on port 5001
Ready:
  ping 192.168.0.99
  UDP echo port: 5005
  iperf TCP/UDP server port: 5001
88Q2112: BMSR=0x786D AN_STAT=0x... speed=1000M
Link up!
Speed/Duplex: 1000M/Full
```

### 6.3 Ping 测试

```powershell
# 在电脑上执行
ping 192.168.0.99 -t
```

### 6.4 UDP Echo 测试

```powershell
# 使用 ncat 或其他 UDP 工具
echo "Hello 88Q2112" | ncat -u 192.168.0.99 5005
```

### 6.5 iperf 测试

```powershell
# UDP iperf（电脑端运行）
D:\hpm\embedded\hpm5e31\bak\iperf.exe -c 192.168.0.99 -u -b 100M -t 10

# TCP iperf（电脑端运行）
D:\hpm\embedded\hpm5e31\bak\iperf.exe -c 192.168.0.99 -t 10
```

## 7. 测试结果

> 📝 待实际测试后填写

| 测试项目 | 结果 | 备注 |
| -------- | ---- | ---- |
| Ping | - | - |
| UDP Echo | - | - |
| iperf UDP | - | - |
| iperf TCP | - | - |
| 链路速率 | - | - |
| PHY 地址 | - | - |

## 8. 参考代码

| 来源 | 链接 |
| ---- | ---- |
| Linux 88Q2XXX 驱动 | [marvell-88q2xxx.c](https://github.com/torvalds/linux/blob/master/drivers/net/phy/marvell-88q2xxx.c) |
| Linux Marvell PHY 驱动 | [marvell.c](https://github.com/torvalds/linux/blob/master/drivers/net/phy/marvell.c) |
| Semidrive q21xx 驱动 | [test_li_os/q21xx.c](https://github.com/leon6002/test_li_os/blob/d398df0/drivers/platform/EthTrcv/src/q21xx.c) |
| Hostmobility PHY Setup | [hm_phy_setup.py](https://github.com/hostmobility/meta-hostmobility-bsp/blob/3f10c9e/recipes-bsp/hm-phy-setup/files/hm_phy_setup.py) |
