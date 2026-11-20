# RK3506 + T30HM1TS2500 (S2500) 10BASE-T1S Linux 驱动

[GitHub 仓库](https://github.com/weifengdq/embedded) | [板卡主文档](../README.md) | [驱动源码说明](drivers/s2500/README.md)

## 1. 这是什么

T30HM1TS2500（下称 S2500）是 onsemi 的 10BASE-T1S MAC-PHY，通过 OPEN Alliance SPI（OA-SPI，TC6）与主控相连。本目录提供它在 **RK3506（Linux 6.1）** 上的 out-of-tree 驱动：

- 标准 Linux 网络设备（netdev），加载后多出一个以太网接口（脚本会改名为 `t1s0`）；
- 支持 PLCA：可作 follower（跟随者，ID 1..254），也可作 coordinator（协调者，ID 0）；
- 提供 SQI（信号质量）、HDD（线缆诊断）、PCS/PHY 环回、MAC 统计和寄存器快照；
- 通过 sysfs 调 PLCA、环回、LED，不需要专用用户态工具；
- 不改内核源码，只编译一个 `s2500.ko`；可选打 DTS 补丁，让开机自动绑定。

已在 HZ-RK3506G2_MiniEVM（RK3506G2，3×Cortex-A7）+ Linux 6.1.118 上实测：ping 0% 丢包，iperf2 TCP 下行 8.83 Mbit/s、上行 8.96 Mbit/s，UDP 9.43 Mbit/s。

## 2. 为什么不能直接用内核自带的 lan8650 驱动

RK3506 BSP 内核里通常已经编入了 Microchip LAN865x 的 `lan8650` 驱动。它按设备树 compatible（`microchip,lan8651` / `microchip,lan8650`）匹配，**不校验芯片 ID**，所以会先把 SPI0 上的 S2500“抢”过去。

但 S2500 的 MAC 寄存器是标准 OA 布局（MMS1 `MCONTROL0` 的 TXEN/RXEN 等），LAN865x 驱动写的是 Microchip 私有寄存器，还会漏掉 S2500 必须的 `PHY_CTRL.LCTL=1`。结果就是：`probe` 看着成功、网卡也出现了，但板子实际不收不发。

因此加载 `s2500.ko` 前必须先解绑：

```bash
echo spi0.0 > /sys/bus/spi/drivers/lan8650/unbind
```

解绑只对本次开机有效。想彻底解决，请打第 7 节的 DTS 补丁，让 S2500 节点直接匹配本驱动。

## 3. 目录结构

```text
rk3506/
├── README.md
├── drivers/s2500/           # 驱动源码
│   ├── s2500.c
│   ├── s2500_regs.h
│   ├── Makefile
│   ├── README.md
│   └── .gitignore
├── scripts/
│   ├── 18-install-s2500.sh  # 编译 / 部署 / 打 DTS 补丁
│   └── 19-s2500-test.sh     # 板端 8 步自测
├── patches/
│   └── 0006-dts-rk3506-s2500-t1s.patch
└── prebuilt/
    └── s2500.ko             # 预编译，仅供快速验证
```

## 4. 硬件连接

以 RK3506 开发板的 J7 排针（树莓派兼容）为例，S2500_1 模块与 LAN8651 模块同引脚：

| J7 | 信号 | RK3506 | S2500 P1 |
| --- | --- | --- | --- |
| 17 | VCC_3V3 | — | 3 |
| 20 | GND | — | 9 |
| 18 | RST_N | GPIO0_A2 (RM_IO2) | 8 |
| 19 | SPI0_MOSI | GPIO0_B1 (RM_IO9) | 10 |
| 21 | SPI0_MISO | GPIO0_B2 (RM_IO10) | 2 |
| 22 | IRQ_N | GPIO0_A6 (RM_IO6) | 5 |
| 23 | SPI0_CLK | GPIO0_B0 (RM_IO8) | 4 |
| 24 | SPI0_CSN0 | GPIO0_B3 (RM_IO11) | 1 |

- 全部 3.3 V，不需要电平转换。
- SPI mode 0、MSB first。DTS 用 25 MHz（S2500 规格上限），实测 15 MHz 也能跑满 10BASE-T1S。
- `IRQ_N` 低有效；`RST_N` 低有效，驱动拉低至少 500 µs，释放后等 15 ms 再访问。
- 若 DTS 里 UART2/UART3 占用了 RM_IO8/9/10，需要把它们关掉，否则 pinctrl 申请不到 SPI 引脚。

## 5. 编译

### 5.1 依赖

- RK3506 Linux SDK，包含**已配置/编译过的内核构建树**（至少有 `.config`、`Module.symvers`；没有就先 `make modules_prepare`）；
- 交叉工具链 `arm-none-linux-gnueabihf-`（SDK 自带，或 `gcc-arm-10.3-2021.07` 之类）；
- 主机 `make`、`bash`。

先确认工具链可用：

```bash
arm-none-linux-gnueabihf-gcc --version
```

### 5.2 配环境

如果 SDK 配套仓库提供 `04-setup-env.sh`：

```bash
cd <rk3506-sdk 配套目录>
source scripts/04-setup-env.sh
```

它会设置 `SDK_DIR`、`CROSS_COMPILE` 和 `PATH`。没有这个脚本就手动设置：

```bash
export SDK_DIR=/path/to/HZ-RK3506_MiniEVM_LINUX_SDK
export KDIR=$SDK_DIR/kernel
export ARCH=arm
export CROSS_COMPILE=arm-none-linux-gnueabihf-
export PATH=/path/to/toolchain/bin:$PATH
```

### 5.3 编译

```bash
cd rk3506
make -C "$SDK_DIR/kernel" M="$PWD/drivers/s2500" \
     ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabihf- modules
```

或者用脚本：

```bash
bash scripts/18-install-s2500.sh build
```

产物：`drivers/s2500/s2500.ko`。脚本还会生成 `s2500.stripped.ko`（去掉调试信息，体积更小）。

检查：

```bash
modinfo drivers/s2500/s2500.ko
```

`vermagic` 必须和板端内核一致（本次实测为 `6.1.118 SMP preempt ... ARMv7`）。不一致就换用与板端内核匹配的 SDK 重编。

## 6. 部署与加载

除 6.1 的 `scp` 在主机执行外，其余命令都在板端执行（root）。假设 `s2500.ko` 已经在 `/tmp/`。

### 6.1 拷贝

```bash
scp drivers/s2500/s2500.ko root@<board-ip>:/tmp/
```

### 6.2 解绑 lan8650

```bash
echo spi0.0 > /sys/bus/spi/drivers/lan8650/unbind
```

没有这个目录说明内核没编 lan8650，跳过即可。

### 6.3 加载驱动

```bash
insmod /tmp/s2500.ko plca_en=1 plca_id=5 plca_ncnt=8 plca_totmr=32 spi_hz=25000000
```

参数含义见 8.2。只想先验证驱动能否 probe，可以不带参数：

```bash
insmod /tmp/s2500.ko
```

### 6.4 改名、配 IP、up

驱动注册的是 `ethX`（通常是 `eth1`，`eth0` 是板载 GMAC）。先看：

```bash
ip -br link
```

把新出现的那个改成 `t1s0`（如果已经是 `t1s0` 就跳过）：

```bash
ip link set eth1 name t1s0
ip addr add 192.168.10.15/24 dev t1s0
ip link set t1s0 up
```

> `ip addr add` 报 `File exists` 说明地址已配过，可以忽略。

### 6.5 确认

```bash
dmesg | tail -20
cat /sys/class/net/t1s0/plca_status
cat /sys/class/net/t1s0/s2500_regs
```

正常应看到 `IDVER=00000011`、`pst=1`（总线上有协调者时）、接口 `UP`。

### 6.6 一键脚本

```bash
bash scripts/18-install-s2500.sh deploy <board-ip>
```

脚本会自动编译、`scp`、解绑、`insmod`（默认参数同上）、改名、配 `192.168.10.15/24` 并 `up`。要求板端可用 root SSH。

## 7. DTS 补丁（可选，推荐）

不打补丁也能用，只是每次开机要手动 unbind + insmod。打补丁后，S2500 节点直接匹配 `onsemi,t30hm1ts2500`，开机自动加载。

```bash
cd rk3506
bash scripts/18-install-s2500.sh dts
```

脚本 18 会处理 DTS 的 CRLF/LF 行尾。手动打补丁时，SDK 自带的这块板 DTS 是 CRLF，
补丁也按 CRLF 保存，因此用：

```bash
cd "$SDK_DIR/kernel"
patch --binary -p1 < /path/to/rk3506/patches/0006-dts-rk3506-s2500-t1s.patch
```

补丁默认针对 `arch/arm/boot/dts/HZ-RK3506G2_MiniEVM_TF.dts`。如果你的板级 DTS 文件名不同，先改补丁头部的路径。它做四件事：

1. 把 `spi0` 下原来的 `lan865x` 节点换成 `s2500`；
2. `compatible = "onsemi,t30hm1ts2500"`；
3. `spi-max-frequency = <25000000>`，中断 GPIO0_A6（低有效），复位 GPIO0_A2；
4. 删掉 `local-mac-address`，让驱动优先使用芯片出厂 MAC。

然后按 SDK 正常流程重编 DTB/FIT 并烧录。只改 DTS 不重烧，等于没打。

如果 UART2/UART3 与 RM_IO8/9/10 复用，确认它们在 DTS 里是 `status = "disabled"`。

## 8. sysfs 与模块参数

### 8.1 sysfs 节点

路径 `/sys/class/net/t1s0/`：

| 节点 | 读写 | 说明 |
| --- | --- | --- |
| `s2500_regs` | RO | IDVER/PHYID/STDCAP/STATUS0/BUFSTS/CONFIG0/MCONTROL0/PLCA 快照 |
| `s2500_counters` | RO | MAC 统计 + 驱动统计 |
| `s2500_sqi` | RO | `sqi=<0..7> sqi_plus=<0..255>` |
| `s2500_hdd` | RW | 写 1 触发 HDD；读回 `valid/short_open/one_wire/raw` |
| `s2500_led` | RW | DIO0=TX/RX 活动，DIO1=PLCA 状态（芯片内部 LED，不占 GPIO） |
| `s2500_loopback` | RW | 0=关，1=PCS 环回，2=PHY 环回 |
| `plca_enable` | RW | 0/1，PLCA 使能 |
| `plca_id` | RW | 0=协调者，1..254=跟随者，255=不参与 |
| `plca_ncnt` | RW | 节点数（所有节点一致） |
| `plca_totmr` | RW | TO timer（bit times，默认 32） |
| `plca_status` | RO | `pst=<0/1> crps=<0/1>` |
| `plca_cr` | RW | 协调者冗余 CREN |

### 8.2 模块参数

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `plca_en` | 0 | 加载时即开 PLCA |
| `plca_id` | 5 | 0=协调者，1..254=跟随者，255=不参与 |
| `plca_ncnt` | 8 | 总线上节点数 |
| `plca_totmr` | 32 | TO timer，单位 bit time |
| `plca_cr` | 0 | 协调者冗余 |
| `led` | 1 | DIO0/DIO1 作 TX/RX 活动、PLCA 状态指示 |
| `rx_fcs_strip` | 1 | 剥掉 RX 的 4 字节 FCS |
| `spi_hz` | 0 | 覆盖 DTS 的 SPI 频率；0 表示用 DTS |

例：临时试 15 MHz，而不重编 DTB：

```bash
insmod s2500.ko spi_hz=15000000
```

## 9. 测试结果

拓扑：主机 USB-10BASE-T1S（LAN8670）作 PLCA 协调者，ID0、NCNT8、TOTMR32，IP `192.168.10.10`；RK3506 + S2500 作跟随者，ID5，IP `192.168.10.15`；中间是真实 T1S 总线。

### 9.1 ping

```bash
# 主机
ping -c 100 -i 0.05 192.168.10.15
# 板端
ping -c 100 192.168.10.10
```

结果：100/100，丢包 0%，RTT min/avg/max = 0.900/1.089/1.968 ms。

### 9.2 iperf2 吞吐

```bash
# 主机作 server，板端作 client（上行）
iperf -s
iperf -c 192.168.10.10 -t 20

# 板端作 server，主机作 client（下行）
iperf -s
iperf -c 192.168.10.15 -t 20

# UDP
iperf -c 192.168.10.15 -u -b 10M -t 20
```

| 项目 | 结果 |
| --- | --- |
| ping | 0% 丢包 |
| TCP 下行（host→board） | 8.83 Mbit/s |
| TCP 上行（board→host） | 8.96 Mbit/s |
| UDP 10 Mbps 灌包 | 9.43 Mbit/s，丢包 0% |
| 板端 CPU（整片 3×A7） | 下行 15.4%，上行 19.2% |

10BASE-T1S 线速 10 Mbit/s、半双工 + PLCA，UDP 9.43 已接近线速，TCP 8.8~9.0 约为线速的 85%~90%（含 TCP/IP 头开销）。

性能关键：驱动一次 SPI 突发最多搬 48 个 68 字节块，而不是每块一次 `spi_sync`。所以 SPI 时钟 15 MHz 就够，25 MHz 只多 1~2%。

## 10. 板端 8 步自测

把 `s2500.ko` 和 `19-s2500-test.sh` 传到板子：

```bash
scp drivers/s2500/s2500.ko scripts/19-s2500-test.sh root@<board-ip>:/tmp/
ssh root@<board-ip> "sh /tmp/19-s2500-test.sh"
```

脚本默认：`/tmp/s2500.ko`、接口 `t1s0`、板端 IP `192.168.10.15`、对端 `192.168.10.10`。8 步分别是：

1. 解绑 lan8650、加载驱动、改名、配 IP、up，打印 dmesg 和链路状态；
2. dump 关键寄存器（`s2500_regs`）；
3. 打印测试前计数器；
4. PCS 环回，确认 TX 帧数增加且链路不掉；
5. PLCA 关→开，确认 `plca_status` 变化；
6. 触发 HDD，读回 SQI/HDD；
7. ping 协调者；
8. 打印测试后计数器。

注意：S2500 的 PCS 环回只把数据绕回 PCS，MAC 侧收不到；PHY 环回会把链路打下来。所以第 4 步只验证 TX 通路，**RX 通路必须用两节点 ping/iperf 验证**。

## 11. 常见问题

### 11.1 加载后没有 t1s0 / probe 失败

- 先确认 `dmesg | grep -i s2500` 有没有 `IDVER=00000011`。没有的话多半是 lan8650 还绑着：看 `/sys/bus/spi/drivers/lan8650/`，执行 unbind 后重来。
- `probe` 报 `-ENODEV`：SPI 通信不通。查 J7 接线、3.3 V、CS/CLK/MOSI/MISO、复位 GPIO，以及 DTS 的 SPI 引脚有没有被 UART 占用。
- 接口出现了但叫 `eth1`：正常，执行 `ip link set eth1 name t1s0`。

### 11.2 link down / ping 不通

- 看 `cat /sys/class/net/t1s0/plca_status`。`pst=0` 表示没收到协调者 BEACON。
- 总线上必须有一个 coordinator（ID0），或者所有节点都关 PLCA 走 CSMA。
- 所有节点的 `plca_ncnt`、`plca_totmr` 要一致；follower ID 不能重复。
- 板子作 coordinator 时：`plca_id=0`，并且总线上不能有第二个 ID0。
- 终端电阻：只在总线两端保留终端，中间节点不要并 49.9 Ω。

### 11.3 HDD 报 SHORT_OPEN=2

`cat /sys/class/net/t1s0/s2500_hdd` 里 `short_open=2` 表示检测到短路/开路（常见是终端过多或接线问题）。多块 S2500_1 板并接时，每块板的 49.9 Ω 终端会并联，6 块时约 8 Ω，总线会被压垮。测试时只保留两端终端。

### 11.4 重启后配置全丢

`insmod` 参数、IP、`ip link set` 都是运行时的，重启当然没有。两个办法：

- 每次开机跑一遍第 6 节；
- 打第 7 节 DTS 补丁并重烧 DTB/FIT，再用 systemd/init 脚本配 IP 和 PLCA。

### 11.5 insmod 报 invalid module format

`vermagic` 与板端内核不一致：`modinfo s2500.ko | grep vermagic`，和板端 `uname -r` 对比。用板端对应的内核源码/配置重编，或直接用 `prebuilt/` 里的（也要先核对 vermagic）。

### 11.6 能 ping 但 iperf 很慢

- 确认 SPI 实际频率：`dmesg | grep -i SPI`，以及是否被 `spi_hz` 覆盖。
- 确认用的是本驱动的突发版本（一次多块），而不是旧的逐块版本。
- 看 `s2500_counters` 里的 `mac_rx_fcs_err`、`drv_*` 是否为 0；有错误先查线缆、终端、PLCA。

## 12. 说明与限制

- 驱动为 GPL-2.0，out-of-tree，单队列、store-and-forward。
- PLCA 走 sysfs，没有走 phylib/ethtool netlink（S2500 不是 MDIO PHY）。
- cut-through、TC10 sleep/wake、TC6 时间戳等寄存器层具备，但未接入 Linux 框架。
- 预编译的 `prebuilt/s2500.ko` 针对某内核版本编译，仅供快速验证；正式使用请按第 5 节重编。
- 数据手册请向 onsemi 获取；本目录不包含 datasheet。
