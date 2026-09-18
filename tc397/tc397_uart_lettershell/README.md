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

## 7 Windows 11 + TASKING 构建与实测（2026-09-18）

### 7.1 测试背景

* 目标：本工程为 8 个 Tasking 移植的基线（letter-shell + LSL 命令表 +
  dsync 屏障均在此验证），Ubuntu GCC 功能已验证，现验证同一套源码在
  Windows 11 + TASKING TriCore v6.3r1（`C:\z\app\TASKING\TriCore_v6.3r1`）
  下的命令行构建与功能。
* 约束：增量改动不得影响 Ubuntu GCC（`build.sh` 原样保留；源码改动包在
  `__TASKING__` 分支或工具链无关形式；Windows GCC 对照编译通过）。
* 环境：AURIX-Studio-1.10.36（GCC 11.3.1 / AURIXFlasher v3.0.18 /
  WinGet 官方 ninja 1.13.2），COM165（CH343，921600-8N1），DAP MiniWiggler。

### 7.2 构建命令

```powershell
.\build.ps1 -Compiler tasking                      # Tasking Debug 编译
.\build.ps1 -Compiler tasking -Action download     # 编译并烧录
.\build.ps1 -Compiler gcc                          # Windows GCC 对照编译（ADS tricore-gcc11）
```

### 7.3 通用兼容改动（GCC 行为不变，详见 `tc397/temp/tasking_porting_log.md`）

* `CMakeLists.txt`：`--language=+volatile` → `--language=+volatile,+gcc`
  （letter-shell `, ##__VA_ARGS__` 空变参需 GNU 扩展，否则 cctc E250；
  实测 `+gcc` 不定义 `__GNUC__`，不影响 iLLD 头文件路径选择）。
* `Shell/letter-shell/src/shell.h`：新增 `__TASKING__` 分支，
  `SHELL_SECTION` 只用 `section`（去 `aligned(1)` 避 W770），
  `SHELL_USED` 用 `__attribute__((used))`。
* `Shell/letter-shell/src/shell.c`：新增 `__TASKING__` 分支，
  用 LSL 命名组标签 `_lc_gb/_lc_ge_shellCommand` 作命令表起止
  （与 GCC 同名段 `shellCommand`）；取表条件加 `defined(__TASKING__)`。
* `Shell/shell_port.c`：`__asm__ volatile("dsync")` → `SHELL_DSYNC()` 宏，
  Tasking 用 iLLD `__dsync()`（`IfxCpu_Intrinsics.h`），GCC 保持原样。
* `Lcf_Tasking_Tricore_Tc.lsl`：Far Const 组内加命名组 `shellCommand`
  （精确名 select 防未引用删除，自动生成起止标签；注释用 `//`，LSL 不认 `/* */`）。
* `Libraries/.../Ifx_Ssw_CompilersTasking.h`：`IFX_SSW_INLINE` 的 C 分支
  `inline` → `static inline`（裸 inline 每 TU 生成全局符号，链接报 ltc E108；
  与 GCC 版 `static inline always_inline` 对齐）。

### 7.4 本工程实测日志（Tasking Debug）

编译（300 obj，0 error；仅 W560/W549/W577 类警告，无 error）：

```
[297/300] Building C object CMakeFiles\tc397_uart_lettershell.dir\Shell\letter-shell\src\shell.c.obj
[298/300] Building C object CMakeFiles\tc397_uart_lettershell.dir\Shell\shell_port.c.obj
[299/300] Linking C executable tc397_uart_lettershell.elf
```

map 确认命令表落盘（`_lc_gb_shellCommand = 0x80006898`，
`_lc_ge_shellCommand = 0x80006a68`，差值 0x2D0 = 720B = 18 条 × 40B/条，
含 shell.c 内建 7 条 + shell_port 11 条）。

`help`（命令表正确性的直接证据）：

```
Command List:
version               CMD   version info
ver                   CMD   version alias
mcu                   CMD   MCU info
uid                   CMD   chip UID
uptime                CMD   uptime
reset                 CMD   software reset
reboot                CMD   reboot alias
temp                  CMD   die temperature
sysinfo               CMD   system info
mem                   CMD   memory info
led                   CMD   P13.0 LED control
help                  CMD   show command info
setVar                CMD   set var
users                 CMD   list all user
cmds                  CMD   list all cmd
vars                  CMD   list all var
keys                  CMD   list all key
clear                 CMD   clear console
```

`version` / `mcu`：

```
TC397 Letter-Shell Firmware / Version : 1.0.0 / Build : Sep 18 2026 11:38:07
ChipID  : 0xAF239793 (CHREV=0x13) / STM Freq: 100000000 Hz
```

Windows GCC 对照：`.\build.ps1 -Compiler gcc` 编译通过（301 obj），
证明上述改动不影响 GCC 路径（Ubuntu GCC 13.4.1 同理）。

### 7.5 测试结果

* 编译/烧录/shell 全 PASS；命令表链接正确性由 `help` 输出直接证明。
* 构建机注记：Python 自带 ninja 1.13.0（kitware 版）增量构建失败，
  WinGet 官方 1.13.2 正常；`rebuild` 可规避。

---

## 8 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
