# 开发环境搭建（从零到点亮，Ubuntu 22.04 / 24.04）

本篇面向**第一次接触本项目的工程师**：拿到一台全新的 Ubuntu、一块 S2500 板子、一根 USB 线，
按本篇顺序做完，就能编译、烧写、打开串口 shell、ping 通、跑通 iperf。

> **时间预算**：网络正常时 60–90 分钟（其中下载工具链占 40 分钟以上）。
> **前置条件**：Ubuntu 22.04 LTS 或 24.04 LTS（物理机最佳，虚拟机需开 USB 直通），
> 磁盘 ≥ 30 GB 空闲，内存 ≥ 8 GB，至少 3 个可用 USB 口。

## 本篇目录

| 章节 | 内容要点 |
| --- | --- |
| 1 | 目标、产出与角色分工 |
| 2 | 主机硬件/系统要求与流程总览 |
| 3 | 步骤 1：系统准备（代理、时区、用户组） |
| 4 | 步骤 2：必备软件包（apt 清单 + 逐条说明） |
| 5 | 步骤 3：串口权限与 udev 规则（固定设备名） |
| 6 | 步骤 4：五套交叉工具链安装与验证 |
| 7 | 步骤 5：调试器（STLINK / J-Link / FT2232H / DAP / DAS） |
| 8 | 步骤 6：Python 依赖 |
| 9 | 步骤 7：获取仓库、首次编译、无硬件单测 |
| 10 | 步骤 8：主机 USB-10T1S 网络与 PLCA 配置 |
| 11 | 步骤 9：VS Code 工程与断点调试 |
| 12 | 步骤 10：十条验证清单 |
| 13 | 内网/离线/代理环境 |
| 14 | 常见安装错误速查 |
| 15 | 一键安装脚本（可直接复制） |

---

## 1. 目标、产出与角色分工

做完本篇，应该得到下面 **6 个可验证的产出**：

| # | 产出 | 验证命令 | 期望 |
| --- | --- | --- | --- |
| 1 | 主机能编译通用驱动 | `make -C common/s2500 check` | 无 error / warning |
| 2 | 串口能进 shell | `./tools/s25.py --auto hpm -c "help"` | 打印命令列表 |
| 3 | 能读芯片 ID | `./tools/s25.py --auto hpm -c "s2500 reg 0 0"` | `0x00000011` |
| 4 | 环回自检通过 | `... -c "s2500 selftest"` | PCS / PHY 全 PASS |
| 5 | 主机网卡 PLCA 正常 | `ethtool --get-plca-status enx001ec0d1c337` | `Status: on` |
| 6 | 能 ping 通板子 | `ping -c 3 192.168.10.12` | 0% 丢包 |

不同角色只需要其中一部分：

| 角色 | 必须完成 | 可以跳过 |
| --- | --- | --- |
| 硬件工程师 | 3、5、6 | 第 6 节除 ARM 外的交叉工具链 |
| 嵌入式软件（MCU） | 1–6 全部 | — |
| Linux 驱动（RK3506） | 1、3、5、第 6.5 节 | TriCore / HPM 工具链 |
| 测试 / 验收 | 2、3、5、6、第 12 节 | 全部编译类 |
| 只写文档 | 1、第 8 节 | 其余 |

---

## 2. 主机要求与流程总览

### 2.1 主机要求

| 项 | 最低 | 推荐 | 说明 |
| --- | --- | --- | --- |
| 系统 | Ubuntu 22.04 LTS | Ubuntu 24.04 LTS | 本仓库实测环境为 24.04 / 26.04 |
| CPU | 2 核 | 4 核以上 | 需要并行编译 lwIP + 5 个工程 |
| 内存 | 8 GB | 16 GB | 并行编译 TriCore 时吃内存 |
| 磁盘 | 30 GB 空闲 | 60 GB | 5 套工具链约 8 GB，内核源码约 5 GB |
| USB-A 口 | 3 个 | 5 个 | 调试器 + 串口 + USB-10T1S 需同时在线 |
| 网络 | 可访问外网（可走代理） | — | 工具链下载 |
| 内核 | ≥ 6.9（PLCA 的 ethtool 接口） | ≥ 6.11 | `uname -r`；低版本只能跑 CSMA |

> **虚拟机注意**：VMware / VirtualBox 必须开启 USB 3.0 控制器并把调试器、USB 网卡
> 直通给虚拟机。PLCA 的 `ethtool --set-plca-cfg` 在部分虚拟网卡上不可用，建议物理机。

### 2.2 一张图看懂整个流程

```
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │ 步骤 1  系统准备        apt 源 / 代理 / 用户组 / 时区                          │
 │         ↓                                                                    │
 │ 步骤 2  必备软件包      build-essential gcc-arm-none-eabi openocd             │
 │                        minicom iperf(必须 v2) ethtool tcpdump pandoc...       │
 │         ↓                                                                    │
 │ 步骤 3  串口权限        dialout 组 + /etc/udev/rules.d/99-s2500.rules         │
 │                        (固定 /dev/s2500-* 名字，避免 ttyACM 编号乱跳)          │
 │         ↓                                                                    │
 │ 步骤 4  交叉工具链      ARM(STM32H723/S32K) · RISC-V(HPM) · TriCore(TC387)   │
 │                        · Linux(RK3506) · 主机原生 gcc(单测/文档)               │
 │         ↓                                                                    │
 │ 步骤 5  调试器          STLINK-V3 / J-Link OB / FT2232H / DAP MiniWiggler    │
 │         ↓                                                                    │
 │ 步骤 6  Python          pyserial(串口助手) matplotlib(文档配图)               │
 │         ↓                                                                    │
 │ 步骤 7  仓库 + 编译     git clone → make check → 无硬件协议单测               │
 │         ↓                                                                    │
 │ 步骤 8  主机网络        ip addr 192.168.10.10/24 + ethtool PLCA 协调者 ID 0    │
 │         ↓                                                                    │
 │ 步骤 9  IDE             VS Code + C/C++ + Cortex-Debug + launch.json         │
 │         ↓                                                                    │
 │ 步骤 10 验证清单        10 条命令，全绿即可交付                                │
 └──────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 步骤 1：系统准备

### 3.1 确认系统版本与内核

```bash
lsb_release -a                 # 22.04 / 24.04
uname -r                       # 用 PLCA 需要 >= 6.9（推荐 >= 6.11）
nproc                          # 编译并行度
df -h ~                        # 剩余空间 >= 30G
id                             # 确认自己在 sudo / dialout 组
```

### 3.2 配置代理（内网必须）

本仓库开发环境的代理为 `http://<proxy-host>:7890`。**只在需要联网时开，用完可以关**。

```bash
# 临时（当前 shell 有效）
export http_proxy=http://<proxy-host>:7890
export https_proxy=$http_proxy        # 注意是 http_proxy 的值，别写成 https_proxy
export no_proxy=localhost,127.0.0.1,192.168.0.0/16,10.0.0.0/8

# 验证
curl -sI https://github.com | head -1        # 期望 HTTP/1.1 200 或 301
```

让 `sudo apt` 也走代理：

```bash
sudo tee /etc/apt/apt.conf.d/95proxy >/dev/null <<'EOF'
Acquire::http::Proxy  "http://<proxy-host>:7890";
Acquire::https::Proxy "http://<proxy-host>:7890";
EOF
```

让 `git` / `pip` 也走代理（可选）：

```bash
git config --global http.proxy http://<proxy-host>:7890
mkdir -p ~/.config/pip
printf '[global]\nproxy = http://<proxy-host>:7890\n' > ~/.config/pip/pip.conf
```

> ⚠️ **常见错误**：写成 `export https_proxy=$https_proxy` 导致变量为空 → 所有下载超时。
> 正确写法是 `export https_proxy=$http_proxy`。

### 3.3 用户组与时区

```bash
sudo usermod -aG dialout,plugdev,sudo "$USER"   # 重新登录后生效
sudo timedatectl set-timezone Asia/Shanghai     # 实测/日志时间对齐
```

> 串口在 `dialout` 组；USB 设备在 `plugdev` 组。改完必须**重新登录**（或 `newgrp dialout`）。

---

## 4. 步骤 2：必备软件包

### 4.1 一条命令装完基础包

```bash
sudo apt update
sudo apt install -y \
  build-essential git curl wget ca-certificates unzip usbutils pkg-config \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi gdb-multiarch \
  openocd stlink-tools \
  minicom picocom screen \
  iproute2 iputils-ping ethtool iperf tcpdump net-tools \
  python3 python3-pip python3-serial python3-matplotlib \
  bc flex bison libssl-dev libncurses-dev libusb-1.0-0-dev libudev-dev \
  device-tree-compiler dosfstools mtools \
  cmake ninja-build make \
  pandoc texlive-xetex texlive-lang-chinese fonts-noto-cjk poppler-utils graphviz \
  jq tree rsync
```

> `iperf` 是 **iperf2**（2.1.x）。板端 `lwiperf` 只兼容 iperf2，装成 `iperf3` 会一直连不上
> （见第 14 节）。

### 4.2 每个包的用途

| 包 | 用途 | 谁需要 |
| --- | --- | --- |
| `build-essential` | gcc / make / 头文件，编译单测与主机工具 | 全部 |
| `gcc-arm-none-eabi` | ARM Cortex-M 交叉编译器（STM32H723 / S32K312） | MCU |
| `gdb-multiarch` | 多架构 GDB（arm / riscv32 / tricore 通用） | 调试 |
| `openocd` | STLINK / FT2232H / DAP 的通用调试服务 | MCU |
| `stlink-tools` | `st-info --probe`、`st-flash` | STM32 |
| `minicom` / `picocom` | 手工串口终端（排障用） | 全部 |
| `ethtool` | **PLCA 配置的唯一入口**（`--set-plca-cfg`） | 总线 |
| `iperf` | iperf2 服务端/客户端（板端 lwiperf 的对端） | 性能 |
| `tcpdump` | 抓 ARP / ICMP，验证帧是否上到主机 | 排障 |
| `python3-serial` | `tools/s25.py` 依赖 | 全部 |
| `python3-matplotlib` | `docs/images/gen_images.py` 生成配图 | 文档 |
| `device-tree-compiler` | RK3506 设备树编译 | RK3506 |
| `pandoc` + `texlive-xetex` + `texlive-lang-chinese` | 可选：把 `docs/*.md` 合成 PDF | 文档 |
| `fonts-noto-cjk` | 中文 PDF / 配图字体 | 文档 |
| `poppler-utils` | `pdftoppm` 抽查 PDF、`pdftotext` 转数据手册 | 文档 |
| `graphviz` | 流程图（排障决策树） | 文档 |

### 4.3 验证

```bash
arm-none-eabi-gcc --version | head -1     # 期望 12.x / 13.x / 14.x
openocd --version 2>&1 | head -1          # 期望 0.12.0 以上
iperf --version | head -1                 # 期望 iperf version 2.1.x（不能是 3）
ethtool --version                         # PLCA 需要 >= 6.19（Ubuntu 24.04 自带）
```

---

## 5. 步骤 3：串口权限与 udev 固定设备名

### 5.1 为什么必须做

`/dev/ttyACM0..3`、`/dev/ttyUSB0..3` 的编号**随插拔顺序变化**。表现为"昨天能连、今天连不上"，
或者命令发到了别人的板子上。解决办法：按 USB VID:PID + 序列号建固定软链接。

### 5.2 本项目用到的 USB 设备

| 设备 | USB VID:PID | 子系统 | 本仓库固定名 |
| --- | --- | --- | --- |
| STLINK-V3（STM32H723） | `0483:3754` | CDC-ACM | `/dev/s2500-stm32`（VCP 也常枚举为 ttyACM0） |
| J-Link OB（S32K312） | `1366:0105` | CDC-ACM | `/dev/s2500-s32k` |
| CH343P #1（S32K312 串口） | `1a86:55d3` | CDC-ACM | `/dev/s2500-s32k-uart` |
| CH343P #2（RK3506 串口） | `1a86:55d3` | CDC-ACM | `/dev/s2500-rk3506` |
| FT2232H（HPM5361） | `0403:6010` | ttyUSB 通道 B | `/dev/s2500-hpm` |
| DAP MiniWiggler（TC387） | `058b:0043` | ttyUSB | `/dev/s2500-tc387` |
| USB-10T1S（总线适配器） | `184f:0051` | usbnet | 网卡名由 MAC 决定 |

### 5.3 先采集真实序列号

**每块板都不同，必须自己采**：

```bash
for d in /dev/ttyACM* /dev/ttyUSB*; do
  echo "== $d"
  udevadm info -q property -n "$d" | \
    grep -E "ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL_SHORT|ID_USB_INTERFACE_NUM"
done
```

### 5.4 规则文件模板

把采到的 `ATTRS{serial}` 填进去：

```bash
sudo tee /etc/udev/rules.d/99-s2500.rules >/dev/null <<'EOF'
# ---- S2500 开发环境：串口固定名字 + 免 root 访问 ----
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3754", \
  SYMLINK+="s2500-stm32", MODE="0666", GROUP="dialout"

SUBSYSTEM=="tty", ATTRS{idVendor}=="1366", ATTRS{idProduct}=="0105", \
  SYMLINK+="s2500-s32k", MODE="0666", GROUP="dialout"

# 两颗 CH343P 用序列号区分（替换成你自己采到的值）
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3", \
  ATTRS{serial}=="5C84339305", SYMLINK+="s2500-s32k-uart", MODE="0666", GROUP="dialout"
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3", \
  ATTRS{serial}=="56D1060726", SYMLINK+="s2500-rk3506", MODE="0666", GROUP="dialout"

# FT2232H 只有通道 B 是串口
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6010", \
  ATTRS{bInterfaceNumber}=="01", SYMLINK+="s2500-hpm", MODE="0666", GROUP="dialout"

SUBSYSTEM=="tty", ATTRS{idVendor}=="058b", ATTRS{idProduct}=="0043", \
  SYMLINK+="s2500-tc387", MODE="0666", GROUP="dialout"

# ---- 调试器免 root ----
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3754", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="1366", ATTR{idProduct}=="0105", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6010", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="058b", ATTR{idProduct}=="0043", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="55d3", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="184f", ATTR{idProduct}=="0051", MODE="0666", GROUP="plugdev"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
ls -l /dev/s2500-*
```

### 5.5 验证

```bash
ls -l /dev/s2500-*                        # 期望出现 6 个软链接
./tools/s25.py -p /dev/s2500-hpm -c "help"
```

> 软链接没出现：① `ATTRS{serial}` 填错；② 规则文件语法错（`udevadm test` 可查）；
> ③ 被别的规则先匹配（文件名用 `99-` 保证最后执行）。

---

## 6. 步骤 4：五套交叉工具链

### 6.1 总览（本仓库实测路径）

| 平台 | 编译器 | 本机实测版本/路径 | 官方获取方式 |
| --- | --- | --- | --- |
| STM32H723 | `arm-none-eabi-gcc` | 系统包 14.2.1（`/usr/bin`） | `apt install gcc-arm-none-eabi` 或 ARM 官方 GNU Toolchain |
| S32K312 | `arm-none-eabi-gcc` | 同上 | 同上（NXP RTD 亦可自带） |
| HPM5361 | `riscv32-unknown-elf-gcc` | `<riscv-toolchain>` | HPMicro `sdk_env` 一键脚本 |
| TC387 | `tricore-gcc` | `/opt/tricore-gcc-13.4.1` | Infineon Free TriCore Entry Tool Chain |
| RK3506 | 板端 gcc / 交叉 gcc | 板端原生编译 | Rockchip SDK |

### 6.2 ARM（STM32H723 + S32K312）

```bash
sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
arm-none-eabi-gcc --version | head -1
```

> **Cortex-M7 支持（STM32H723）**：系统包 `arm-none-eabi-gcc 14.2.1` 已支持
> `-mcpu=cortex-m7`（H723 使用 `-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard`），
> **不需要安装新的工具链**。H723 的 HAL 来源是 **STM32CubeH7 固件包**
> （来自 STM32CubeH7 / CubeMX 生成工程的 `Drivers/STM32H7xx_HAL_Driver` + CMSIS Device，已随
> `stm32h723/Drivers/` 入库），不依赖 CubeMX 现场生成；工程用 CMake 构建，
> 现有 `cmake / ninja-build` 已够用。

需要更新版本（例如用 Cortex-M7 的新特性）：

```bash
# 下载: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
# 选择 x86_64 Linux 宿主 + arm-none-eabi 目标，解压到 /opt
sudo tar -xf arm-gnu-toolchain-*-x86_64-arm-none-eabi.tar.xz -C /opt
sudo ln -sfn /opt/arm-gnu-toolchain-*-x86_64-arm-none-eabi/bin/* /usr/local/bin/
arm-none-eabi-gcc --version | head -1
```

### 6.3 RISC-V（HPM5361）

HPMicro 官方推荐 `sdk_env`，它会自动下载匹配版本的 RISC-V GCC 与 hpm_sdk：

```bash
mkdir -p ~/hpm5e31 && cd ~/hpm5e31
git clone https://github.com/hpmicro/sdk_env.git
cd sdk_env && ./install.sh          # 交互式选择 hpm_sdk 版本与工具链
source ~/hpm5e31/sdk_env/env.sh     # 每开新 shell 都要 source
riscv32-unknown-elf-gcc --version | head -1
```

本仓库实测路径（可直接复用，不必重装）：

```
<home>/hpm5e31/sdk_env_v1.13.0/hpm_sdk
<home>/hpm5e31/toolchains/riscv32-unknown-elf-newlib-multilib
```

### 6.4 TriCore（TC387）

Infineon 的 TriCore GCC 需从官网下载（Free TriCore Entry Tool Chain），解压到 `/opt`：

```bash
sudo mkdir -p /opt
sudo tar -xf TriCore_Free_Toolchain_*.zip -C /opt
sudo mv /opt/Infineon/TriCore-* /opt/tricore-gcc-13.4.1 2>/dev/null || true
/opt/tricore-gcc-13.4.1/bin/tricore-gcc --version | head -1
```

本仓库实测：`/opt/tricore-gcc-13.4.1`。`tc387/build.sh` 已把该路径写进 `PATH`，
可用环境变量 `TRICORE_GCC` 覆盖。

### 6.5 RK3506（Linux）

RK3506 上的 S2500 是**内核驱动 + out-of-tree 模块**，编译发生在板端（或 Rockchip SDK 交叉编译）：

```bash
# 板端（推荐，版本绝对匹配）
uname -r                                  # 例: 6.1.x
ls /lib/modules/$(uname -r)/build         # 必须存在（内核头文件）
cd /path/to/s2500-driver
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

交叉编译需要 Rockchip SDK 的 `CROSS_COMPILE` 与 `ARCH=arm`，详见
`rk3506/README.md`。

> **RK3506 网络（2026-11-20 变更）**：管理口 **eth0 是 DHCP**，地址由现场网络分配；
> SSH/部署前先在串口执行 `ip -br addr` 确认。
> **t1s0 仍固定 `192.168.10.15`**，业务测试不受影响。

### 6.6 主机原生 gcc

```bash
gcc --version | head -1      # 用于 tools/sim 协议单测
```

> **无硬件也能验证驱动**：`tools/sim/s2500_sim.c` 用软件模拟 S2500 的 SPI 行为，
> 没有板子也能跑通 chunk 重组、FCS 校验、最小帧填充等逻辑（见第 9 节）。

---

## 7. 步骤 5：调试器

### 7.1 四类调试器对照

| 板 | 调试器 | 上位机工具 | 验证命令 | 备注 |
| --- | --- | --- | --- | --- |
| STM32H723 | STLINK-V3 | openocd / stlink-tools | `st-info --probe` | SWD，openocd `target/stm32h7x.cfg`；USART1→VCP 115200 |
| S32K312 | J-Link OB | JLinkExe / openocd | `JLinkExe -device S32K312 -if SWD -speed 4000` | 免驱，需装 SEGGER 包 |
| HPM5361 | 板载 FT2232H | openocd | `openocd -f interface/ftdi/ftdi.cfg ...` | 通道 A=JTAG，通道 B=UART |
| TC387 | DAP MiniWiggler | DAS / AURIX Flasher | `lsusb | grep 058b` | 需装 DAS 8.3.0 |

> **历史平台**：STM32F407（STLINK-V3，`target/stm32f4x.cfg`）板子已损坏，2026-11-20 停用；
> 当年"SWD 连不上"的记录见 [docs/10](10_hardware_troubleshooting.md) §2，属旧板问题，
> 不要套到 H723 上。

### 7.2 SEGGER J-Link

```bash
# 下载: https://www.segger.com/downloads/jlink/
sudo dpkg -i JLink_Linux_V*.deb
JLinkExe -nogui 1 -device S32K312 -if SWD -speed 4000 -autoconnect 1
# 期望: Cortex-M7 identified.  /  Found SW-DP
```

### 7.3 Infineon DAS（TC387）

```bash
# 官网下载 DAS 安装包，默认装到 /opt/Tools/DAS
ls /opt/Tools/DAS/8.3.0
sudo /opt/Tools/DAS/8.3.0/bin/das64 &     # TAS 服务，AURIX Flasher 依赖它
```

### 7.4 openocd（通用）

```bash
# STLINK + STM32H7
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "init; targets; exit"

# FT2232H + HPM5361（板级配置见 hpm5361/README.md）
openocd -f interface/ftdi/ftdi.cfg -c "transport select jtag" \
        -f target/hpm5361.cfg -c "init; halt; exit"
```

> ⚠️ 若报 `libusb_open() failed with LIBUSB_ERROR_ACCESS`，是 udev 规则没生效：
> 把用户加进 `plugdev` 并重插设备。**不要用 `sudo openocd`**（会污染工程文件属主）。

---

## 8. 步骤 6：Python 依赖

```bash
sudo apt install -y python3-serial python3-matplotlib python3-pil
# 或用 pip（需要代理）
python3 -m pip install --user pyserial matplotlib pillow
```

| 脚本 | 依赖 | 用途 |
| --- | --- | --- |
| `tools/s25.py` | `pyserial` | 串口命令助手（按 VID:PID 自动找口） |
| `host/bus_test.py` | 标准库 + `ping` | 主机侧总线测试 |
| `host/acceptance.sh` | bash + `iperf` + `ethtool` | 一键验收 |
| `host/coord_test.sh` | bash + `ethtool` | **PLCA 协调者验证**：自动把被测板设为 ID 0、主机降为 ID 7，跑完自动恢复 |
| `docs/images/gen_images.py` | `matplotlib`（+ `dot`） | 生成文档配图 |
| （可选）`pandoc` | `pandoc` + `xelatex` + `ctex` | 自行把 `docs/*.md` 合成总 PDF 手册 |

```bash
python3 -c "import serial; print('pyserial', serial.__version__)"
python3 -c "import matplotlib; print('matplotlib', matplotlib.__version__)"
dot -V; pandoc --version | head -1; xelatex --version | head -1
```

---

## 9. 步骤 7：获取仓库、首次编译、无硬件单测

### 9.1 获取

```bash
git clone https://github.com/weifengdq/embedded
cd embedded/t30hm1ts2500
git config user.name  "你的名字"
git config user.email "you@example.com"
```

### 9.2 无硬件也要先跑的三项

```bash
# 1) 通用驱动语法/接口检查（不需要任何板子）
make -C common/s2500 check

# 2) 协议模拟单元测试：软件模拟 S2500 的 SPI 应答
gcc -std=c11 -Icommon/s2500 tools/sim/s2500_sim.c common/s2500/s2500.c \
    -o /tmp/s2500_sim && /tmp/s2500_sim

# 3) 主机侧脚本自检
bash -n host/setup_usb10t1s.sh && bash -n host/plca.sh
python3 -m py_compile tools/s25.py host/bus_test.py
```

期望：第 2 步打印 `ALL TESTS PASSED`（覆盖字节序、chunk 重组、FCS、最小帧填充）。

### 9.3 编译某一平台

```bash
cd stm32h723 && ./build.sh        # 或 hpm5361 / s32k312 / tc387（F407 为历史工程）
ls -lh build/                              # 期望 *.elf / *.bin / *.hex
```

各平台编译/烧写细节见 `<board>/README.md`。

---

## 10. 步骤 8：主机 USB-10T1S 网络与 PLCA

### 10.1 找到网卡

```bash
ip -br link | grep -i enx              # 名字来自 MAC: enx001ec0d1c337
lsusb | grep 184f                      # EVB-LAN8670-USB
```

### 10.2 一键配置

```bash
sudo ./host/setup_usb10t1s.sh
# 等价于：
#   sudo ip addr replace 192.168.10.10/24 dev enx001ec0d1c337
#   sudo ethtool --set-plca-cfg enx001ec0d1c337 enable on node-id 0 node-cnt 8 to-tmr 32
#   sudo ip link set enx001ec0d1c337 up
```

### 10.3 验证

```bash
ethtool enx001ec0d1c337 | grep -E "Speed|Duplex|Link detected"
#   期望: Speed: 10Mb/s   Duplex: Half   Link detected: yes
ethtool --get-plca-status enx001ec0d1c337     # 期望: Status: on
ip -s link show enx001ec0d1c337 | tail -4
```

> ⚠️ **USB 重插后 PLCA 配置会丢失**，跟随者立即 `PST=0` 发不出帧。重跑脚本即可。
> 想永久生效可写成 systemd unit。

---

## 11. 步骤 9：VS Code 工程与断点调试

### 11.1 必装扩展

| 扩展 | 用途 |
| --- | --- |
| `ms-vscode.cpptools`（C/C++） | 代码导航、IntelliSense |
| `marus25.cortex-debug` | Cortex-M 断点调试（STM32 / S32K） |
| `ms-vscode.cmake-tools` | CMake 工程 |
| `mcu-debug.memory-view` | 外设寄存器查看（可选） |
| `yzhang.markdown-all-in-one` | 文档编写与预览 |

### 11.2 `.vscode/c_cpp_properties.json`

```json
{
  "configurations": [{
    "name": "Linux",
    "includePath": [
      "${workspaceFolder}/common/s2500",
      "${workspaceFolder}/common/lwip/src/include",
      "${workspaceFolder}/common/lwip-port",
      "${workspaceFolder}/common/letter-shell/src",
      "${workspaceFolder}/common/letter-shell-port",
      "${workspaceFolder}/stm32h723/Core/Inc",
      "${workspaceFolder}/stm32h723/Drivers/STM32H7xx_HAL_Driver/Inc"
    ],
    "defines": ["USE_HAL_DRIVER", "STM32H723xx"],
    "cStandard": "c11",
    "intelliSenseMode": "gcc-arm"
  }],
  "version": 4
}
```

### 11.3 `.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [{
    "name": "STM32H723 (STLINK + openocd)",
    "type": "cortex-debug",
    "request": "launch",
    "servertype": "openocd",
    "cwd": "${workspaceFolder}",
    "executable": "${workspaceFolder}/stm32h723/build/stm32h723_s2500.elf",
    "device": "STM32H723ZGT6",
    "configFiles": ["interface/stlink.cfg", "target/stm32h7x.cfg"],
    "runToEntryPoint": "main"
  },
  {
    "name": "S32K312 (J-Link)",
    "type": "cortex-debug",
    "request": "launch",
    "servertype": "jlink",
    "cwd": "${workspaceFolder}",
    "executable": "${workspaceFolder}/s32k312/build/s32k312_s2500.elf",
    "device": "S32K312",
    "interface": "swd",
    "runToEntryPoint": "main"
  }]
}
```

> 想看 chunk 重组过程，把断点打在 `s2500_recv()` 的循环内，条件写 `rx->len > 60`，
> 就能停在第一帧完整接收的时刻。

### 11.4 串口在 VS Code 里看

推荐 `ms-vscode.vscode-serial-monitor`，或在集成终端里：

```bash
picocom -b 115200 /dev/s2500-hpm        # Ctrl-A Ctrl-X 退出
```

---

## 12. 步骤 10：十条验证清单

按顺序执行，**全绿再开始改代码**（端口按你的板子替换）：

```bash
# 1  USB 设备是否都在
lsusb | grep -E "0483:3754|1366:0105|0403:6010|058b:0043|184f:0051"

# 2  串口软链接
ls -l /dev/s2500-*

# 3  工具链
arm-none-eabi-gcc --version | head -1 && iperf --version | head -1

# 4  shell 能进
./tools/s25.py --auto hpm -c "help"

# 5  SPI 通路（读 IDVER，期望 0x00000011）
./tools/s25.py --auto hpm -c "s2500 reg 0 0"

# 6  芯片信息（MAC / PHY / PLCA）
./tools/s25.py --auto hpm -c "s2500 info"

# 7  环回自检（不依赖总线）
./tools/s25.py --auto hpm -c "s2500 selftest"

# 8  主机网卡 + PLCA
ethtool --get-plca-status enx001ec0d1c337
ethtool enx001ec0d1c337 | grep -E "Speed|Link"

# 9  板子加入 PLCA 并 ping
./tools/s25.py --auto hpm -c "s2500 plca id 2 ncnt 8 totmr 32 en 1"
./tools/s25.py --auto hpm -c "s2500 status"        # PST=1
ping -c 3 192.168.10.12

# 10 吞吐（板端起 server，主机用 iperf2）
./tools/s25.py --auto hpm -c "iperf -s" &
iperf -c 192.168.10.12 -t 10 -i 5 -f m
```

---

## 13. 内网 / 离线 / 代理环境

| 场景 | 做法 |
| --- | --- |
| 只能走 HTTP 代理 | 第 3.2 节的 `http_proxy` + `apt.conf.d` + `pip.conf` 三处都要配 |
| 完全离线 | 联网机器上 `apt-get download` 全部 .deb，或直接用 Docker 镜像；工具链拷 `/opt` |
| 公司 MITM 证书 | CA 放到 `/usr/local/share/ca-certificates/` 后 `sudo update-ca-certificates` |
| git ssh 端口被墙 | 改用 HTTPS 克隆：`git clone https://github.com/weifengdq/embedded` |
| 时间不同步导致 apt 报错 | `sudo timedatectl set-ntp true` |

---

## 14. 常见安装错误速查

| 现象 | 根本原因 | 解决 |
| --- | --- | --- |
| `arm-none-eabi-gcc: command not found` | 装了 meta 包但 PATH 未刷新 | 重开 shell；`dpkg -L gcc-arm-none-eabi` 查实际路径 |
| `iperf3: error - unable to connect to server` | 板端是 **lwiperf，只支持 iperf2** | 改用 `iperf`（v2），不要用 `iperf3` |
| `ethtool: bad command line arguments` | ethtool 版本太老，没有 PLCA 命令 | Ubuntu 24.04 自带 ≥ 6.19；否则源码编译 |
| `--set-plca-cfg ... Operation not permitted` | 没用 sudo，或网卡不支持 PLCA | 加 `sudo`；确认网卡是 LAN8670 |
| `Permission denied: '/dev/ttyACM0'` | 不在 `dialout` 组 | `sudo usermod -aG dialout $USER` 后重新登录 |
| `openocd: LIBUSB_ERROR_ACCESS` | udev 规则未生效 | 加 `plugdev`，重插设备，reload rules |
| `fatal error: lwip/opt.h: No such file` | include 路径漏了 `common/lwip/src/include` | 按 `docs/00_porting_guide.md` 第 3 节补齐 5 条 |
| `undefined reference to 'sys_now'` | `NO_SYS=1` 下需应用提供 `sys_now()` | 由 `s2500_app.c` 提供，确认进编译列表 |
| `! LaTeX Error: File 'ctex.sty' not found` | 缺中文 TeX 支持 | `sudo apt install texlive-lang-chinese texlive-xetex` |
| PDF 中文变方块 | 缺字体或未指定 CJK 字体 | `sudo apt install fonts-noto-cjk`，用 `pandoc` 合并 `docs/*.md` |
| `ModuleNotFoundError: No module named 'serial'` | 没装 pyserial | `sudo apt install python3-serial` |
| 命令报 `Command not Found` | 发的是 `\r\n`，letter-shell 只认 `\r` | 用 `tools/s25.py`（只发 `\r`） |
| `riscv32-unknown-elf-gcc: not found` | 没 source hpm 的 `env.sh` | `source ~/hpm5e31/sdk_env/env.sh` |
| `tricore-gcc: not found` | 未装或路径不对 | `export TRICORE_GCC=/opt/tricore-gcc-13.4.1` |

---

## 15. 一键安装脚本

存成 `~/setup_s2500_host.sh`，`chmod +x` 后执行。**只做安装，不改网络/PLCA 配置**。

```bash
#!/usr/bin/env bash
# setup_s2500_host.sh -- S2500 开发主机（Ubuntu 22.04/24.04）一键安装
set -eo pipefail

PROXY_IN="$PROXY"          # 用法: PROXY=http://<proxy-host>:7890 ./setup_s2500_host.sh
if [ -n "$PROXY_IN" ]; then
  export http_proxy="$PROXY_IN" https_proxy="$PROXY_IN"
  export no_proxy="localhost,127.0.0.1,192.168.0.0/16,10.0.0.0/8"
fi

echo "==> [1/5] 基础软件包"
sudo apt update
sudo apt install -y build-essential git curl wget ca-certificates unzip usbutils \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi gdb-multiarch \
  openocd stlink-tools minicom picocom iproute2 iputils-ping ethtool iperf tcpdump \
  python3 python3-pip python3-serial python3-matplotlib bc flex bison \
  libssl-dev libncurses-dev libusb-1.0-0-dev libudev-dev device-tree-compiler \
  cmake ninja-build pandoc texlive-xetex texlive-lang-chinese fonts-noto-cjk \
  poppler-utils graphviz jq tree rsync

echo "==> [2/5] 用户组"
sudo usermod -aG dialout,plugdev "$USER" || true

echo "==> [3/5] 工具链版本"
arm-none-eabi-gcc --version | head -1
iperf --version | head -1
ethtool --version
pandoc --version | head -1

echo "==> [4/5] Python 依赖"
python3 -c "import serial, matplotlib; print('  ok pyserial+matplotlib')"

echo "==> [5/5] 完成。重新登录后执行:"
cat <<'EOF'
  ls -l /dev/s2500-*                      # 没有就先配 99-s2500.rules
  ./tools/s25.py --auto hpm -c "s2500 reg 0 0"
  sudo ./host/setup_usb10t1s.sh
EOF
```

---

## 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 10BASE-T1S / PLCA 是什么 | [`docs/13_10baset1s_primer.md`](13_10baset1s_primer.md) |
| 把驱动移植到新平台 | [`docs/00_porting_guide.md`](00_porting_guide.md) |
| 设备名、调试器实测记录 | [`docs/09_debug_env.md`](09_debug_env.md) |
| 总线不通怎么查 | [`docs/10_hardware_troubleshooting.md`](10_hardware_troubleshooting.md) |
| 命令怎么用 | [`docs/08_shell_commands.md`](08_shell_commands.md) |
| 交付验收清单 | [`docs/14_delivery_checklist.md`](14_delivery_checklist.md) |
