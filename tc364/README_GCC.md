# TC364 CMake 命令行构建指南（GCC + TASKING 双工具链）

本文档说明如何在不依赖 AURIX Development Studio（ADS）IDE 的情况下，通过 PowerShell + CMake 对 TC364 系列工程进行清理、编译、下载、复位和串口验证。工程同时支持 TriCore GCC 和 TASKING 两种工具链，ADS IDE 功能不受影响。

参考工程：`tc364/tc364_uart0_gcc`（由 `tc364/0_Board_Test_UART0` 拷贝改造而来）。

---

## 一、工具链与软件依赖

### 1.1 必须安装

| 工具 | 版本（实测）| 默认安装路径 | 说明 |
|------|------------|------------|------|
| AURIX Development Studio | 1.10.28 | `C:\Infineon\AURIX-Studio-1.10.28` | 内含 tricore-gcc11、AURIXFlasher、make |
| CMake | ≥ 3.24（实测 3.27.4）| 系统 PATH | 低于 3.24 会报错 |
| Ninja | ≥ 1.10（实测 1.13.0）| 系统 PATH | GCC/TASKING 构建后端 |
| PowerShell | 5.1 或 7.x | 系统自带 | 构建脚本运行环境 |

推荐通过 winget 安装：`winget install Kitware.CMake Ninja-build.Ninja`

### 1.2 TriCore GCC（随 ADS 捆绑）

ADS 自带 tricore-gcc11，无需单独安装：

```
C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin\tricore-elf-gcc.exe
```

版本：**GCC 11.3.1 20221230**（AURIX(tm) GCC, Built 2025-09-08）

> **重要：TC364 的 `-mcpu` 取值是 `tc33xx`，不是 `tc36xx`。**
>
> tricore-gcc11 的 `-mcpu` 只接受**设备系列名**，完整可选值为：
> `tc22xx tc23xx tc26xx tc27xx tc29xx tc33xx tc38xx tc39xx tc49xx`（另有内核架构名 `tc161/tc162/tc18`）。
>
> 列表中**没有 `tc36xx`**。TC36x 与 TC33x 同为 TriCore 1.6.2P 内核、指令集完全一致，
> 官方 AURIX Studio 对 TC36x 的 GCC 配置同样使用 `tc33xx`。
> 误填 `tc36xx` 会报 `error: unrecognized argument in option '-mcpu=tc36xx'`。

### 1.3 TASKING TriCore 编译器（需商业授权）

商业版 TASKING TriCore 编译器，**单独安装**，不含在 ADS 内：

```
C:\z\app\TASKING\TriCore_v6.3r1\ctc\bin\cctc.exe
```

版本：**TASKING VX-toolset for TriCore v6.3r1** Build 19041558

TASKING 侧 TC364 使用 `-Ctc36x`（支持设备名，与 GCC 命名规则不同）。

> **注意**：ADS 捆绑的 TASKING（非商业版）仅允许在 ADS IDE 内使用，命令行独立运行会报
> `F104: License does not support running as standalone`。命令行编译必须使用单独购买的商业版 TASKING。

### 1.4 烧录工具（随 ADS 捆绑）

```
C:\Infineon\AURIX-Studio-1.10.28\tools\AurixFlasherSoftwareTool_v3.0.14\AURIXFlasher.exe
```

版本：**AURIXFlasher 3.0.14**，连接方式 DAP MiniWiggler（DAS 协议），`-connect 6`。

---

## 二、工程目录结构

改造后新增的文件如下（ADS 原有文件均未修改，IDE 可继续正常使用）：

```
tc364_uart0_gcc/
├── CMakeLists.txt                        # CMake 构建定义（GCC/TASKING 双工具链）
├── build.ps1                             # PowerShell 构建/烧录/复位/串口脚本
├── .gitignore                            # 忽略 build/ 与构建产物
├── cmake/
│   ├── AurixProject.cmake                # 源码递归扫描 + .cproject 排除规则解析
│   ├── tricore-gcc-toolchain.cmake       # TriCore GCC 工具链定义
│   ├── tasking-tricore-toolchain.cmake   # TASKING 工具链定义
│   └── gcc-link-wrapper.cmake            # GCC 链接包装（展开 @response 文件）
├── Configurations/
│   └── Ifx_Gnuc_CppInit.c                # 新增：GCC 下的 _init/_fini 实现
├── Lcf_Gnuc_Tricore_Tc.lsl               # GCC 链接脚本（ADS 原有）
├── Lcf_Tasking_Tricore_Tc.lsl            # TASKING 链接脚本（ADS 原有）
├── Cpu0_Main.c / Cpu1_Main.c             # 应用代码（ADS 原有）
├── Configurations/ Libraries/            # iLLD 及配置（ADS 原有）
└── build/                                # CMake 构建输出（gitignore）
    ├── gcc/                              # GCC 构建产物
    └── tasking/                          # TASKING 构建产物
```

源码采集方式：`AurixProject.cmake` 递归扫描工程目录下所有 `.c`，并自动解析 `.cproject`
中的 `excluding=` 规则，排除其他 MCU 型号的 iLLD 代码（本工程保留 TC36x，排除 TC33x/TC37x/TC38x/TC39x 等）。
新增源文件无需修改 CMakeLists.txt，重新 configure 即可（`CONFIGURE_DEPENDS`）。

---

## 三、快速开始

```powershell
cd C:\github\embedded\tc364\tc364_uart0_gcc

# GCC 编译（默认）
.\build.ps1

# GCC 编译并烧录
.\build.ps1 -Action download

# TASKING 编译并烧录
.\build.ps1 -Compiler tasking -Action download

# 一条龙：清理 + 编译 + 烧录 + 串口验证
.\build.ps1 -Action all
```

---

## 四、build.ps1 参数说明

### 4.1 Action（操作）

| Action | 说明 |
|--------|------|
| `configure` | 仅执行 CMake 配置 |
| `build` | 配置 + 编译（**默认**）|
| `rebuild` | 删除构建目录后重新配置编译 |
| `clean` | 删除构建目录 |
| `download` | 编译（如需要）后通过 AURIXFlasher 烧录并启动 |
| `reset` | 仅复位目标板 |
| `monitor` | 复位后读取串口输出，并回送 `PING` 验证 echo |
| `all` | clean → build → download → monitor |

### 4.2 其他参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-Compiler` | `gcc` | `gcc` 或 `tasking` |
| `-BuildType` | `Debug` | `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` |
| `-BuildDir` | `build/<compiler>` | 构建目录 |
| `-AurixStudioPath` | `C:\Infineon\AURIX-Studio-1.10.28` | ADS 根目录，自动推导 GCC/Flasher/make |
| `-TaskingPath` | `C:\z\app\TASKING\TriCore_v6.3r1` | 商业版 TASKING 根目录 |
| `-GccBin` / `-TaskingBin` / `-FlashTool` | 空 | 细粒度覆盖，留空则自动推导 |
| `-DasPort` | `0` | DAP/DAS 端口索引，多个下载器时区分 |
| `-SerialPort` | `COM127` | 调试串口 |
| `-BaudRate` | `4000000` | 波特率，需与固件 `init_uart0()` 一致 |
| `-MonitorSeconds` | `5` | `monitor` 读取时长 |

换机器只需覆盖高层路径：

```powershell
.\build.ps1 -AurixStudioPath "C:\Infineon\AURIX-Studio-1.10.32"
.\build.ps1 -Compiler tasking -TaskingPath "C:\TASKING\TriCore_v6.4r1"
```

---

## 五、直接使用 CMake（不经过 build.ps1）

```powershell
# GCC
cmake -S . -B build/gcc -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=cmake/tricore-gcc-toolchain.cmake `
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build/gcc -j

# TASKING
cmake -S . -B build/tasking -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=cmake/tasking-tricore-toolchain.cmake `
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tasking -j
```

可覆盖的 CMake 缓存变量：

| 变量 | 说明 |
|------|------|
| `AURIX_TOOLCHAIN_BIN` | GCC bin 目录 |
| `AURIX_TASKING_BIN` | TASKING ctc\bin 目录 |
| `AURIX_CPU` | CPU 名，GCC 默认 `tc33xx`，TASKING 默认 `tc36x` |
| `AURIX_LINKER_SCRIPT` | 链接脚本路径，按工具链自动选择 |
| `AURIX_TARGET_NAME` | 输出文件名（默认 `tc364_uart0_gcc`）|

---

## 六、构建产物

| 文件 | GCC | TASKING |
|------|-----|---------|
| `build/<compiler>/tc364_uart0_gcc.elf` | ELF 可执行（含调试信息）| 链接器直接输出的 ihex 内容 |
| `build/<compiler>/tc364_uart0_gcc.hex` | 由 `objcopy -O ihex` 生成 | 由 `.elf` 复制而来 |
| `build/<compiler>/tc364_uart0_gcc.map` | 链接映射表 | — |

> TASKING 使用 `--format=ihex`，链接器直接产出 Intel HEX 内容，但 CMake 强制文件名后缀为
> `.elf`，因此 post-build 步骤会把它复制成 `.hex` 供烧录工具使用。这也意味着 TASKING
> 构建下 `.elf` 并非真正的 ELF 文件（如需 ELF 调试请去掉 `--format=ihex`）。

实测 GCC Debug 体积：

```
   text    data     bss     dec     hex
  22082      56   27520   49658    c1fa
```

---

## 七、实测验证结果

硬件：TC364 开发板 + DAP MiniWiggler，调试串口 COM127 @ 4000000 8N1。

### 7.1 编译

| 工具链 | 结果 | 源文件数 |
|--------|------|---------|
| GCC (`-mcpu=tc33xx`) | 通过，0 error / 2 warnings | 211 |
| TASKING (`-Ctc36x`) | 通过，0 error / 7 warnings | 211 |

> 上述 warning 均来自 iLLD 官方库源码（未修改），不影响功能。

### 7.2 烧录

```
AURIX Flasher Software Tool 3.0.14.0
::Connected to device... [TC3x] TC36x
::Erasing Flash memory .......... (Pass)
::Programming Flash memory ..........(Pass)
::Verifying Flash memory ..........
::Flash memory matches expected value (Pass)
AURIXFlasher Exit Status: Pass
```

> 烧录过程中的 `Warning: The given program file is trying to write on reserved memory
> addresses, which will be skipped.` 属正常现象（BMHD 保留区），不影响运行。

### 7.3 串口输出

`.\build.ps1 -Action monitor` 实测（GCC 与 TASKING 固件表现一致）：

```
---- COM127 output ----
UART Echo Advanced Ready
PING
---------------------------
```

其中 `UART Echo Advanced Ready` 为固件开机横幅，`PING` 为脚本发送后被固件回显，
说明 ASCLIN0 收发均正常。

---

## 八、改造过程中遇到的问题与解决

以下是把 ADS 工程改成 CMake 工程时实际踩到的坑，供其他型号移植参考。

### 8.1 `-mcpu=tc36xx` 无法识别

**现象**

```
error: unrecognized argument in option '-mcpu=tc36xx'
note: valid arguments to '-mcpu=' are: tc161 tc162 tc18 tc22xx tc23xx
      tc26xx tc27xx tc29xx tc33xx tc38xx tc39xx tc49xx
```

**原因** tricore-gcc11 的 `-mcpu` 只接受设备系列名，没有 `tc36xx` 这一项。

**解决** TC36x 改用 `tc33xx`（同为 TriCore 1.6.2P 内核）。
注意 TASKING 侧仍然是 `-Ctc36x`，两套工具链命名规则不同，CMakeLists.txt 中按
`AURIX_TOOLCHAIN_TYPE` 分别设置默认值。

### 8.2 `-nocrt0` 不是 GCC driver 选项

**现象** 链接期报 `ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX: Invalid argument`。

**原因** `-nocrt0` 并非 GCC 选项，GCC driver 不认识就原样透传给 `ld`，破坏了 ld 的命令行。

**解决** 改用标准的 `-nostartfiles`。启动代码由工程内的
`Ifx_Ssw_Tc0.c` 和 `Lcf_Gnuc_Tricore_Tc.lsl` 提供。

### 8.3 `undefined reference to '_init'`

**现象** 使用 `-nostartfiles` 后链接报 `_init` 未定义。

**原因** `Libraries/Infra/Ssw/TC3xx/Tricore/Ifx_Ssw_Infra.c` 的 `Ifx_Ssw_doCppInit()`
在 `__GNUC__` 分支会调用 `_init()`（C++ 全局构造入口）。`_init`/`_fini` 正常由
`crti.o`/`crtn.o` 提供，而 `-nostartfiles` 恰好把它们排除了。

**解决** 新增 `Configurations/Ifx_Gnuc_CppInit.c`，提供遍历 `.init_array` /
`.fini_array` 的 `_init`/`_fini` 实现。该文件用
`#if defined(__GNUC__) && !defined(__HIGHTEC__) && !defined(__TASKING__)` 包裹，
TASKING 构建时为空文件，不会冲突。

### 8.4 cc1.exe / ld.exe 不支持 @response 文件（**最主要的坑**）

**现象**

```
cc1.exe: error: too many filenames given; type 'cc1.exe --help' for usage
cc1.exe: fatal error: @C:\WINDOWS\TEMP\ccXXXXXX: Invalid argument
ld.exe:  cannot find @C:\WINDOWS\TEMP\ccXXXXXX: Invalid argument
```

**原因** tricore-elf 工具链自带的 `cc1.exe` 和 `ld.exe` 都不支持 `@file` 语法。
而 CMake 的 Ninja 生成器在 Windows 上默认会把 include 列表和对象列表写进 `.rsp`
文件，以 `@xxx.rsp` 形式传给 gcc，gcc 再转交给 cc1/ld，于是失败。

**解决（分两部分）**

1. **编译阶段**：在 `CMakeLists.txt` 中、**`project()` 调用之前**关闭 response file：

   ```cmake
   foreach(_lang C ASM)
       set(CMAKE_${_lang}_USE_RESPONSE_FILE_FOR_INCLUDES  0)
       set(CMAKE_${_lang}_USE_RESPONSE_FILE_FOR_OBJECTS   0)
       set(CMAKE_${_lang}_USE_RESPONSE_FILE_FOR_LIBRARIES 0)
   endforeach()
   ```

   注意两点：
   - 必须在 `project()` **之前**，Ninja 生成器在 `project()` 期间就固化了这个决定，
     写在 toolchain 文件里或 `project()` 之后都不生效；
   - **不要定义 `CMAKE_NINJA_FORCE_RESPONSE_FILE`**，Ninja 生成器只检查该变量是否被
     "定义"，即使赋值为 `OFF` 也会强制启用 response file。

2. **链接阶段**：链接规则的 `RSP_FILE` 是 Ninja 生成器硬编码的，无法用变量关闭。
   改为经 `cmake/gcc-link-wrapper.cmake` 中转 —— 该脚本把命令行里的 `@xxx.rsp`
   就地展开成真实对象列表后再调用 gcc：

   ```cmake
   set(CMAKE_C_LINK_EXECUTABLE
       "\"${CMAKE_COMMAND}\" -DGCC=<CMAKE_C_COMPILER> \"-DARGS=...|<OBJECTS>|...\" -P \"${_aurix_link_wrapper}\"")
   ```

本工程展开后编译命令行约 3.6 KB、链接命令行约 21 KB，均低于 Windows `cmd.exe`
的 32 KB 上限，可安全展开。若移植到源文件更多的工程导致超限，可改为缩短路径
（如把工程放到更浅的目录）或改用 `Unix Makefiles` 生成器。

### 8.5 TASKING 编译选项重复导致 E204

**现象** `cctc E204: unknown option` 或选项冲突。

**原因** CMake 识别到 Tasking 6.3 后会自动往 `CMAKE_C_FLAGS` 注入
`-O0 -g --iso=99 --strict`，与 `target_compile_options` 中手写的同名选项重复。

**解决** 在 TASKING 分支中清空默认 flags，由 `target_compile_options` 统一接管：

```cmake
set(CMAKE_C_FLAGS         "" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DEBUG   "" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE "" CACHE STRING "" FORCE)
```

同时 `--iso=99 --strict` 交给 CMake 的 `C_STANDARD 99` / `C_EXTENSIONS NO` 自动生成，
`-O0 -g` 交给 `CMAKE_BUILD_TYPE` 自动生成，都不要在 `target_compile_options` 里重复写。

### 8.6 TASKING 配置阶段编译器检测失败

**原因** TASKING 链接可执行文件必须提供目标专用的 `.lsl`，CMake 的编译器检测阶段
没有链接脚本，必然失败。

**解决** 在 `tasking-tricore-toolchain.cmake` 中：

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)   # 检测只编静态库，不链接
set(CMAKE_C_COMPILER_WORKS   TRUE CACHE BOOL "")
set(CMAKE_ASM_COMPILER_WORKS TRUE CACHE BOOL "")
set(CMAKE_DEPENDS_IN_PROJECT_ONLY ON)               # 不扫描 TASKING 系统头文件
```

### 8.7 复位时报"拒绝访问"

**现象** `monitor` 动作中调用 `reset` 时报 `Reset skipped: 拒绝访问。`

**原因** 复位脚本原先写在 `C:\WINDOWS\TEMP` 下，该环境中 AURIXFlasher 读取该目录被拒。

**解决** 把复位脚本改写到构建目录下的 `aurix_reset.txt`。

---

## 九、常见问题

**Q：`CMake 3.24 or higher is required`**
升级 CMake：`winget install Kitware.CMake`。

**Q：`tricore-elf-gcc.exe not found`**
用 `-AurixStudioPath` 指定实际 ADS 安装路径，或用 `-GccBin` 直接指定 bin 目录。

**Q：TASKING 报 `F104: License does not support running as standalone`**
用的是 ADS 捆绑版 TASKING。命令行编译必须使用单独安装的商业版，用 `-TaskingPath` 指定。

**Q：`AURIXFlasher.exe not found`**
脚本会在 `<AurixStudioPath>\tools` 下自动搜索 `AurixFlasherSoftwareTool*` 目录并取最新版。
若安装位置特殊，用 `-FlashTool` 直接指定完整路径。

**Q：串口没有输出**
1. 确认 `-SerialPort` 与实际端口一致（`[System.IO.Ports.SerialPort]::GetPortNames()` 可列出）；
2. 确认波特率与固件 `Cpu0_Main.c` 中 `init_uart0()` 的设置一致（本工程 4000000）；
3. 确认固件已烧录并已复位启动。

**Q：改了源码但没重新编译**
`CONFIGURE_DEPENDS` 只在重新运行 CMake 时刷新文件列表。新增/删除文件后建议
`.\build.ps1 -Action rebuild`。

**Q：ADS IDE 还能用吗？**
能。所有新增文件都不影响 ADS，`.project` / `.cproject` 未做修改，IDE 内编译调试照常。
