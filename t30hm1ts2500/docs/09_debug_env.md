# 调试环境与设备映射（实测）

> **本篇记录的是"实测的"环境映射**：哪台设备在哪个口、哪个工具链在哪。
> 要从零搭环境，请先看 [`docs/12_dev_env_setup.md`](12_dev_env_setup.md)。

## 本篇目录

| 章节 | 内容要点 |
| --- | --- |
| 1 | USB 设备与串口映射（实测） |
| 2 | 调试器连接状态 |
| 3 | 主机侧 USB-10T1S 配置 |
| 4 | 工具链路径 |
| 5 | 常用命令 |
| 6 | 设备名固定化（udev） |
| 7 | 常见错误与排查 |

---

## 1. USB 设备与串口映射

`lsusb` 与 `udevadm` 实测（Ubuntu 26.04，2026-11）：

| 调试器 | USB ID | 串口 | udev 标识 |
| --- | --- | --- | --- |
| STLINK-V3 | `0483:3754` | `/dev/ttyACM0` | `STMicroelectronics_STLINK-V3_003D001D3433510737363934` |
| J-Link OB | `1366:0105` | `/dev/ttyACM1` | `SEGGER_J-Link_000024060502` |
| CH343P #1 | `1a86:55d3` | `/dev/ttyACM2` | serial `5C84339305` |
| CH343P #2 | `1a86:55d3` | `/dev/ttyACM3` | serial `56D1060726` |
| FT2232H (HPM) | `0403:6010` | `/dev/ttyUSB0` (A/JTAG), `/dev/ttyUSB1` (B/UART) | `FTDI_Dual_RS232-HS` |
| DAP MiniWiggler (TC387) | `058b:0043` | `/dev/ttyUSB2` | `IFX_DAS_JDS_TriBoard_TC2XX_V2.0_TB825NR5` |
| USB-10T1S | `184f:0051` | — | 网卡 `enx001ec0d1c337`，MAC `00:1e:c0:d1:c3:37` |

按 VID:PID 稳定识别，不依赖枚举顺序：
```bash
for d in /dev/ttyACM* /dev/ttyUSB*; do
  udevadm info -q property -n $d | grep -E "ID_USB_VENDOR_ID|ID_USB_MODEL_ID|ID_USB_SERIAL"
done
```

## 2. 调试器连接状态

| 调试器 | 工具 | 状态 |
| --- | --- | --- |
| STLINK-V3 | openocd 0.12 / st-info | ✅ SWD 连接 STM32H723（`target/stm32h7x.cfg`）；VCP `/dev/ttyACM0` 115200 |
| J-Link OB | JLinkExe | ✅ `Cortex-M7 identified` (S32K312) |
| FT2232H | openocd | 待验证（HPM agent） |
| DAP MiniWiggler | openocd/DAS | 待验证（TC387 agent） |

### STLINK（STM32H723）验证

```bash
st-info --probe
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "init; halt"
```

连接失败时按顺序查：
1. SWDIO/SWCLK 接线断开或与 GND 接反；
2. NRST 未接，目标固件禁用了 SWD 引脚（需 connect-under-reset 或擦除）；
3. STLINK 与目标之间缺少 GND；
4. 目标是否上电（3.3 V），以及芯片型号是否选对（H723 用 `stm32h7x.cfg`）。

> **历史记录（F407，已停用）**：旧板曾出现 `Failed to enter SWD mode` / `init mode failed`，
> 排查后重接 SWDIO 可连（chipid=0x413）。该板 2026-11-20 已物理损坏停用，记录仅供追溯，
> **不要把它当成 H723 的问题**。

## 3. 主机侧 USB-10T1S 配置

```bash
sudo ethtool --set-plca-cfg enx001ec0d1c337 enable on node-id 0 node-cnt 8 to-tmr 32
sudo ip addr replace 192.168.10.10/24 dev enx001ec0d1c337
```

实测：
```
Speed: 10Mb/s, Duplex: Half, Link detected: yes
PLCA status: on
```

> 重启或重插 USB 后 PLCA 配置丢失，需重新执行 `host/setup_usb10t1s.sh`。

## 4. 工具链

| 工具 | 版本/路径 |
| --- | --- |
| arm-none-eabi-gcc | 14.2.1 (系统) |
| openocd | 0.12.0 |
| JLinkExe | SEGGER (系统) |
| TriCore GCC | `/opt/tricore-gcc-13.4.1` |
| HPM RISC-V GCC | `<home>/hpm5e31/toolchains/riscv32-unknown-elf-newlib-multilib` |
| hpm_sdk | `<home>/hpm5e31/sdk_env_v1.13.0` |
| NXP RTD | `<home>/s32k3/RTD` |
| DAS (Infineon) | `/opt/Tools/DAS/8.3.0` |

## 5. 常用命令

```bash
# 串口命令 (自动识别板子)
./tools/s25.py --auto h723 -c "s2500 info"
./tools/s25.py -p /dev/ttyUSB1 -b 115200 -c "help"

# 主机总线测试
./host/bus_test.py plca
./host/bus_test.py ping 11 12 13
./host/bus_test.py iperf 11

# 驱动单元测试 (无硬件)
gcc -std=c11 -Icommon/s2500 tools/sim/s2500_sim.c common/s2500/s2500.c -o /tmp/sim && /tmp/sim
```

---

## 6. 设备名固定化（udev）

`/dev/ttyACM*` / `/dev/ttyUSB*` 的编号会随插拔顺序变化，导致"连错板子"。
完整规则文件与采集脚本见 [`docs/12_dev_env_setup.md`](12_dev_env_setup.md) 第 5 节，核心思路：

```
按 VID:PID（同型号再用序列号区分）建软链接
  -> /dev/s2500-stm32  /dev/s2500-hpm  /dev/s2500-s32k
  -> /dev/s2500-tc387  /dev/s2500-rk3506
之后所有脚本都用固定名，或继续用 tools/s25.py --auto
```

验证：

```bash
ls -l /dev/s2500-*
udevadm info -q property -n /dev/s2500-hpm | grep -E "ID_VENDOR_ID|ID_MODEL_ID"
```

## 7. 常见错误与排查

| 现象 | 原因 | 处置 |
| --- | --- | --- |
| `lsusb` 看不到设备 | USB 线只有充电芯；或虚拟机没直通 | 换数据线；VM 里直通 USB |
| 串口能打开但收不到数据 | TX/RX 没交叉；波特率错（RK3506 是 1.5 M） | 交叉接线；改波特率 |
| 收到的全是乱码 | 波特率不匹配 / GND 没共地 | 统一 115200 + 共地 |
| `st-info --probe` 失败 | STLINK NRST 没接；目标没供电 | 接 NRST；量 3.3 V |
| `openocd` 报 LIBUSB_ERROR_ACCESS | udev 规则没生效 | 加 `plugdev` 组，重插 |
| `JLinkExe` 连不上 S32K312 | 没装 SEGGER 包 / device 名错 | 装 .deb；`-device S32K312` |
| FT2232H 两个口不知道用哪个 | 通道 A = JTAG，通道 B = UART | 用 `/dev/serial/by-id/` 下的名字 |
| DAP 的串口找不到 | DAS 服务没起 | `sudo /opt/Tools/DAS/8.3.0/bin/das64` |
| RK3506 串口打不开 | 需要 1.5 Mbit/s | `s25.py -b 1500000` |
| 多个板子同时插着分不清 | 编号乱序 | 用 udev 固定名或 `--auto` |

## 8. 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 从零搭环境（含 udev 全文） | [`docs/12_dev_env_setup.md`](12_dev_env_setup.md) |
| 串口命令助手 | [`tools/s25.py`](../tools/s25.py) |
| 硬件排障 | [`docs/10_hardware_troubleshooting.md`](10_hardware_troubleshooting.md) |
