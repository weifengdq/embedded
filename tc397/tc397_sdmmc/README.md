# tc397_uart_lettershell — TC397XX (292pin) ASCLIN0 Letter-Shell (921600) + P13.0 LED

本工程以 `tc397_0` (ADS, TC39XB) 为蓝本，在 **Ubuntu 26.04 + tricore-gcc 13.4.1 + CMake/Ninja**
下重建，移植 `tc387_1` 的 **Letter-Shell + 921600 高速串口** 方案（不含以太网），目标
**TC397XX 292pin**，调试串口 **ASCLIN0 TX P14.0 / RX P14.1, 921600-8N1**，
LED **P13.0（低电平点亮）** 通过 Shell 命令控制。

* 参考移植：`/home/z/lz/tc387/tc387_1`（Shell/UART/CMake/build.sh，见其 README §5.4 921600 优化）
* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
 （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 串口：`/dev/ttyACM0`（1a86:55d3），DAP MiniWiggler `058b:0043` 仅用于 TAS 下载

---

## 1 硬件

| 信号 | TC397 Pin | 说明 |
| --- | --- | --- |
| UART TX | P14.0 | `IfxAsclin0_TX_P14_0_OUT`, `cmosAutomotiveSpeed4` |
| UART RX | P14.1 | `IfxAsclin0_RXA_P14_1_IN`, `pullUp`, `Ifx_RxSel_a` |
| LED | P13.0 | 低电平点亮，上电短亮后 1 Hz 心跳（`Shell_Process`），`led` 命令控制 |
| DAP | USB 058b:0043 | TAS 下载 |
| MCU | TC397XX 292pin | `DEVICE_TC39XB` + `IFX_PIN_PACKAGE_LFBGA292`，6 核（Shell 跑 Core0） |

---

## 2 目录结构

```
tc397_uart_lettershell/
├── cmake/tricore-gcc-toolchain.cmake  # /opt/tricore-gcc/bin, 13.4.1
├── cmake/AurixProject.cmake           # 递归收集 + 排除 build/.ads/.settings
├── Configurations/
│   ├── Configuration.h / ConfigurationIsr.h  # STM 100k ticks/ms, OS_TICK 10, ASCLIN0 TX31/RX32
│   └── Ifx_Cfg.h (LFBGA292) / Ifx_Cfg_Ssw.*  # 保留 tc397_0
├── Libraries/
│   ├── iLLD/TC3xx/...                 # 保留 tc397_0 原版（TC39xB），勿用 tc387 的覆盖
│   ├── UART/UART_Logging.c/h          # ASCLIN0 921600, FIFO 1024, RX Level 1, TX/RX ISR + UART_Poll
│   └── Infra/Service/...              # Bsp, Ssw, Platform（保留 tc397_0）
├── Shell/
│   ├── letter-shell/src/              # 3.2.4
│   ├── shell_cfg_user.h               # 1024 Shell缓冲, 8历史, tick=g_TickCount_1ms
│   └── shell_port.c/h                 # 环形缓冲 1024B + led/mcu/temp 等命令
├── Lcf_Gnuc_Tricore_Tc.lsl            # 已增 .shellCommand/.shellVar (KEEP, PROVIDE)
├── Cpu0_Main.c                        # STM 1ms + P13.0 + UART/Shell/DTS
├── Cpu1..5_Main.c                     # 仅同步，空转（保留 tc397_0）
├── build.sh / serial_monitor.py       # 一键构建/烧录/监控
└── build/gcc/tc397_uart_lettershell.{elf,hex,map}
```

---

## 3 构建与下载（Ubuntu 26.04）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
tricore-elf-gcc --version  # 13.4.1

cd tc397_uart_lettershell
./build.sh build                        # Debug
./build.sh build --build-type Release
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh download --build-type Release --id 0
./build.sh clean
./build.sh all                          # rebuild + download
./build.sh reset                        # 触发 RESET + Application Reset
```

产物 `build/gcc/tc397_uart_lettershell.{elf,hex,map}`（`build/` 已全局忽略，不进 git）。

手动 CMake：

```bash
cmake -S . -B build/gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/tricore-gcc-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/gcc -j$(nproc)
tricore-elf-size --format=berkeley build/gcc/tc397_uart_lettershell.elf
```

下载说明：`build.sh` 默认 flasher 为
`/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`
（`resolve_flasher` 另试 `tools/aurix_flasher/`），可用 `--flash-tool <path>` 覆盖。
TAS 需运行：`systemctl status tas-server`（`ss -tlnp | grep 24817`），
验证 `./aurix_flasher -id list`。

---

## 4 串口与 Shell

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw
python3 serial_monitor.py --port /dev/ttyACM0 --baud 921600
python3 serial_monitor.py --port /dev/ttyACM0 --baud 921600 --cmd help --duration 5
```

启动日志（921600）：

```
After Shell_Init direct
TC397 Letter-Shell ...
TC397 UART0 + Letter-Shell
Board: TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600)
Type 'help' for commands, ...
ChipID: 0x... CHREV=0x...
SCU_ID: 0x... RSTSTAT: 0x...
STM Freq: 100000000 Hz Tick: 0
DTS raw=0x... -> .. C
```

| 命令 | 说明 |
| --- | --- |
| `help` | 列出全部命令 |
| `version` / `ver` | 固件版本、编译时间、板卡 |
| `mcu` | ChipID/SCU_ID/RSTSTAT/RSTCON/CCUCON/STM |
| `uid` | CHIPID + DTSSTAT |
| `uptime` | g_TickCount 天时分秒 |
| `reset` / `reboot` | 软件复位 |
| `temp` | DTS 温度 |
| `sysinfo` | mcu+temp+uptime |
| `led` | P13.0 控制（见下） |
| `mem` | 提示（NO_SYS） |

LED（P13.0，低=亮）：

```
letter:/$ led
letter:/$ led on        # 点亮，心跳关
letter:/$ led off       # 熄灭，心跳关
letter:/$ led toggle
letter:/$ led blink 5 200   # 闪 5 次 x 200ms
letter:/$ led hb on     # 1Hz 心跳开（默认开）
letter:/$ led hb off    # 心跳关
```

---

## 5 移植要点（vs tc397_0 / tc387_1）

* **保留 tc397_0**：`Libraries/iLLD`（TC39xB 全套 SFR/PinMap）、`Infra/Service`、
  `Configurations/Ifx_Cfg_Ssw.*`、`Lcf` 内存布局（6 核 stacks/CSA）、`Cpu1..5_Main.c`。
  切勿用 tc387 的 iLLD 覆盖（版本不同）。
* **修改 `Ifx_Cfg.h`**：`IFX_PIN_PACKAGE_LFBGA292`（原 516），`DEVICE_TC39XB` 不变。
* **新增**：`cmake/`（3 文件，直拷 tc387_1）、`CMakeLists.txt`
  （`project(tc397_uart_lettershell)`，GCC `-mcpu=tc39xx` / TASKING `tc39xb`）、
  `build.sh`（同上 + flasher 多路径）、`serial_monitor.py`、
  `Configurations/Configuration.h` + `ConfigurationIsr.h`
  （`OS_TICK 10`，`ASCLIN0_TX 31 / RX 32`）、`Libraries/UART/`（ASCLIN0 版）、
  `Shell/`（letter-shell + `shell_cfg_user.h` + `shell_port.c` TC397 版 + `led`）、
  `Lcf` 追加 `.shellCommand/.shellVar`（KEEP/PROVIDE，同 tc387_1）。
* **UART0**：`MODULE_ASCLIN0`，`IfxAsclin0_TX_P14_0_OUT` / `IfxAsclin0_RXA_P14_1_IN`，
  `921600/oversampling 16/medianFilter three/samplePoint 12/prescaler 1`，
  `PadDriver cmosAutomotiveSpeed4`，`TX/RX 1024`，`RX Level 1 / TX 8`，
  `RX 32 > TX 31 > STM 10`，ISR 批量 64B `Shell_RxPush`，
  `UART_Poll` 仅作 `RFL` 丢失回退（见 tc387_1 README §5.4）。
* **时钟/温度**：STM0 1ms（`100k ticks/ms`，`increaseCompare`），
  DTS `LOW -40 UPPER 170`，`convertToCelsius`。
* **CPU**：`CMake -mcpu=tc39xx`（`--target-help` 实测支持 `tc39xx`），
  `.cproject` 仍为 `tc39xb`（TASKING 名，供 ADS 参考）。

---

## 6 常见问题

* **串口无输出**：确认 `921600` 且为 `/dev/ttyACM0` 非 `ttyUSB0`；
  `stty -F /dev/ttyACM0 921600 raw -echo` 后重读；按 RESET；
  `aurix_flasher -id 0 -read 0x80000000` 触发 RESET + Application Reset。
* **烧录后仍 halt**：`build.sh download` 末尾已自动 `-read 0x80000000` 一次；
  无效则 `./build.sh reset` 或按板载 RESET。
* **`tricore-elf-gcc not found`**：`export PATH=/opt/tricore-gcc/bin:$PATH`。
* **TAS 连不上**：`systemctl status tas-server`，`ss -tlnp | grep 24817`，
  `ldd /opt/Tools/DAS/8.3.0/bin/tas_server` 查 `libftd2xx`。
* **LED 不亮**：P13.0 低=亮；`led on` 后用万用表量 P13.0 应 ~0V；
  `led hb off` 排除心跳干扰后再测。

---

## 7 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
