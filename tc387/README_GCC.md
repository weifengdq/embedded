# TC387 CMake 命令行构建指南（GCC + TASKING 双工具链）

本文档说明如何在不依赖 AURIX Development Studio（ADS）IDE 的情况下，通过 PowerShell + CMake 对 TC387 系列工程进行清理、编译、下载和复位操作。工程同时支持 TriCore GCC 和 TASKING 两种工具链，ADS IDE 功能不受影响。

---

## 一、工具链与软件依赖

### 1.1 必须安装

| 工具 | 版本（测试）| 默认安装路径 | 说明 |
|------|------------|------------|------|
| AURIX Development Studio | 1.10.28 | `C:\Infineon\AURIX-Studio-1.10.28` | 内含 tricore-gcc11、AURIXFlasher、make |
| CMake | ≥ 3.24（测试 3.27.4）| `C:\z\app\CMake` | 需在 PATH 中 |
| Ninja | ≥ 1.10（测试 1.13.2）| 系统 PATH | GCC/TASKING 构建后端 |
| PowerShell | 5.1 或 7.x | 系统自带 | 构建脚本运行环境 |

> **CMake 最低版本要求 3.24**，低于此版本会报错。推荐通过 winget 安装：`winget install Kitware.CMake`

### 1.2 TriCore GCC（随 ADS 捆绑）

ADS 自带 tricore-gcc11，无需单独安装：

```
C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin\tricore-elf-gcc.exe
```

版本：**GCC 11.3.1**（AURIX(tm) GCC, Built 2025-09-08）

GCC 对 TC387 的 `-mcpu` 参数值为 **`tc38xx`**（注意末尾双 x），与 TASKING 的 `tc38x` 不同。

### 1.3 TASKING TriCore 编译器（需商业授权）

商业版 TASKING TriCore 编译器，**单独安装**，不含在 ADS 内：

```
C:\z\app\TASKING\TriCore_v6.3r1\ctc\bin\cctc.exe
```

版本：**TASKING VX-toolset for TriCore v6.3r1** Build 19041558

> **注意**：ADS 捆绑的 TASKING（非商业版）仅允许在 ADS IDE 内使用，命令行独立运行会报错 `F104: License does not support running as standalone`。命令行编译必须使用单独购买的商业版 TASKING。

### 1.4 烧录工具（随 ADS 捆绑）

```
C:\Infineon\AURIX-Studio-1.10.28\tools\AurixFlasherSoftwareTool_v3.0.14\AURIXFlasher.exe
```

版本：**AURIXFlasher 3.0.14**，连接方式 DAP Miniwiggler（DAS 协议）。

---

## 二、工程目录结构

每个适配工程的新增文件如下（ADS 原有文件均未修改）：

```
<project>/
├── CMakeLists.txt                   # CMake 构建定义（GCC/TASKING 双工具链）
├── build.ps1                        # PowerShell 构建/下载/复位脚本
└── cmake/
    ├── AurixProject.cmake           # 源码/头文件递归收集工具函数
    ├── tricore-gcc-toolchain.cmake  # TriCore GCC 工具链配置
    └── tasking-tricore-toolchain.cmake  # TASKING 工具链配置
```

**构建输出目录**：
- GCC：`<project>/build/gcc/`
- TASKING：`<project>/build/tasking/`

两个编译器的构建目录完全隔离，ADS 的 `TriCore Debug (TASKING)` / `TriCore Debug (GCC)` 目录不受影响。

---

## 三、`build.ps1` 使用方法

### 3.1 参数说明

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `-Action` | 字符串 | `build` | `configure` \| `build` \| `rebuild` \| `clean` \| `download` \| `reset` \| `all` |
| `-Compiler` | 字符串 | `gcc` | `gcc` \| `tasking` |
| `-BuildType` | 字符串 | `Debug` | `Debug` \| `Release` \| `RelWithDebInfo` \| `MinSizeRel` |
| `-AurixStudioPath` | 路径 | `C:\Infineon\AURIX-Studio-1.10.28` | ADS 根目录，自动推导 GCC bin、AURIXFlasher、make |
| `-TaskingPath` | 路径 | `C:\z\app\TASKING\TriCore_v6.3r1` | TASKING 根目录，自动推导 `ctc\bin` |
| `-GccBin` | 路径 | （自动推导）| 显式指定 GCC bin 目录，优先于 `AurixStudioPath` |
| `-TaskingBin` | 路径 | （自动推导）| 显式指定 TASKING ctc\bin 目录，优先于 `TaskingPath` |
| `-FlashTool` | 路径 | （自动查找）| 显式指定 AURIXFlasher.exe 路径 |
| `-DasPort` | int | `0` | DAP/DAS 端口索引，多个下载器时用 `-DasPort 1` |

### 3.2 常用命令

```powershell
# ---- GCC（默认）----
.\build.ps1                            # Debug 编译（增量）
.\build.ps1 -Action rebuild            # 清理后重新编译
.\build.ps1 -Action download           # 编译（如需）并烧录
.\build.ps1 -Action reset              # 仅复位板子，不烧录
.\build.ps1 -Action all                # 清理 + 编译 + 烧录
.\build.ps1 -BuildType Release         # Release 编译

# ---- TASKING ----
.\build.ps1 -Compiler tasking                        # TASKING Debug 编译
.\build.ps1 -Compiler tasking -Action rebuild        # TASKING 清理重编
.\build.ps1 -Compiler tasking -Action download       # TASKING 编译并烧录

# ---- 仅清理 ----
.\build.ps1 -Action clean              # 删除 build/gcc/
.\build.ps1 -Compiler tasking -Action clean   # 删除 build/tasking/

# 若出现 TriCore GCC 链接器临时响应文件错误（例如 `ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX`），可在构建前指定临时目录后重试：
New-Item -ItemType Directory -Path C:\Temp -Force | Out-Null
$env:TEMP = "C:\Temp"
$env:TMP  = "C:\Temp"
./build.ps1 -Action rebuild -Compiler gcc
```

### 3.3 Action 含义

| Action | 说明 |
|--------|------|
| `configure` | 仅运行 CMake 配置（生成 build.ninja） |
| `build` | 配置（如需）并增量编译 |
| `rebuild` | 删除构建目录后重新配置编译 |
| `clean` | 删除构建目录 |
| `download` | 编译（如产物不存在）并通过 AURIXFlasher 烧录，复位后启动 |
| `reset` | 通过 AURIXFlasher script 模式执行 `RESET 6`（reset & halt，之后设备正常运行） |
| `all` | `rebuild` + `download` |

---

## 四、新机器移植说明

### 4.1 前提检查

1. **安装 AURIX Development Studio**（提供 tricore-gcc11 + AURIXFlasher）
2. **安装商业版 TASKING**（如需 TASKING 编译）
3. **安装 CMake ≥ 3.24** 并加入 PATH
4. **安装 Ninja** 并加入 PATH（`winget install Ninja-build.Ninja`）

### 4.2 路径适配

默认路径写死在 `build.ps1` 参数默认值中，新机器只需通过参数覆盖，**无需修改脚本**：

```powershell
# ADS 版本不同
.\build.ps1 -AurixStudioPath "C:\Infineon\AURIX-Studio-1.10.32"

# TASKING 版本不同
.\build.ps1 -Compiler tasking -TaskingPath "C:\TASKING\TriCore_v6.4r1"

# 完全自定义路径
.\build.ps1 -GccBin "D:\tools\tricore-gcc11\bin" `
            -FlashTool "D:\tools\AURIXFlasher.exe"
```

### 4.3 CMake 工具链文件默认路径

`cmake/tricore-gcc-toolchain.cmake` 和 `cmake/tasking-tricore-toolchain.cmake` 中也有默认路径常量，通过 `-D` 参数可在 configure 时覆盖：

```powershell
# 若需临时覆盖（不推荐，使用 build.ps1 的参数即可）
cmake ... -DAURIX_TOOLCHAIN_BIN="D:\tools\tricore-gcc11\bin"
cmake ... -DAURIX_TASKING_BIN="D:\tools\TASKING\ctc\bin"
```

### 4.4 `.cproject` 排除规则同步

`CMakeLists.txt` 会自动读取 ADS 的 `.cproject` 中的 `excluding` 规则，与 ADS 保持同步的源文件集。若 `.cproject` 缺失，CMake 会扫描整个工程目录（仅排除 `build/`、`.ads/`、`.settings/` 等）。

### 4.5 常见问题

| 现象 | 原因 | 解决 |
|------|------|------|
| `tricore-elf-gcc.exe not found` | GCC bin 路径不对 | 检查 `-AurixStudioPath` 或 `-GccBin` |
| `cctc.exe not found` | TASKING 路径不对 | 检查 `-TaskingPath` 或 `-TaskingBin` |
| `F104: License does not support running as standalone` | 使用了 ADS 捆绑版 TASKING | 安装商业版 TASKING，更新 `-TaskingPath` |
| `AURIXFlasher.exe not found` | ADS 版本路径不对 | 检查 `-AurixStudioPath` 或 `-FlashTool` |
| `cmake: command not found` | CMake 未加入 PATH | 安装 CMake 并添加至 PATH |
| `ninja: command not found` | Ninja 未加入 PATH | `winget install Ninja-build.Ninja` |
| TASKING CMake 配置极慢 | 编译器 ABI 检测 | 正常现象，已通过 `CMAKE_C_COMPILER_WORKS=TRUE` 跳过链接测试 |

---

## 五、工程列表及测试结果

测试日期：**2026-06-16**  
硬件：**TC387（AURIX TC38x）**，下载器：**DAP Miniwiggler**

### 5.1 tc387_blink_gcc_test — LED 闪烁

基础验证工程，无外设依赖，仅 GPIO LED 闪烁 + 多核同步。

| 编译器 | 构建 | 烧录 | 运行 | ELF 大小 | HEX 大小 |
|--------|------|------|------|---------|---------|
| GCC 11.3.1 | ✅ Pass | ✅ Pass | ✅ LED 闪烁正常 | 1337.8 KB | 43.3 KB |
| TASKING v6.3r1 | ✅ Pass（1 warning） | ✅ Pass | ✅ LED 闪烁正常 | 31.6 KB | 31.6 KB |

> TASKING 的 ELF 体积远小于 GCC，因为 TASKING 默认对调试信息做了压缩且输出格式为 ihex（文件即为 hex 内容）。

### 5.2 tc387_uart_echo_gcc — UART Echo

ASCLIN4 UART，波特率 4 Mbps，调试串口 COM66，发送任意数据原样返回。

| 编译器 | 构建 | 烧录 | 运行 | ELF 大小 | HEX 大小 |
|--------|------|------|------|---------|---------|
| GCC 11.3.1 | ✅ Pass | ✅ Pass | ✅ Echo 正常 | 1591.0 KB | 67.4 KB |

**串口测试**（COM66，4000000 baud）：

```
发送: Hello!
接收: Hello!
```

### 5.3 tc387_can_x12_gcc — CAN × 12 路

12 路 CAN/CAN-FD 节点，仅做编译+烧录验证（需专用 CAN 分析仪测试收发）。

| 编译器 | 构建 | 烧录 | ELF 大小 | HEX 大小 |
|--------|------|------|---------|---------|
| GCC 11.3.1 | ✅ Pass | ✅ Pass | 1749.0 KB | 79.4 KB |

> CAN 收发功能需外接 CAN 分析仪验证，此处仅确认编译烧录无误。

### 5.4 tc387_lwip_iperf_gcc — LwIP TCP/IP + iperf

LwIP 以太网协议栈，iperf TCP server，静态 IP：`192.168.0.100/24`。

| 编译器 | 构建 | 烧录 | Ping | iperf 吞吐量 | ELF 大小 | HEX 大小 |
|--------|------|------|------|------------|---------|---------|
| GCC 11.3.1 | ✅ Pass | ✅ Pass | ✅ 0 丢包 | ~134 Mbps | 4921.9 KB | 365.8 KB |

**Ping 测试**（5 包，0 丢失，RTT < 1 ms）：
```
来自 192.168.0.100 的回复: 字节=32 时间<1ms TTL=255  (×4)
来自 192.168.0.100 的回复: 字节=32 时间=2ms TTL=255   (×1)
```

**iperf 测试**（TCP，5 秒，客户端 `192.168.0.2 → 192.168.0.100:5001`）：
```
[256]  0.0- 1.0 sec  15.3 MBytes   129 Mbits/sec
[256]  1.0- 2.0 sec  16.9 MBytes   142 Mbits/sec
[256]  2.0- 3.0 sec  15.9 MBytes   133 Mbits/sec
[256]  3.0- 4.0 sec  15.7 MBytes   132 Mbits/sec
[256]  4.0- 5.0 sec  16.1 MBytes   135 Mbits/sec
[256]  0.0- 5.0 sec  79.9 MBytes   134 Mbits/sec
```

```powershell
# iperf 命令（iperf.exe 在 tc387\bak\ 目录）
.\iperf.exe -c 192.168.0.100 -t 5 -i 1
```

---

## 六、cmake 文件说明

### cmake/AurixProject.cmake

通用工具函数库，三个核心函数：

| 函数 | 说明 |
|------|------|
| `aurix_normalize_path` | 路径统一为正斜杠，移除尾部斜杠 |
| `aurix_collect_project_files` | 递归扫描工程目录，收集 `.c`/`.h` 及 include 目录，支持排除规则 |
| `aurix_extract_cproject_excluded_dirs` | 从 ADS `.cproject` 解析 `excluding=` 字段，自动同步 ADS 排除目录 |

### cmake/tricore-gcc-toolchain.cmake

TriCore GCC 工具链文件：
- CPU：`tc38xx`（GCC 命名规则）
- 默认 bin 路径：`C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin`
- 通过 `-DAURIX_TOOLCHAIN_BIN=` 覆盖

### cmake/tasking-tricore-toolchain.cmake

TASKING 工具链文件：
- CPU：`tc38x`（TASKING 命名规则）
- 默认 bin 路径：`C:/z/app/TASKING/TriCore_v6.3r1/ctc/bin`
- 通过 `-DAURIX_TASKING_BIN=` 覆盖
- 设置 `CMAKE_C_COMPILER_WORKS=TRUE` 跳过 CMake 链接测试（裸机目标无法通过标准链接探测）
- 设置 `CMAKE_DEPENDS_IN_PROJECT_ONLY=ON` 避免依赖扫描时处理 TASKING 系统头文件

### CMakeLists.txt 关键逻辑

- 根据 `AURIX_TOOLCHAIN_TYPE`（由工具链文件注入）自动选择：
  - GCC：`-mcpu=tc38xx`，链接脚本 `Lcf_Gnuc_Tricore_Tc.lsl`
  - TASKING：`-Ctc38x`，链接脚本 `Lcf_Tasking_Tricore_Tc.lsl`
- 通过 `aurix_extract_cproject_excluded_dirs` 从 `.cproject` 同步 ADS 排除规则
- GCC 产物：`.elf` + `.hex`（objcopy）+ `.map`
- TASKING 产物：`.elf`（ihex 格式内容）+ `.hex`（post_build copy）

---

## 七、本机工具安装路径速查

```
CMake         : C:\z\app\CMake\bin\cmake.exe          (v3.27.4)
Ninja         : winget/PATH                            (v1.13.2)
tricore-gcc11 : C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin
TASKING       : C:\z\app\TASKING\TriCore_v6.3r1\ctc\bin
AURIXFlasher  : C:\Infineon\AURIX-Studio-1.10.28\tools\AurixFlasherSoftwareTool_v3.0.14\AURIXFlasher.exe
make (ADS)    : C:\Infineon\AURIX-Studio-1.10.28\tools\make\make.exe
iperf         : C:\git\embedded\tc387\bak\iperf.exe
```
