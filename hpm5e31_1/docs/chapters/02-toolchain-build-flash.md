# 第 2 章 开发环境、构建、烧写与调试

## 2.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/hello_world/`、`samples/shell/letter_shell_basic/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（命令与日志均为本机实测） |

## 2.2 本章导览

- 一套**可复现**的 Linux 开发环境（SDK/工具链/CMake/Ninja/J-Link 版本精确到小版本）；
- 本仓库的板级支持包结构：`boards/hpm5e31_1/` 里每个文件是干什么的；
- 三条命令完成"编译 → 烧写 → 抓串口"（`build.sh` / `flash.sh` / `hwrun.sh`）；
- 烧写原理（J-Link 命令行 + 片内 Flash + 实测速度）与失败处理、ISP 备用方案；
- 用 GDB 调试 RISC-V、死机（trap）定位方法；
- 从零搭环境（含代理）与遇到过的问题。

## 2.3 环境总览

表 2-1 软件与硬件版本（2026-10-30 实测）

| 项 | 版本 | 位置/命令 |
|---|---|---|
| 主机系统 | Ubuntu 26.04 LTS（内核 7.0.0-38），24 核 / 61 GB | 远程 `<user>@<host>` |
| HPM SDK | v1.13.0（VERSION_MAJOR=1, MINOR=13, PATCH=0） | `$HOME/sdk_env_v1.13.0/hpm_sdk` |
| RISC-V GCC | `riscv32-unknown-elf-gcc (g5964b5cd727) 11.1.0` | `$HOME/toolchains/riscv32-unknown-elf-newlib-multilib` |
| CMake | 4.2.3 | `/usr/bin/cmake` |
| Ninja | 1.13.2 | `/usr/bin/ninja` |
| J-Link | Commander/DLL V9.72（J-Link OB-RA4M2-Full，固件 2026-06-25） | `/usr/bin/JLinkExe` |
| 串口工具 | pyserial 3.5（`scripts/serial_cap.py`） | `/dev/ttyACM0` 921600 8N1 |
| 命令行 | letter-shell 3.2.4 | 第 03 章 |

> **GCC 版本差异**：SDK v1.13.0 官方推荐更新版工具链（GCC 13/14），本次使用 GCC 11.1 也能编译。
> CMake 的 `check_march_ext_supported` 会探测工具链支持的扩展，结果是
> `-march=rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs`（包含 B 扩展的 Zba/Zbb/Zbc/Zbs），
> 说明这颗 RV32-IMAFDCBP 的 B 扩展在本工具链下可用。性能数据（第 23 章）与 GCC 版本强相关。

## 2.4 仓库与板级支持包

### 2.4.1 目录结构

```text
hpm5e31_1/
├── boards/hpm5e31_1/          # 板级支持包（BSP），随每个例程一起编译
│   ├── board.c / board.h      # 控制台、时钟摘要、LED、复位、ENET 板级钩子、Flash 配置字
│   ├── clock.c / clock.h      # PLL 与各外设时钟组（PLL0=960MHz → CPU=480MHz）
│   ├── pinmux.c / pinmux.h    # 核心引脚复用（控制台/LED/CAN/SPI2/RGMII/PHY 复位）
│   ├── pins/pinmux_*.c        # 各领域追加的引脚（file(GLOB) 自动纳入编译）
│   ├── ext/board_*.h          # 各领域板级宏（每个 owner 一个文件，避免冲突）
│   ├── bsp/                   # letter-shell 移植 + 通用命令 + BSP API
│   └── CMakeLists.txt
├── samples/                   # 例程（按外设分类）
├── scripts/                   # env/build/flash/serial/hwrun/new_sample
├── docs/                      # 本书（chapters/figures/generated）
├── tools/                     # 文档校验/PDF/绘图/寄存器表生成
└── logs/                      # 实测串口日志
```

表 2-2 板级文件职责

| 文件 | 职责 | 改它的场景 |
|---|---|---|
| `board.c` | `board_init()` 依次初始化时钟、控制台、PMP；`board_delay_ms/us`；ENET/PHY 钩子；`.nor_cfg_option` Flash 配置字 | 换 Flash、改控制台、改 LED/按键宏 |
| `clock.c` | PLL0 960 MHz、CPU 480 MHz、MCHTMR 24 MHz、各外设时钟开关 | 改主频、降功耗 |
| `pinmux.c` | 核心引脚初始化函数（`init_uart0_pins`、`init_mcan0_pins`、`init_spi2_pins`、`init_enet0_pins_rgmii` …） | 改硬件接线 |
| `bsp/bsp.c` | `bsp_init()`：board_init + letter-shell 初始化；`bsp_shell_poll()` | 加通用命令 |
| `bsp/shell_port.c` | UART0 中断 + 512 B 环形缓冲 + letter-shell 读写钩子 | 换控制台/改缓冲 |
| `bsp/bsp_cmds.c` | 通用 shell 命令：`ver/uptime/clk/pin/reg/mem/reboot` | 加调试命令 |
| `ext/board_*.h` | 各领域板级资源宏（CAN 灯、LAN8651、YT8011…） | 各领域 owner |

表 2-3 BSP 常用 API（`boards/hpm5e31_1/bsp/bsp.h`）

| API | 作用 |
|---|---|
| `bsp_init()` | 板级初始化 + 控制台 + letter-shell（每个例程第一句） |
| `bsp_shell_poll()` | 主循环里调用，处理命令行输入 |
| `bsp_uptime_ms()` | 基于 mcycle 的毫秒级 uptime（32 位回绕约 49 天） |
| `bsp_parse_u32(str, &v)` | 解析十进制/0x 十六进制（shell 命令常用） |
| `bsp_hexdump(addr, len)` | 十六进制 dump（16 字节一行） |
| `bsp_gpio_output_init/input_init(port, pin)` | 快速把引脚设为 GPIO 输入/输出 |
| `BSP_LOG(...)` | 等价 `printf`，统一日志出口 |

## 2.5 构建

### 2.5.1 一条命令

```bash
cd hpm5e31_1
bash scripts/build.sh can/mcan_loopback          # 编译 samples/can/mcan_loopback
bash scripts/build.sh can/mcan_loopback -c release
```

`scripts/build.sh` 做三件事：`source scripts/env.sh` → 首次 `cmake -GNinja` 配置到
`build/<name>-<config>/` → `cmake --build`。产物：`build/<name>-debug/output/demo.elf|.bin|.map|.asm`。

![图 2-1 构建、烧写、运行与观测流程](figures/02/fig-02-1-flow.png)

*图 2-1 构建、烧写、运行与观测流程*

### 2.5.2 关键 CMake 变量

表 2-4 构建参数

| 变量 | 本仓库取值 | 含义 |
|---|---|---|
| `HPM_SDK_BASE` | `$HOME/sdk_env_v1.13.0/hpm_sdk` | SDK 根目录（环境变量） |
| `GNURISCV_TOOLCHAIN_PATH` | `.../riscv32-unknown-elf-newlib-multilib` | 工具链根目录 |
| `BOARD` | `hpm5e31_1` | 板级目录名 |
| `BOARD_SEARCH_PATH` | `<repo>/boards` | 板级搜索路径（非 SDK 内置板） |
| `HPM_BUILD_TYPE` | `flash_xip` | 代码在 Flash 上 XIP 执行 |
| `CMAKE_BUILD_TYPE` | `debug` / `release` | 影响 `-O0/-O2` 与调试信息 |

### 2.5.3 镜像体积（实测）

表 2-5 典型镜像体积（`riscv32-unknown-elf-size`）

| 例程 | text | data | bss | 说明 |
|---|---|---|---|---|
| hello_world | 45,864 B | 472 B | 33,312 B | 含 SDK 驱动库 + 控制台；bss 主要是 32 KB 栈/堆预留 |
| letter_shell_basic | 63,040 B | 560 B | 34,504 B | 增加 letter-shell（约 17 KB） |
| xpi_probe | 62,688 B | 560 B | 34,496 B | 同上 |

> 片内 Flash 1 MB，当前例程占用 6% 左右，余量充足；真正限制往往在 **DLM/ILM**（各 128 KB）。
![图 2-2 片内 Flash 的镜像布局（nor_cfg_option / boot_header / 应用镜像）](figures/02/fig-02-2-flash.png)

**图 2-2** ：片内 1 MB Flash 的镜像布局。J-Link 的 `loadfile` 会按 ELF 段自动擦写
`nor_cfg_option`（0x400）、`boot_header`（0x1000）与应用区（0x3000 起），因此**不要手工搬动应用起始地址**，
否则芯片起不来（详见第 21 章）。


## 2.6 烧写

### 2.6.1 J-Link 命令行（实际使用的方式）

```bash
cat > /tmp/flash.jlink <<EOF
JTAGConf -1,-1
r
h
loadfile build/hello_world-debug/output/demo.elf
r
g
qc
EOF
JLinkExe -NoGui 1 -AutoConnect 1 -Device HPM5E31xGNx -If JTAG -Speed 4000 \
         -ExitOnError 1 -CommandFile /tmp/flash.jlink
```

命令解释：`JTAGConf -1,-1` 自动探测 IR/ID；`r` 复位；`h` 暂停；`loadfile` 按 ELF 段
自动擦写片内 Flash（含 `nor_cfg_option` 与 boot header）；再 `r`、`g` 运行。

【实测】一次烧写（57~64 KB）总耗时约 1.3~1.7 s，其中编程速度 **78~83 KB/s**，
擦除 0.07 s，校验 0.05 s。同一镜像重复烧写会显示 `Skipped. Contents already match`（省时间）。

### 2.6.2 封装脚本与并发

`scripts/flash.sh` 在烧写前对 `/tmp/hpm5e31_hw.lock` 加 `flock`，保证多 agent/多人共享一块板时
不会同时占用 J-Link；`hwrun.sh` 持有整机锁（编译在锁外，烧写+串口在锁内）。

### 2.6.3 常见失败与处理

表 2-6 烧写问题排查

| 现象 | 原因 | 处理 |
|---|---|---|
| `Cannot connect to target` / `TotalIRLen = 0` | 板子没上电、JTAG 线松、J-Link 被占用 | 查电源/线；`ps -ef | grep JLink` 杀掉残留进程 |
| `Device "HPM5E31XGNX" selected` 后卡住 | 芯片进入 ISP 模式（复位时按住 BTN） | 松开按键重新上电 |
| 烧写成功但串口无输出 | 波特率不对（115200 vs 921600）、串口被别的进程占用 | 用 `serial_cap.py --baud 921600`；`fuser /dev/ttyACM0` |
| `Flash download failed - Could not open file` | ELF 路径错 | 用绝对路径；`build.sh` 会打印 ELF 路径 |
| 程序跑飞/反复重启 | 时钟/引脚配置错、访问了未映射地址 | 用 J-Link 连 `h` 暂停看 PC；见 4.4 与第 04 章 trap |
| J-Link 固件升级后连不上 | 固件升级需重新插拔 | 拔插 USB 后重试 |

### 2.6.4 ISP 备用方案（程序跑飞时的恢复）

HPM5E31 支持串行 ISP：把 **PA03（BOOT_MODE1）拉高**后复位，芯片进入 ISP（从 UART0/USB0 下载）。
本板 PA03 就是 USR BTN：**按住按键再复位**即可进入。随后可用 HPM 的 ISP 工具
（`hpm_sdk` 配套的 `hpm_isp`/`hpm_manufacturing_tool`，或 ROM API 示例）串口下载；
也可以直接用 J-Link 重新烧写（J-Link 复位后能 halt 住芯片，通常无需 ISP）。

## 2.7 串口与命令行

### 2.7.1 抓串口

```bash
# 只抓 5 秒
python3 scripts/serial_cap.py --secs 5
# 发命令再抓（; 分隔多条，自动补 CRLF）
python3 scripts/serial_cap.py --secs 6 --send "ver;clk;pin A2"
# 等到匹配到关键字就返回（退出码 0；不匹配退出码 3）
python3 scripts/serial_cap.py --secs 10 --send "bench" --expect "CoreMark"
```

### 2.7.2 一条龙

```bash
bash scripts/hwrun.sh shell/letter_shell_basic --secs 6 --send "help;ver"
# 编译 + 烧写 + 抓串口 + 日志落盘 logs/YYYYMMDD-HHMMSS-letter_shell_basic.log
```

【实测】hello_world 输出（`logs/20261030-104021-hello_world.log`）：

```text
===========

[hpm5e31_1] hello world (hpm5e31_1 board)
cpu0 : 480000000 Hz
ahb  : 200000000 Hz
xpi0 : 100000000 Hz
tick 1, uptime 1000 ms
tick 2, uptime 2000 ms
```

letter-shell 输出（`logs/20261030-104826-letter_shell_basic.log`，节选）：

```text
 _         _   _                  _          _ _
| |    ___| |_| |_ ___ _ __   ___| |__   ___| | |
...
letter:/$ ver
board       : hpm5e31_1
soc         : HPM5E31 (RV32-IMAFDCBP @ 480000000 Hz)
...
letter:/$ pin A2
A2 = 0
```

### 2.7.3 通用命令

表 2-7 letter-shell 通用命令

| 命令 | 作用 | 示例 |
|---|---|---|
| `help` / `help <cmd>` | 列出/查看命令 | `help pin` |
| `ver` | 板卡/芯片/SDK/构建时间 | `ver` |
| `uptime` | 运行时间 | `uptime` |
| `clk` | 主时钟树频率 | `clk` |
| `pin <A..F><n> [0|1]` | 读/写 GPIO | `pin A2 1` |
| `reg <addr> [val]` | 读/写 32 位寄存器 | `reg 0xF0040000` |
| `mem <addr> [len]` | 内存 hexdump | `mem 0x80000000 64` |
| `reboot` | 软件复位（PPOR） | `reboot` |
| `cmds` / `clear` / `sh <cmd>` | 命令列表/清屏/直接执行 | `sh ver` |

各章还会注册自己的命令（如 CAN 章 `can send ...`、性能章 `bench ...`），见对应章节。

## 2.8 调试

### 2.8.1 GDB + J-Link GDB Server

```bash
# 终端 1：启动 GDB Server（设备名与烧写一致）
JLinkGDBServer -if JTAG -device HPM5E31xGNx -speed 4000 -port 2331 -singlerun -nogui
# 终端 2：用工具链里的 gdb 连接
riscv32-unknown-elf-gdb build/hello_world-debug/output/demo.elf
(gdb) target remote :2331
(gdb) load
(gdb) monitor reset
(gdb) break main
(gdb) continue
```

### 2.8.2 死机定位

SDK 默认 trap 是 weak 空实现且**不保存 mtval**（见 `soc/HPM5E00/HPM5E31/toolchains/trap.c`），
所以"死机没信息"是常态。建议：

1. 在例程里重写 trap handler，打印 `mcause/mepc/mtval`（本书 `bsp` 后续会提供 `trap_dump` 命令，
   当前可用 J-Link 暂停后看 PC）；
2. J-Link 连接后 `h` 暂停 → 用 `regs` 看 PC/mcause，对照 `.map`/`.asm` 定位；
3. 用 `samples/.../output/demo.asm` 反汇编对照源码行；
4. 常见原因：访问未开时钟的外设（全 0/全 F）、DMAMUX 通道号错、栈溢出（DLM 越界）、
   中断里调用 printf 导致重入。

### 2.8.3 观察点（无示波器时）

- `pin` 命令 + 8 颗 LED：把状态机状态"打"到 LED（第 4/9 章例程就这么做）；
- `mcycle` 计时：`hpm_csr_get_core_mcycle()`，精度 1/480 MHz ≈ 2.08 ns；
- `MCHTMR`：24 MHz，适合长时间测量；
- GPTMR 输入捕获：测外部/内部信号频率与占空比（第 15 章）。

## 2.9 从零复现环境

```bash
# 1) 解压 SDK（官方 sdk_env_v1.13.0.zip，只取 hpm_sdk）
unzip -q sdk_env_v1.13.0.zip 'hpm_sdk/*' -d $HOME/sdk_env_v1.13.0
# 2) 解压 Linux 工具链（官方 riscv32-unknown-elf-newlib-multilib）
unzip -q riscv32-unknown-elf-newlib-multilib.zip -d $HOME/toolchains
# 3) 安装依赖（需要能联网或代理）
export http_proxy=http://<proxy>:7890 https_proxy=http://<proxy>:7890
sudo -E apt-get update
sudo -E apt-get install -y cmake ninja-build python3-pip python3-serial \
    graphviz pandoc texlive-xetex texlive-lang-chinese fonts-noto-cjk
# 4) 克隆仓库并编译（公开仓库）
git clone https://github.com/weifengdq/embedded.git
cd embedded/hpm5e31_1
source scripts/env.sh
bash scripts/build.sh shell/letter_shell_basic
bash scripts/hwrun.sh shell/letter_shell_basic --secs 5 --send "ver;clk"
```

## 2.10 易错点清单（本仓库开发过程中真实遇到过）

表 2-8 环境/构建/烧写易错点

| # | 现象 | 原因 | 解决 |
|---|---|---|---|
| 1 | `serial.py` 报 `module 'serial' has no attribute 'Serial'` | 脚本名与 pyserial 包同名，被当成本地模块导入 | 改名 `serial_cap.py` |
| 2 | `hwrun.sh` 卡死不动 | 父进程持锁后又调 `flash.sh` 再次锁同一文件 → flock 不可重入 | 父进程加锁后导出 `HPM_HW_LOCK_HELD=1`，子脚本跳过加锁 |
| 3 | 首次 `JLinkExe` 卡在 `JTAGConf` 交互 | 未给 `JTAGConf -1,-1` | 命令文件第一行加自动探测 |
| 4 | 串口输出丢开头几个字符 | 921600 下 USB-UART/终端打开时机 | 先复位再抓；或在主循环里周期打印；已记为待确认 |
| 5 | `cmake` 报 `find_package(hpm-sdk)` 失败 | 未设置 `HPM_SDK_BASE` 或 `BOARD_SEARCH_PATH` | 用 `scripts/build.sh`（内部已 source env） |
| 6 | 板级改动不生效 | CMake 缓存了旧板级路径 | 删 `build/<name>-debug` 重配 |
| 7 | 官方例程直接拷到本板编译不过 | EVK 与 hpm5e31_1 引脚/宏不同 | 对比第 01 章表 1-10 改 `board.h`/`pinmux` |
| 8 | zsh 下 `JLinkExe -?` 报错 | zsh 把 `?` 当通配符 | 写成 `JLinkExe '-?'` 或用 bash -lc |

## 2.11 参考来源

1. `hpm_sdk/README_zh.md`、`hpm_sdk/cmake/toolchain/gcc/gcc.cmake`（CMake 变量与工具链探测）
2. `hpm_sdk/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（`nor_cfg_option`/boot header 偏移）
3. `hpm_sdk/boards/hpm5e31_1/CMakeLists.txt`、`boards/hpm5e00evk/board.c`（板级钩子）
4. J-Link 命令参考：<https://kb.segger.com/J-Link_Commander>
5. HPM SDK 快速入门 `sdk_env_v1.13.0/doc/HPM_SW_QSG.pdf`
6. 本仓库 `scripts/`、`logs/20261030-104021-hello_world.log`、`logs/20261030-104826-letter_shell_basic.log`

## 2.12 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 项 | 状态 | 说明/验证方法 |
|---|---|---|
| 921600 下偶发首字节丢失 | 已知限制 | 用第 05 章 UART 压力例程统计误码；必要时降 460800 |
| GCC 11.1 vs 官方推荐 GCC 13 的性能差异 | 已知限制 | 第 23 章可换工具链重测 CoreMark 对比 |
| GDB 单步/断点在 XIP Flash 上的稳定性 | 已知限制 | J-Link 硬件断点数量有限，复杂调试建议放 ILM |
| ISP 模式未实测 | 已知限制 | 按住 BTN 复位后用 HPM ISP 工具验证（不影响正常使用） |
