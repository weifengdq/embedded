# 交接说明 — RK3506 + LAN8651 (10BASE-T1S) 移植、调试与性能测试

> 面向**新的 AI 会话 / 新接手人员**，自包含。先读本文件，再读根目录
> `README_LAN8651.md`（完整原理、命令、测试方法与证据）。
> 最后更新：**2026-10-23**
> 分支：`lan8651_10t1s`（仓库 `/mnt/sda3/rk3506/rk3506_1`）

---

## 1. 目标与背景

### 1.1 本次目标

1. 在 **HZ-RK3506G2_MiniEVM（RK3506G2，TF 卡版）** 上移植 **Microchip LAN8651
   10BASE-T1S MAC-PHY（SPI / OPEN Alliance TC6）** 的 Linux 驱动；
2. 打通 Rockchip SPI0 + GPIO（RESET/IRQ）→ LAN8651 → 10BASE-T1S 总线；
3. 与 **USB-10BASE-T1S（EVB-LAN8670-USB，`184f:0051`）** 对联；
4. **PLCA Master（Node 0）与 Slave（Node 1）都要实测**；
5. 记录性能（ping/iperf）、调优、故障排查；
6. 所有过程沉淀到 `README_LAN8651.md` + 脚本 + 补丁 + 日志 + 本交接。

### 1.2 背景

- 厂商资料：`/mnt/sda3/rk3506/HZ-RK3506G2_MiniEVM`
- 在线文档：<https://hzhydocs.yuque.com/org-wiki-hzhydocs-rg5tcg/dn05eo>
- 旧交接（板卡 SDK/编译/启动/SocketCAN/gs_usb）：`handover/HANDOVER.md`
- 主机：Ubuntu 26.04 / GCC 15；板卡调试串口 `/dev/ttyACM0` 1500000；
- 板卡网络：`eth0`（RK GMAC）DHCP，IP 会变；登录 `root/rockchip`。

---

## 2. 已确认的约束和偏好

| 项 | 约定 |
| --- | --- |
| 工作仓库 | `/mnt/sda3/rk3506/rk3506_1`，分支 `lan8651_10t1s` |
| 提交/推送 | **每次修改自行 commit + push，无需询问**；少量多次 |
| 工作路径 | SDK/工具链在 `/mnt/sda3/rk3506/work/...`（英文路径） |
| 入库范围 | 文档、脚本、补丁、适配后的驱动源码、日志结论；SDK 全量源码/大产物不入库 |
| 文档语言 | 中文为主，术语保留英文 |
| 硬件改动 | 允许增量部署（SSH 写 p3），尽量不重刷 TF 卡；必须重刷时告诉用户 |
| 网络代理 | `10.11.1.221:7890`（下载 kernel/ethtool 源码时用） |
| 串口 | `/dev/ttyACM0`，1500000 8N1；可用 `work/serial-cmd.py` 脚本化 |
| 对端 | USB-10BASE-T1S 在**主机**上，网卡名 `enx001ec0d1c337`；不要与板端 Node ID 冲突 |

---

## 3. 已完成事项与关键结论

### 3.1 ✅ 硬件接线与电平（最终正确版）

| J7 | 网络 | RK3506 实际引脚 | LAN8651 |
| --- | --- | --- | --- |
| 17 | VCC_3V3 | — | 3V3 |
| 20 | GND | — | GND |
| 18 | GPIO0_A2 | GPIO0_A2 | RESET_N |
| 19 | SPI0_MOSI | **GPIO0_B1 / RM_IO9** | SDI |
| 21 | SPI0_MISO | **GPIO0_B2 / RM_IO10** | SDO |
| 22 | GPIO0_A6 | GPIO0_A6 | IRQ_N |
| 23 | SPI0_CLK | **GPIO0_B0 / RM_IO8** | SCLK |
| 24 | SPI0_CSN0 | **GPIO0_B3 / RM_IO11** | CS_N |

- **电平全部 3.3 V**：PMUIO / GPIO0_ABC = VCC_3V3 = 3.3 V；DevKit 也是 3.3 V。
- **★ 最大坑**：J7 的 SPI0 走 **RM_IO8-11（GPIO0_B0-B3）**，不是专用 SPI0 C0-C3。
  用 `spi0_clk_pins`/`spi0_csn0_pins` 会一直读 0。
- **★ 复用冲突**：B0/B1/B2 分别被 UART3_TX（北斗）/ UART2_TX/RX（RS485_1）占用；
  DTS 必须：
  ```dts
  &uart2 { status = "disabled"; };
  &uart3 { status = "disabled"; };
  ```
- **EXP_D1 LED 在 GPIO0_C3**，不是 J7 的 CS0（B3），无需禁用（之前误判，已恢复）。
- **RESET/IRQ 实测有效**：GPIO0_A2 拉低 → IRQ=1；释放 → IRQ=0。
- **SPI**：Mode 0、MSB first、15 MHz（已足够跑满 10 Mbps）。

### 3.2 ✅ Linux 6.1 驱动与 PLCA 回移

- **0004 补丁**（`patches/0004-rk3506-6.1-lan865x-plca-core.patch`）：
  - UAPI：`10baseT1S_Full/Half/P2MP_Half`（bit 93/94/95）；
  - phylib：`phy_plca_cfg/status`、`genphy_c45_plca_*`、`PHY_BASIC_T1S_P2MP_FEATURES`；
  - ethtool netlink：`net/ethtool/plca.c`（6.9 实现适配 6.1：无 `->set`/`set_ntf_cmd`，SET 走 small op）；
  - `phy-core.c` speed/duplex 表 + `BUILD_BUG_ON` 93→96；
  - `net/ethtool/common.c` link mode 名称/参数；
  - Kconfig/Makefile 挂点：`OA_TC6`、`LAN865X`、`MICROCHIP_T1S_PHY`。
- **驱动源码**（`drivers/lan865x/`，从 Microchip v1.1 + mainline 6.12 适配 6.1）：
  - `oa_tc6.c/.h`：TC6 框架（含 v1.1 自动恢复、硬件 reset、PLCA 配置保持）；
  - `lan865x.c`：LAN865x MAC 驱动；
  - `microchip_t1s.c`：LAN867x/LAN865x PHY（含 PLCA 回调）；
  - `mdio-open-alliance.h`、`plca.c`；
  - 6.1 关键适配：`mii_bus` 没有 `read_c45/write_c45`，改为解析 `MII_ADDR_C45` 编码。
- **0005 补丁**（`patches/0005-dts-rk3506-lan8651-t1s.patch`）：
  - `&spi0` 启用，`pinctrl-0 = <&rm_io8_spi0_clk &rm_io9_spi0_mosi &rm_io10_spi0_miso &rm_io11_spi0_csn0>`；
  - `lan865x@0`：`microchip,lan8651`，15 MHz，IRQ GPIO0_A6 电平低，reset GPIO0_A2 低有效，MAC `02:00:00:10:86:51`；
  - 禁用 UART2/UART3。
- **编译结果**：`zImage` 6,879,408 B，DTB 39,020 B，无错误；`lan865x`/`microchip_t1s` 为 built-in。
- **增量部署**：FIT（`thunderboot.its` + `rkbin/tools/mkimage`）6,922,240 B，SSH `dd` 到 `/dev/mmcblk0p3`，重启成功；模块整体替换 `/lib/modules/6.1.118`。

### 3.3 ✅ 板端 LAN8651 已跑通

```
[    3.200864] lan8650 spi0.0: Hard reset performed
[    3.225765] LAN865X Rev.B0/B1 Internal Phy spi0.0:00: attached PHY driver
[   11.027094] lan8650 spi0.0 t1s0: Link is Up - 10Mbps/Half - flow control off
```

- `t1s0`：`10baseT1S_P2MP/Half`，MAC `02:00:00:10:86:51`，IP `192.168.1.100/24`。
- `eth0`：RK GMAC，DHCP（本次 `10.11.1.148`）。
- 接口重命名脚本 `/etc/init.d/S39rename-t1s`（见 3.5）。

### 3.4 ✅ PLCA Master/Slave 实测

| 场景 | 板端 | 主机 | 结果 |
| --- | --- | --- | --- |
| Slave | Node 1（follower） | Node 0（coordinator） | `Status: on`；ping 3/3（1.44–1.88 ms）；TCP **8.86 Mbps** |
| Master | Node 0（coordinator） | Node 1（follower） | `Status: on`（两端）；ping 5/5（1.00–1.20 ms）；TCP **8.93 Mbps** |
| PLCA off（CSMA） | — | — | TCP **6.72 Mbps** |
| UDP 9 Mbps | Master | Follower | 丢包 **0.13%**，jitter 0.093 ms |

**调优数据**：

| 场景 | 速率 | Retr |
| --- | --- | --- |
| TO=32, Burst=0 | 8.87 Mbps (20 s) | 198 |
| TO=32, Burst=2 | **8.94 Mbps** (15 s) | 101 |
| TO=64, Burst=0 | 8.76 Mbps (15 s) | 115 |
| TO=16, Burst=0 | **5.74 Mbps** (15 s) | 477 |
| **60 s soak**（Master, TO=32） | **8.86 Mbps** (60 s) | 649 |
| 反向（板→主机, Master） | 8.44 Mbps (20 s) | 6 |

结论：PLCA 比 CSMA 提升约 **32%**；`to-tmr=32/64` 好，16 明显差；`burst-cnt=2` 略优；15 MHz SPI 足够。

### 3.5 ✅ 接口命名修复

LAN865x built-in 先 probe，会抢 `eth0`，RK GMAC 变 `eth1`，导致原 `eth0` DHCP 配到 T1S 上、上行失联。已加：

`/etc/init.d/S39rename-t1s`：
- `spi0.0` → `t1s0`
- `ff4c8000.ethernet` → `eth0`

`/etc/network/interfaces`：
```
auto eth0
iface eth0 inet dhcp

auto t1s0
iface t1s0 inet static
    address 192.168.1.100
    netmask 255.255.255.0
```

最终镜像用 `scripts/17-apply-lan8651-network.sh` 写入 buildroot overlay。

---

## 4. 重要文件、链接、数据或代码位置

### 4.1 仓库内

```
rk3506_1/
├── README_LAN8651.md                       # ★ 完整原理/接线/移植/测试/结果/调优
├── drivers/lan865x/                        # 适配 6.1 的驱动源码
│   ├── oa_tc6.c/.h  lan865x.c  microchip_t1s.c
│   ├── mdio-open-alliance.h  plca.c
│   ├── Kconfig.lan865x  Makefile.lan865x
├── patches/
│   ├── 0004-rk3506-6.1-lan865x-plca-core.patch
│   └── 0005-dts-rk3506-lan8651-t1s.patch
├── scripts/
│   ├── 15-install-lan865x.sh               # 幂等：打补丁/拷驱动/加 config
│   ├── 16-build-ethtool.sh                 # 交叉编译 ethtool 6.11
│   ├── 17-apply-lan8651-network.sh         # S39 重命名 + interfaces
│   └── 31-deploy-kernel-ssh.sh             # SSH 增量部署 FIT + 模块
├── logs/board-test-lan8651-2026-10-23.md   # 本次实测证据
└── handover/HANDOVER-LAN8651.md            # 本文件
```

### 4.2 关键绝对路径

| 内容 | 路径 |
| --- | --- |
| SDK | `/mnt/sda3/rk3506/work/HZ-RK3506_MiniEVM_LINUX_SDK` |
| 内核树 | `$SDK/kernel`（`kernel-6.1`） |
| 新 FIT | `$SDK/kernel/zboot-lan8651.img` |
| DTB | `$SDK/kernel/arch/arm/boot/dts/HZ-RK3506G2_MiniEVM_TF.dtb` |
| 板端 boot 分区 | `/dev/mmcblk0p3`（FIT 裸镜像，**不是文件系统**） |
| 板端模块 | `/lib/modules/6.1.118` |
| 旧 p3 备份 | `/mnt/sda3/rk3506/work/boot-p3-backup-2026-10-23.img`（12 MB） |
| 新模块包 | `/mnt/sda3/rk3506/work/modules-6.1.118-new.tar.gz` |
| ethtool 6.11 | `/mnt/sda3/rk3506/work/ethtool-6.11/ethtool` |
| 串口脚本 | `/mnt/sda3/rk3506/work/serial-cmd.py` |
| 板端工具 | `/userdata/ethtool`、`/userdata/gpio_poll`、`/userdata/bitbang_spi_gpio` |

### 4.3 当前现场状态（2026-10-23 收盘）

- 板卡已启动，`t1s0` up，`192.168.1.100/24`；`eth0` DHCP（本次 `10.11.1.148`）。
- 板端 PLCA 当前为 **Master Node 0**；主机 USB 适配器为 **Follower Node 1**（`Status: on`）。
- 主机 USB 适配器 IP `192.168.1.1/24`。
- 测试完成后建议把两端 PLCA 关掉或恢复默认，避免下次误判：
  ```bash
  sudo ethtool --set-plca-cfg enx001ec0d1c337 enable off
  /userdata/ethtool --set-plca-cfg t1s0 enable off
  ```
- USB 适配器偶发 wedge：重新插拔或 `authorized 0/1` 复位。

---

## 5. 当前未完成任务

1. **最终镜像固化**：
   - 把 `S39rename-t1s` + `interfaces` 写进 buildroot overlay（`scripts/17` 已做）；
   - 重编 `rootfs`/`firmware`，重烧 TF 卡，得到开箱即用固件（当前是增量部署 + 手工加脚本）。
2. **25 MHz SPI 压测**（可选）：当前 15 MHz 已跑满 10 Mbps；若改 25 MHz 需复测误码/重传。
3. **多节点 PLCA（>2 节点）**：需要更多 LAN8651 板/USB 适配器。
4. **长稳 soak**：连续 iperf 1 小时，观察 adapter wedge、重传、温度。
5. **RS485_1/北斗替代方案**：当前 DTS 禁用了 UART2/UART3；若项目必须同时使用，需要硬件改板/改引脚。
6. **README/日志的最终校对**：性能表、命令、路径已填；如再测 25 MHz/多节点，补进第 7/9 章。

---

## 6. 下一步请直接执行什么

### 6.1 先确认当前状态

```bash
# 串口
picocom -b 1500000 /dev/ttyACM0
# 或
python3 /mnt/sda3/rk3506/work/serial-cmd.py "ip -br link; dmesg | grep -i lan865"

# 期望
# t1s0  UP  02:00:00:10:86:51
# eth0  UP  <RK GMAC>
```

### 6.2 回归 PLCA Slave / Master

```bash
# 主机
IF=enx001ec0d1c337
sudo ip addr add 192.168.1.1/24 dev $IF 2>/dev/null
sudo ip link set $IF up

# --- Slave: 主机 Node0, 板 Node1 ---
sudo ethtool --set-plca-cfg $IF enable on node-id 0 node-cnt 8 \
    to-tmr 32 burst-cnt 0 burst-tmr 128
ssh root@<board-ip> '/userdata/ethtool --set-plca-cfg t1s0 enable on \
    node-id 1 node-cnt 8 to-tmr 32 burst-cnt 0 burst-tmr 128; \
    /userdata/ethtool --get-plca-status t1s0; ping -c 3 192.168.1.1'

# --- Master: 主机 Node1, 板 Node0 ---
sudo ethtool --set-plca-cfg $IF enable on node-id 1 node-cnt 8 \
    to-tmr 32 burst-cnt 0 burst-tmr 128
ssh root@<board-ip> '/userdata/ethtool --set-plca-cfg t1s0 enable on \
    node-id 0 node-cnt 8 to-tmr 32 burst-cnt 0 burst-tmr 128; \
    /userdata/ethtool --get-plca-status t1s0'
sudo ethtool --get-plca-status $IF     # 期望 on
```

### 6.3 测速

```bash
# 板端
ssh root@<board-ip> 'iperf3 -s -p 5201'
# 主机
iperf3 -c 192.168.1.100 -p 5201 -t 20 -O 3
iperf3 -c 192.168.1.100 -p 5201 -t 20 -O 3 -R
iperf3 -c 192.168.1.100 -p 5201 -u -b 9M -t 10 -O 2
```

### 6.4 最终镜像（可选，但推荐）

```bash
cd /mnt/sda3/rk3506/rk3506_1
source scripts/04-setup-env.sh
bash   scripts/15-install-lan865x.sh
bash   scripts/17-apply-lan8651-network.sh
cd "$SDK_DIR"
./build.sh kernel
./build.sh kernel-modules:"$SDK_DIR/buildroot/output/rockchip_rk3506/target"
./build.sh rootfs
./build.sh firmware
# 然后用 scripts/30-make-tf-card.sh 重烧 TF 卡
```

> 当前已用 SSH 增量部署验证；最终镜像只是把同样的配置固化，风险低。

### 6.5 提交/推送

```bash
cd /mnt/sda3/rk3506/rk3506_1
git add -A
git commit -m "lan8651: 修正 J7 SPI0 为 RM_IO8-11(B0-B3)、禁用 UART2/3、接口重命名；PLCA 主从/iperf 实测通过"
git push
```

---

## 7. 风险、假设与待确认项（务必阅读）

### 7.1 风险

- **UART2/UART3 被禁用**：RS485_1 与北斗/GPS 不能与 LAN8651 同时使用（硬件复用）。
  这是当前 DTS 的硬约束，项目若需要两者共存必须改板/改引脚。
- **接口命名依赖 S39 脚本**：脚本未执行时 LAN8651 会抢 `eth0`，上行 DHCP/SSH 异常。
- **内核 ABI**：`struct phy_driver` 布局变了（PLCA 回调插在 `read_mmd` 前），
  旧 PHY 模块不能与新内核混用；必须整体替换模块（`31-deploy-kernel-ssh.sh` 已做）。
- **USB 适配器 wedge**：已知偶发；测试中 ping 突然不通先复位适配器。
- **PLCA 参数不一致**：两端 `node-cnt/to-tmr` 必须一致；Node ID 不能冲突。
- **25 MHz SPI 未验证**：当前 15 MHz 已稳定；若改 25 MHz 需复测。
- **`to-tmr=16` 性能差**：实测 5.74 Mbps，不要用。

### 7.2 假设

- LAN8651 板是 Microchip **LAN8651 DevKit**（P1 2×5：1=RST_N, 2=IRQ_N, 3=MISO, 4=CS_N, 5=SCK, 6=MOSI, 9=GND, 10=3V3）。
- J7 引脚序号与厂商规格书表 3-3 一致（本次 18/22/19/21/23/24 全部实测）。
- 3V3 能提供 ≥100 mA；两端 10BASE-T1S 终端电阻已接。
- 主机 `ethtool` 6.19 / 板端 6.11 的 PLCA 命令编号与内核一致（已实测）。

### 7.3 待确认

1. 最终镜像是否要保留 UART2/UART3 禁用（取决于项目是否用 RS485_1/北斗）。
2. 是否需要 25 MHz SPI（当前 15 MHz 已够）。
3. 多节点/长稳是否有需求。
4. 板端 `eth0` DHCP 的 IP 会变；建议路由器绑定或改静态。

---

## 8. 快速复现命令（最小集）

```bash
# 1) 安装驱动/补丁
cd /mnt/sda3/rk3506/rk3506_1
source scripts/04-setup-env.sh
bash   scripts/15-install-lan865x.sh

# 2) 编译 + 增量部署
cd "$SDK_DIR" && ./build.sh kernel
bash /mnt/sda3/rk3506/rk3506_1/scripts/31-deploy-kernel-ssh.sh <board-ip>

# 3) ethtool 6.11
bash /mnt/sda3/rk3506/rk3506_1/scripts/16-build-ethtool.sh
scp /mnt/sda3/rk3506/work/ethtool-6.11/ethtool root@<board-ip>:/userdata/

# 4) 板端检查
dmesg | grep -i -E 'lan865|spi0'
ip -br link                    # 期望 eth0(GMAC) + t1s0(LAN8651)
/userdata/ethtool --get-plca-cfg t1s0

# 5) 主机对端
sudo ip addr add 192.168.1.1/24 dev enx001ec0d1c337
sudo ethtool --set-plca-cfg enx001ec0d1c337 enable on node-id 0 node-cnt 8 \
    to-tmr 32 burst-cnt 0 burst-tmr 128
ping -c 5 192.168.1.100
```

---

## 9. 时间线

- 2026-10-23 上午：阅读旧 handover、厂商文档、LAN8651 手册；核对 J7 接线/电平。
- 2026-10-23 中午：回移 mainline PLCA + v1.1 LAN865x/OA-TC6 到 6.1；编译
  zImage/DTB/模块；增量部署到 p3；启动成功。
- 2026-10-23 下午（前半）：板端 probe 失败（RX 全 0）；用 GPIO/IRQ/位翻转/四种
  SPI 模式定位；主机 USB 适配器 PLCA Master 验证；交叉编译 ethtool 6.11。
- 2026-10-23 下午（后半）：发现 J7 SPI0 实际走 **RM_IO8-11（B0-B3）**，修正 DTS；
  禁用 UART2/UART3；回环通过；LAN8651 link up；PLCA 主从、iperf、UDP 全部通过。
- 待办：最终镜像固化、25 MHz/多节点/长稳（可选）。
