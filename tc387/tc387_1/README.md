# tc387_1 — TC387 TriBoard Letter-Shell (ASCLIN4, 921600, 无 LAN)

本工程以 `ref/tc387_uart_echo_gcc` 为蓝本, 在 **Ubuntu 26.04 + tricore-gcc 13.4.1 + CMake/Ninja** 下重建, 保留 `ASCLIN4 (P00.9 TX / P00.12 RX, 921600)` 调试串口与 `Letter-Shell` 命令行, 移除 `LAN8651` 与 `LwIP` 全部代码, 专注于 **MCU 基础诊断、回显与 Shell**。工程名已改为 `tc387_1`, 产物为 `build/gcc/tc387_1.{elf,hex,map}`。

* 原回显工程见 `ref/tc387_uart_echo_gcc` ( `init_uart4(4000000)` 的高级回显, 已改为 `921600` 的 Shell 版，详见 §5.4 优化 )
* 调试串口: **ASCLIN4, TX P00.9 / RX P00.12, 921600-8N1**, Linux 下为 `/dev/ttyACM0` (`1a86:55d3 CH340`) 或 `/dev/serial/by-id/usb-1a86_USB_Single_Serial*`, 备用 `picocom` / `pyserial`
* DAP 调试口: `058b:0043 Infineon DAS JDS TriBoard TC2XX V2.0` → `MaxiWiggler` / `FTDI`, Linux 下为 `/dev/ttyUSB0` (`FT2232H`) 仅用于 `TAS` 下载, 非日志口

---

## 1 硬件

| 信号 | TC387 Pin | 说明 |
| --- | --- | --- |
| UART TX | P00.9 | `IfxAsclin4_TX_P00_9_OUT`, `cmosAutomotiveSpeed4` |
| UART RX | P00.12 | `IfxAsclin4_RXA_P00_12_IN`, `pullUp`, `Ifx_RxSel_a` |
| LED/调试 | P00.5 | `core0_main` 入口置高, `Shell_Process` 中 `P00.5|P00.8` 置高指示调度 |
| DAP | USB 058b:0043 | `TAS` 下载, `FT2232H` 的 `1.1` 为 `ttyUSB0` |

> 本板为 `ref/tc387_uart_echo_gcc` 对应板, 硬件正常, 无需怀疑 `P00.9/12` 连线。

---

## 2 目录结构

```
tc387_1/
├── cmake/
│   ├── tricore-gcc-toolchain.cmake  # Linux: /opt/tricore-gcc/bin, 无 .exe, 适配 13.4.1
│   └── AurixProject.cmake           # 递归收集 + 排除 build/.ads/.settings
├── Configurations/
│   ├── Ifx_Cfg.h / Ifx_Cfg_Ssw.*    # 来自回显工程, 仅 STM 100k ticks/ms, 无 LAN
│   └── ConfigurationIsr.h           # ISR_PRIORITY_OS_TICK=10 (原99,现降低让UART抢占), 用于 STM 1ms
├── Libraries/
│   ├── iLLD/TC3xx/...               # Infineon iLLD (ASCLIN, STM, SCU, DTS, Port ...)
│   ├── Infra/                       # Bsp, Ssw, Platform
│   ├── Service/                     # SysSe
│   ├── UART/UART_Logging.c/h        # g_asc (ASCLIN4, 921600, TX 31 / RX 32, oversampling 16, FIFO 1024), Tx/Rx ISR + UART_Poll
│   └── pinmapper.pincfg
├── Shell/
│   ├── letter-shell/src/            # 上游 3.2.4: shell.c/h, shell_cfg.h, shell_cmd_list.c ...
│   ├── shell_cfg_user.h             # 256B 打印/命令, 1024B Shell缓冲, 8 历史, tick = g_TickCount_1ms
│   └── shell_port.c/h               # 环形缓冲 1024B + Shell_RxPushBulk, userShellRead/Write(shellPrint)
├── Lcf_Gnuc_Tricore_Tc.lsl           # 已增 .shellCommand/.shellVar (KEEP, PROVIDE)
├── Cpu0_Main.c                      # 主循环: STM 1ms + DTS + Shell + 轮询
├── Cpu1/2/3_Main.c                  # 仅同步, 空转
├── build.sh                         # Linux 一键脚本 (替代 build.ps1)
└── build/gcc/tc387_1.{elf,hex,map}  # 产物, 另有 tc387_1.hex 供烧录
```

---

## 3 新机环境搭建 (Ubuntu 26.04 全流程)

### 3.1 基础工具

```bash
sudo apt update
sudo apt install -y cmake ninja-build python3-serial python3-pip usbutils
cmake --version   # >=3.24, 实测 4.2.3
ninja --version   # 1.13.x
```

### 3.2 tricore-gcc 13.4.1 (NoMore201)

```bash
export http_proxy=http://w.x.y.z:7890 https_proxy=http://w.x.y.z:7890
wget https://github.com/NoMore201/tricore-gcc-toolchain/releases/download/13.4.1/tricore-gcc-13.4.1-linux.tar.gz -O /tmp/tricore-gcc-13.4.1-linux.tar.gz
echo "22dbe2fc5195a24736b0b091a2f53f609ad0f6a35b45bfa8db8c2176c28f9a70 /tmp/tricore-gcc-13.4.1-linux.tar.gz" | sha256sum -c
sudo mkdir -p /opt/tricore-gcc-13.4.1
sudo tar -xzf /tmp/tricore-gcc-13.4.1-linux.tar.gz -C /opt/tricore-gcc-13.4.1 --strip-components=1
sudo ln -sf /opt/tricore-gcc-13.4.1 /opt/tricore-gcc
echo 'export PATH=/opt/tricore-gcc/bin:$PATH' | sudo tee /etc/profile.d/tricore-gcc.sh
export PATH=/opt/tricore-gcc/bin:$PATH
tricore-elf-gcc --version  # 13.4.1
# 可选 11.3.0 (EEESlab) 但本工程以 13.4.1 验证
```

> 代理按 `~/.zshrc` 的 `https_proxy` 设置, 若无代理直接下载。

### 3.3 DAS / TAS (DAP 下载必需)

> Infineon 许可限制, 需从官网手动下载: https://www.infineon.com/cms/en/product/promopages/DAS/ (8.0.5 Linux, 4.61MB)

```bash
# 已随工程提供: ref/DAS_v8_0_5_Linux.tar.gz (4.8M) 与 ref/DAS_8.3.0_linux_x64.deb (18M)
sudo dpkg -i ref/DAS_8.3.0_linux_x64.deb  # 安装至 /opt/Tools/DAS/8.3.0
ls /opt/Tools/DAS/8.3.0/bin/tas_server  # 624K
# FTDI D2XX 驱动 (TAS 依赖)
sudo /opt/Tools/DAS/8.3.0/others/install_ftdi_drivers.sh
ls /usr/local/lib/libftd2xx.so*  # libftd2xx.so.1.4.27
sudo ldconfig && ldconfig -p | grep ftd2xx

# udev 规则 (已提供 90-infineon-tas.rules: SUBSYSTEM=="usb", ATTR{idVendor}=="058b", ATTR{idProduct}=="0043", MODE="0666")
sudo cp ref/aurix_flasher_linux-master/others/90-infineon-tas.rules /etc/udev/rules.d/ 2>/dev/null || echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="058b", ATTR{idProduct}=="0043", MODE="0666"' | sudo tee /etc/udev/rules.d/90-infineon-tas.rules
sudo udevadm control --reload-rules && sudo udevadm trigger

# 修正 systemd 服务 (原包 ExecStart=/opt/tas/bin/tas_server 不存在)
sudo mkdir -p /opt/tas/bin
sudo ln -sf /opt/Tools/DAS/8.3.0/bin/tas_server /opt/tas/bin/tas_server
sudo ln -sf /opt/Tools/DAS/8.3.0/bin/tas_server_console /opt/tas/bin/tas_server_console
sudo bash -c 'cat > /lib/systemd/system/tas-server.service <<EOF
[Unit]
Description=Tool Access Socket (TAS) Server
After=network.target
[Service]
User=tas
Group=tas
ExecStart=/opt/Tools/DAS/8.3.0/bin/tas_server
Restart=always
RestartSec=2
LimitNOFILE=1048576
LimitNPROC=64
PrivateTmp=true
ProtectHome=false
ProtectSystem=false
[Install]
WantedBy=multi-user.target
EOF'
sudo systemctl daemon-reload
sudo systemctl enable --now tas-server
systemctl status tas-server  # active (running)
ss -tlnp | grep 24817  # LISTEN 0.0.0.0:24817 tas_server
# 验证
./ref/aurix_flasher_linux-master/linux/aurix_flasher -id list  # -id 0 -> TC38x GBKANT TB67V7ID
# 若 ldd tas_server 提示 libftd2xx.so not found, 检查 /usr/local/lib 与 ldconfig
```

> `ref/aurix_flasher_linux-master` 为 `github` 拉取的 Linux 移植版 `aurix_flasher`, 已编译为 `linux/aurix_flasher` (169K), 依赖 `TAS` 的 `flash_TC3xx.hex`

### 3.4 串口权限

```bash
sudo usermod -a -G dialout $USER  # 重新登录生效
ls -l /dev/ttyACM0 /dev/ttyUSB0  # crw-rw-rw- dialout, crw-rw---- plugdev
ls /dev/serial/by-id/*  # usb-1a86_USB_Single_Serial_5AA9009329 -> ttyACM0 (CH340), usb-IFX_DAS_JDS_TriBoard... -> ttyUSB0 (FTDI)
stty -F /dev/ttyACM0 921600 raw -echo
```

---

## 4 构建与下载

### 4.1 一键脚本 `build.sh` (替代 Windows `build.ps1`)

```bash
cd tc387_1
./build.sh --help
./build.sh configure                 # CMake -DCMAKE_BUILD_TYPE=Debug
./build.sh build                     # Debug, 产 tc387_1.elf/hex/map, 约 text 69k
./build.sh build --build-type Release
./build.sh rebuild
./build.sh clean
./build.sh download                  # 需 TAS 运行, 默认 -id 0, -hex build/gcc/tc387_1.hex
./build.sh download --id 0 --build-type Release
./build.sh all                       # rebuild + download
./build.sh reset
```

产物:

```
build/gcc/tc387_1.elf  # 2.3M, text 69858 data 3619 bss 49548
build/gcc/tc387_1.hex  # 203K Intel HEX
build/gcc/tc387_1.map  # 2.2M
```

### 4.2 手动 CMake

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cmake -S . -B build/gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/tricore-gcc-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/gcc -j$(nproc)
tricore-elf-size --format=berkeley build/gcc/tc387_1.elf
```

### 4.3 下载

```bash
# 确保 TAS 运行
systemctl status tas-server
./../ref/aurix_flasher_linux-master/linux/aurix_flasher -id list
# 下载
./build.sh download                  # 内部: aurix_flasher -hex build/gcc/tc387_1.hex -id 0
# 或直接
../ref/aurix_flasher_linux-master/linux/aurix_flasher -hex build/gcc/tc387_1.hex -id 0 -start on
# 成功: Programming Flash memory ....... (Pass) Overall 1.2s
# 若提示 Failed to connect, 检查 tas_server 是否运行, ldd 是否缺 libftd2xx
```

> `AURIXFlasher` 默认 `-start on` 会在烧录后 `write32 0xF000047C 0xC0000000` 做 `Application Reset`; 若需 `DAP` 的 `RESET` (run) 可单独用 `tas_reset` (见 `ref/aurix_flasher_linux-master` 的 `-read` 扩展)

---

## 5 串口与 Shell

### 5.1 监控

```bash
ls /dev/serial/by-id/*
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw  # 需 pip install pyserial
# 或
python3 tc387_1/serial_monitor.py  # 简易脚本
```

### 5.2 启动日志 (921600, `4Mbps` 的回显工程已改为 `921600` 的 Shell 版)

```
letter:/$  (Shell prompt)
TC387 UART Echo + Letter-Shell
Board: TC387 TriBoard TC2XX V2.0 (ASCLIN4 P00.9 TX / P00.12 RX, 921600)
Type 'help' for commands, 'mcu' for chip info, 'temp' for temperature
ChipID: 0x8C228784 CHREV=0x04
SCU_ID: 0x00C4C0C1 RSTSTAT: 0x10010000
STM Freq: 100000000 Hz Tick: 0
DTS raw=0x0961 -> 46.77 C
```

> `P00.5` 高电平指示 `core0_main` 已进入, `P00.8` 在 `Shell_Process` 中置高

### 5.3 Shell 命令 ( `help` )

| 命令 | 说明 |
| --- | --- |
| `help` | 列出所有命令 |
| `version` / `ver` | 固件版本、编译时间、板卡 |
| `mcu` | ChipID、SCU_ID、RSTSTAT/RSTCON、CCUCON、STM 频率 |
| `uid` | CHIPID + DTSSTAT |
| `uptime` | `g_TickCount_1ms` 的天时分秒 |
| `reset` / `reboot` | `IfxScuRcu_performReset(system)` |
| `temp` | `IfxDts_getTemperatureValue` → `IfxDts_Dts_convertToCelsius` |
| `sysinfo` | `mcu` + `temp` + `uptime` |
| `mem` | 提示 (NO_SYS) |
| `clear` / `keys` 等 | Letter-Shell 内建 |

```
tc387> help
tc387> mcu
ChipID  : 0x8C228784 CHREV=0x04 CHREV...
tc387> temp
DTS raw=0x0974 -> 49.3 C
tc387> version
TC387 Letter-Shell Firmware v1.0.0
```

> 本工程已移除 `LAN8651` 相关命令 (`laninfo/landump/phyread` 等), 若需网络可参考 `ref/stm32h723_tja1103` 的 `lwip` 移植; `PHY` 诊断可直接通过 `TAS` 内存读写 `0xF003A000` 等寄存器

### 5.4 回显与 921600 优化 (解决乱码/卡顿)

**现象**: 原 921600 下 `RX FIFO Level=8` 导致短命令 (<8B 如 `temp\r`) 留在 HW FIFO 不触发中断, 直到下一帧才执行, 表现为 `tem��p` 乱码、`Command not Found`、回显延迟. 另 `printf` 在 shell 命令上下文中经 `_write` 阻塞导致 `mcu/temp` 卡死.

**优化**:

* **波特率**: `UART_Logging.c` `SERIAL_BAUDRATE=921600`, `oversampling=16`, `medianFilter=three`, `samplePoint=12`, `prescaler=1`, `PadDriver=cmosAutomotiveSpeed4` (原 Speed1 在 921600 下边沿不足)
* **FIFO**: `ASC_TX/RX_BUFFER_SIZE=1024` (原256), `RX FIFO Level=1` (原8, 确保每字节即中断), `TX Level=8`
* **中断优先级**: `RX 32 > TX 31 > STM 10` (原 RX28<TX29<STM99, STM 抢占 UART 导致 921600 溢出; 现 UART 抢占 STM)
* **ISR 批量**: `asclin4RxISR` 中 `isrReceive` 后 `getReadCount` 批量 `read` 64B 再 `Shell_RxPush`, `UART_Poll` 仅做 `RFL` 丢失回退 (关中断再 `isrReceive`) 并单包排空, 降低每字节 `disableInterrupts` 开销
* **Shell**: `gShellBuffer 1024` (原512), `SHELL_RX_RING 1024` (原512), `SHELL_COMMAND_MAX_LENGTH 256`/`PRINT_BUFFER 256` (原128), `Shell_RxPush` 溢出计数 `gShellRxOverflow`, `userShellRead` 快照 head/tail, 命令改用 `shellPrint` (经 `shell->write` 即 `userShellWrite` 的 `IfxAsclin_Asc_write` 分片) 替代 `printf` 的 `_write` 阻塞路径
* **回显**: `Shell` 行编辑自带回显, `UART_Poll` 仅作中断丢失兜底, 不再与 ISR 重复 `isrReceive`

验证: `python3 serial_monitor.py --baud 921600` 或 `miniterm /dev/ttyACM0 921600 --raw` 下 `help/mcu/temp` 均 <100ms 返回, `temp` 连发 10 次无 `�`, `char-by-char` 5ms 间隔无丢字

---

## 6 移植要点 (vs 回显工程)

* **工程名**: `project(tc387_1)` → `tc387_1.hex` (原 `tc387_lwip_iperf_gcc`)
* **移除**: `Libraries/Ethernet/lwip`, `Libraries/LAN8651`, `Configurations/lwipopts.h`, `Configuration.h` 的 `LAN8651_*` 与 `ETH_*` 定义
* **保留**: `Libraries/iLLD/TC3xx` 的 `Asclin/Stm/Scu/Dts/Port`, `Libraries/UART`, `Libraries/Infra/Service`
* **新增**: `Shell/letter-shell 3.2.4` + `shell_port.c` (`512B` 环形缓冲, `dsync`, `userShellRead` 轮询 `fallback`), `Lcf` 的 `.shellCommand/.shellVar` ( `KEEP` ), `CMake` 的 `SHELL_CFG_USER`
* **C/C++**: `printf` 重定向 (`UART_Logging.c:_write` → `IfxAsclin_Asc_write, TIME_INFINITE`), `setvbuf(stdout, _IONBF)` 禁缓冲
* **时钟**: `STM0` 的 `1ms` 中断 (`ISR_PRIORITY_OS_TICK=10 (原99,现降低让UART抢占)`, `ticks=100k*10` 首次 `10ms`, 后 `100k` 每 `1ms`, `IfxStm_increaseCompare`)
* **温度**: `IfxDts_Dts` (`LOW -40 UPPER 170, isrPriority 0`), `IfxDts_getTemperatureValue` → `convertToCelsius`

---

## 7 常见问题

* **Q: `Required TriCore GCC tool not found`**  
  A: `which tricore-elf-gcc` 或 `export PATH=/opt/tricore-gcc/bin:$PATH`, 或 `cmake -DAURIX_TOOLCHAIN_BIN=/opt/tricore-gcc/bin`

* **Q: `cannot find @C:\WINDOWS\TEMP\ccXXXXXX`**  
  A: Linux 下无; 若 `TMPDIR` 含空格 `export TMPDIR=/tmp`

* **Q: `aurix_flasher: Failed to connect to the server`**  
  A: `TAS` 未运行: `systemctl status tas-server` 或 `ss -tlnp|grep 24817`; 按 `3.3` 重装 `libftd2xx` 并 `sudo ldd /opt/Tools/DAS/8.3.0/bin/tas_server`

* **Q: `tas_server: Unable to locate executable /opt/tas/bin/tas_server`**  
  A: `service` 的 `ExecStart` 指向旧路径, 已修正为 `/opt/Tools/DAS/8.3.0/bin/tas_server` 并 `ln -sf`

* **Q: `P00_OUT` 仍 `0` 或 `Shell` 无 `help` 输出**  
  A: `TAS` 读 `0xF003A000` 应为 `0x20` (`P00.5` 高) 且 `P00_IOCR4=0x00008000`; 若 `STM` 中断未起 `g_TickCount` 不增, 检查 `ISR_PRIORITY_OS_TICK` 是否被 `UART` (`28/29`) 抢占, 或 `TAS` 的 `device_connect RESET_AND_HALT` 使 `CPU` 保持 `halt`, 需 `device_connect RESET` (run) 后 `IfxScuRcu` 的 `Application Reset`

* **Q: 串口无输出**  
  A: 确认 `921600` 且端口为 `/dev/ttyACM0` ( `CH340` ) 非 `ttyUSB0` ( `DAP` ), `dmesg | grep tty`, 按 `RESET` 键, `stty -F /dev/ttyACM0 921600 raw -echo` 后 `cat`

* **Q: `temp` 异常**  
  A: 前两次 `DTS` 需丢弃, 已置 `LOW -40 UPPER 170`, 多次 `temp` 观察收敛

* **Q: `help` 仅回 `he` 或 `Warning: Command is too long`**  
  A: `pyserial` 的 `ser.write(b'help\r')` 应为 `b'help\r'` 或 `b'help\n'`, 避免 `b'help\r\n'` 被拆为两条空命令; `UART_Poll` 的 `RFL` 轮询与 `asclin4RxISR` 的 `isrReceive` 可能重复, 已精简为单 `RFL` 检查

---

## 8 后续

* 增加 `plca` `mib` 等 (若接 `LAN8651`)
* 增加 `ping` (需 `lwip`)
* 将 `SHELL_HISTORY_MAX_NUMBER` 改大或 `DFlash` 持久化
* 增加 `xdump <addr> <len>` 内存查看
* 增加 `tas_reset` 的 `TAS` 内存读写命令到 `build.sh download --read 0xF003A000`

---

## 9 许可

* `iLLD`/`Libraries` 遵循 `Infineon Boost Software License 1.0`
* `Letter-Shell` MIT
* 其余移植代码内部许可
