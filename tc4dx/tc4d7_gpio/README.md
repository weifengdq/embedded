# tc4d7_gpio 构建系统与链接脚本详解

本文档对本工程的 CMake 相关文件和链接脚本进行集中说明，目标是回答四个问题：

1. 这个工程的 CMake 是怎样把源文件、头文件、include 目录收集起来的。
2. TriCore GCC 工具链是怎样接入 CMake 的。
3. 链接脚本 Lcf_Gcc_Tricore_Tc.lsl 在内存布局上做了什么。
4. 最终会生成哪些构建产物，以及这些产物分别有什么用途。

## 文件总览

| 文件 | 角色 | 主要职责 | 是否已补中文注释 |
| --- | --- | --- | --- |
| CMakeLists.txt | 顶层工程脚本 | 定义 target、构建类型、编译链接选项、hex/size 目标 | 是 |
| cmake/AurixProject.cmake | 工程辅助脚本 | 路径归一化、排除规则判断、源码递归扫描、ADS 排除规则导入 | 是 |
| cmake/tricore-gcc-toolchain.cmake | 工具链脚本 | 指定 tricore-gcc11 交叉编译器与 binutils | 是 |
| Lcf_Gcc_Tricore_Tc.lsl | 链接脚本 | 定义 TriCore 多核内存区域、节布局、启动入口、向量表位置 | 否，本文详细解释 |

## 一、CMake 构建链路总览

### 1.1 构建流程表

| 阶段 | 入口文件 | 关键动作 | 输出/结果 |
| --- | --- | --- | --- |
| 工具链初始化 | cmake/tricore-gcc-toolchain.cmake | 告诉 CMake 当前目标平台是 Generic tricore，设置 gcc/ar/objcopy/size 等工具路径 | CMake 获得交叉编译能力 |
| 工程配置 | CMakeLists.txt | 设置工程名、默认构建类型、链接脚本、CPU 变体、排除规则 | 形成目标级配置 |
| 源码扫描 | cmake/AurixProject.cmake | 递归扫描 .c/.h，并从 .cproject 导入 excluding 目录 | 生成源文件、头文件、include 目录列表 |
| 编译 | CMakeLists.txt | 按构建类型附加优化/调试参数，并统一附加 TriCore 通用编译选项 | 生成 .o 文件 |
| 链接 | CMakeLists.txt + Lcf_Gcc_Tricore_Tc.lsl | 使用链接脚本放置各节到指定存储区，并输出 map 文件 | 生成 tc4d7_gpio.elf 与 tc4d7_gpio.map |
| 后处理 | CMakeLists.txt | 调用 objcopy 把 ELF 转成 Intel HEX | 生成 tc4d7_gpio.hex |
| 尺寸统计 | CMakeLists.txt | 调用 size 查看节占用 | 输出代码/数据大小摘要 |

### 1.2 构建关系图

| 输入 | 经过 | 产出 | 说明 |
| --- | --- | --- | --- |
| 源代码与头文件 | aurix_collect_project_files | AURIX_SOURCE_FILES / AURIX_HEADER_FILES / AURIX_INCLUDE_DIRS | 来自工程目录递归扫描 |
| .cproject excluding 规则 | aurix_extract_cproject_excluded_dirs | AURIX_ADS_EXCLUDED_DIRS | 让 ADS 和 CMake 排除规则尽量一致 |
| tricore-gcc11 工具链 | tricore-gcc-toolchain.cmake | CMAKE_C_COMPILER 等工具变量 | 供编译/链接/objcopy 使用 |
| Lcf_Gcc_Tricore_Tc.lsl | target_link_options | .elf + .map | 决定程序和数据的最终地址布局 |

## 二、CMakeLists.txt 详细解释

### 2.1 顶层职责汇总表

| 配置块 | 关键语句/变量 | 作用 | 备注 |
| --- | --- | --- | --- |
| 工程定义 | project(tc4d7_gpio LANGUAGES C) | 定义工程名和语言 | PROJECT_NAME 后续也作为默认 target 名 |
| 默认构建类型 | CMAKE_BUILD_TYPE | 未指定时默认 Debug | 适合首次调试 |
| 辅助脚本引入 | include(cmake/AurixProject.cmake) | 加载扫描与排除逻辑 | 把复杂文件收集逻辑从顶层脚本剥离 |
| AURIX 缓存变量 | AURIX_TARGET_NAME / AURIX_CPU / AURIX_LINKER_SCRIPT / AURIX_CPROJECT_FILE | 提供核心可调配置入口 | 可在 CMake GUI 或命令行覆盖 |
| 排除目录 | AURIX_EXCLUDED_DIRS / AURIX_EXCLUDED_FILES | 避免扫描构建输出目录 | 防止递归扫描把 build 产物收进 target |
| ADS 规则同步 | aurix_extract_cproject_excluded_dirs | 导入 ADS 的 excluding 路径 | 对导出工程非常关键 |
| 文件收集 | aurix_collect_project_files | 收集 .c/.h/include 目录 | 自动化程度高 |
| 目标创建 | add_executable | 生成 ELF 目标 | 裸机工程的主构建目标 |
| 编译选项 | target_compile_options | 设置优化、调试、mcpu、section 粒度等 | 与链接时 gc-sections 配套 |
| 链接选项 | target_link_options | 指定 lsl、禁用默认 crt0、输出 map | 启动流程由工程自带启动代码负责 |
| 后处理目标 | add_custom_command / add_custom_target | 生成 hex 和 size 输出 | 便于烧录与分析 |

### 2.2 关键缓存变量说明

| 变量 | 默认值 | 含义 | 常见修改场景 |
| --- | --- | --- | --- |
| AURIX_TARGET_NAME | tc4d7_gpio | 输出文件基名 | 一个工程想保留目录名但输出其他文件名时 |
| AURIX_CPU | tc4DAx | 传给 -mcpu 的 TriCore CPU 型号 | 切换到同系列其他器件时 |
| AURIX_LINKER_SCRIPT | 当前目录下的 Lcf_Gcc_Tricore_Tc.lsl | 链接脚本路径 | 更换存储布局或多核布局时 |
| AURIX_CPROJECT_FILE | 当前目录下的 .cproject | ADS 工程配置文件 | 需要同步 ADS 的排除规则时 |
| AURIX_EXCLUDED_DIRS | build、TriCore Debug (GCC)、TriCore Release (GCC) | 手工指定排除目录 | 添加 demo/reference/output 目录时 |
| AURIX_EXCLUDED_FILES | 空 | 手工指定排除文件 | 个别源文件需强制跳过时 |

### 2.3 编译与链接选项说明表

| 选项 | 阶段 | 作用 | 实际意义 |
| --- | --- | --- | --- |
| -Og | Debug 编译 | 兼顾优化与可调试性 | 比 -O0 更接近真实运行状态 |
| -g3 / -gdwarf-3 | Debug 编译 | 生成更完整调试信息 | 便于断点和变量查看 |
| -O3 | Release 编译 | 高优化 | 用于最终性能版本 |
| -DNDEBUG | Release/RelWithDebInfo/MinSizeRel | 关闭 assert 等调试逻辑 | 减少开销 |
| -Wall | 编译 | 打开常见警告 | 基本质量门槛 |
| -fno-common | 编译 | 禁止重复定义被合并 | 更早暴露全局变量定义错误 |
| -fstrict-volatile-bitfields | 编译 | 严格按照位域访问硬件寄存器 | 对 MCU 寄存器访问更安全 |
| -fdata-sections / -ffunction-sections | 编译 | 每个数据/函数放进独立节 | 让链接器能做未引用节裁剪 |
| -mcpu=tc4DAx | 编译/链接 | 指定目标 CPU 架构 | 保证指令集和 ABI 匹配 |
| -T...lsl | 链接 | 指定链接脚本 | 决定内存布局 |
| -nocrt0 | 链接 | 不使用默认 CRT 启动文件 | 由 AURIX 启动文件接管复位流程 |
| -Wl,--gc-sections | 链接 | 删除未引用节 | 减少最终镜像体积 |
| -Wl,-Map,...map | 链接 | 输出 map 文件 | 用于分析节地址和大小 |

### 2.4 产物说明表

| 产物 | 生成方式 | 用途 |
| --- | --- | --- |
| tc4d7_gpio.elf | add_executable 链接输出 | 调试、反汇编、下载、符号分析 |
| tc4d7_gpio.map | 链接器 -Map 输出 | 查看节分布、符号大小、内存占用 |
| tc4d7_gpio.hex | objcopy 从 ELF 转换 | 常用于烧录工具下载 |
| tc4d7_gpio_size | 自定义目标 | 查看程序总体占用统计 |

## 三、cmake/AurixProject.cmake 详细解释

### 3.1 函数汇总表

| 函数 | 输入 | 输出 | 作用 | 关键点 |
| --- | --- | --- | --- | --- |
| aurix_normalize_path | 输入路径、输出变量名 | 归一化后的路径 | 把路径统一成 CMake 风格 | 去掉尾部多余斜杠，避免字符串比较误判 |
| aurix_path_is_excluded | 候选路径、排除目录、排除文件、输出变量名 | TRUE/FALSE | 判断路径是否应被排除 | 同时支持目录树排除和单文件精确排除 |
| aurix_collect_directory_entries | 当前目录、排除目录、排除文件、输出变量名 | sources/headers/include_dirs | 递归扫描目录 | 每个子目录默认加入 include dirs |
| aurix_collect_project_files | ROOT_DIR 等参数 | 工程级 sources/headers/include_dirs | 上层总入口 | 负责去重、排序、补根目录 |
| aurix_extract_cproject_excluded_dirs | .cproject、ROOT_DIR、输出变量名 | 排除目录列表 | 从 ADS 工程导入 excluding 规则 | 解决 ADS 与 CMake 扫描结果不一致问题 |

### 3.2 递归扫描策略说明表

| 策略 | 当前实现 | 好处 | 风险/注意事项 |
| --- | --- | --- | --- |
| 源文件收集 | 递归扫描所有 .c | 新增模块时通常不用手写文件列表 | 排除规则必须正确，否则容易误收参考目录 |
| 头文件收集 | 递归扫描所有 .h | IDE 能更完整感知工程文件 | 头文件本身不参与编译，只是附加到 target |
| include 目录收集 | 每个目录都加入 include 路径 | 少写 include_directories | include 路径较多时会增大搜索范围 |
| 排除目录判定 | 完整匹配 + 前缀匹配 | 子目录会整体被跳过 | 需要先做路径归一化 |
| 排除文件判定 | 精确匹配 | 粒度更细 | 不会自动排除同名不同路径文件 |

### 3.3 为什么要解析 .cproject

| 问题 | 现象 | 解析 .cproject 后的收益 |
| --- | --- | --- |
| ADS 工程里排除了某些目录，但 CMake 不知道 | CMake 可能把这些目录重新编进来 | CMake 和 ADS 的“有效源文件集合”更接近 |
| 工程被复制后目录结构复杂 | 递归扫描容易扫到参考代码或生成目录 | 减少误编译和链接冲突 |
| 多套构建系统并存 | 一个能编，一个不能编 | 通过同步排除规则降低分叉 |

## 四、cmake/tricore-gcc-toolchain.cmake 详细解释

### 4.1 工具链变量表

| 变量 | 示例值 | 作用 |
| --- | --- | --- |
| CMAKE_SYSTEM_NAME | Generic | 告诉 CMake 当前不是桌面 OS，而是通用裸机目标 |
| CMAKE_SYSTEM_PROCESSOR | tricore | 标识目标架构 |
| CMAKE_TRY_COMPILE_TARGET_TYPE | STATIC_LIBRARY | 避免配置阶段 try_compile 去链接裸机可执行文件 |
| AURIX_TOOLCHAIN_BIN | C:/Infineon/.../tricore-gcc11/bin | 工具链 bin 目录 |
| CMAKE_C_COMPILER | tricore-elf-gcc.exe | C 编译器 |
| CMAKE_CXX_COMPILER | tricore-elf-g++.exe | C++ 编译器 |
| CMAKE_ASM_COMPILER | tricore-elf-gcc.exe | 汇编编译器 |
| CMAKE_AR | tricore-elf-ar.exe | 静态库归档工具 |
| CMAKE_RANLIB | tricore-elf-ranlib.exe | 静态库索引工具 |
| CMAKE_OBJCOPY | tricore-elf-objcopy.exe | ELF 转 hex/bin 等格式 |
| CMAKE_OBJDUMP | tricore-elf-objdump.exe | 反汇编/节查看 |
| CMAKE_SIZE | tricore-elf-size.exe | 查看节大小 |

### 4.2 关键设计说明表

| 设计点 | 当前做法 | 原因 |
| --- | --- | --- |
| try_compile 行为 | 强制 STATIC_LIBRARY | 裸机环境下默认链接探测经常失败 |
| 工具存在性检查 | foreach + EXISTS | 尽早在配置阶段暴露路径错误 |
| 可执行后缀 | .elf | 符合嵌入式产物习惯 |
| FIND_ROOT_PATH 模式 | 全部设为 NEVER | 避免 CMake 擅自把主机路径当交叉根路径重写 |

## 五、Lcf_Gcc_Tricore_Tc.lsl 链接脚本详细解释

链接脚本是本工程最关键的“内存布局总说明书”。编译器只负责把每个 .c 文件变成若干节，真正决定“代码和数据最后放到哪里”的，是这个 .lsl 文件。

### 5.1 文件级作用表

| 项目 | 内容 | 说明 |
| --- | --- | --- |
| 输出格式 | elf32-tricore | 最终 ELF 面向 TriCore 架构 |
| 输出架构 | tricore | 链接器按 TriCore 规则布局 |
| 入口符号 | _START | 程序复位后从启动入口开始执行 |
| 目标类型 | Default linker script for normal executables | 标准裸机可执行镜像 |

### 5.2 每核运行时保留区配置表

| CPU | CSA 大小 | 用户栈 USTACK | 中断栈 ISTACK | DSPR 起始地址 | DSPR 大小 |
| --- | --- | --- | --- | --- | --- |
| CPU0 | 8 KB | 2 KB | 1 KB | 0x70000000 | 240 KB |
| CPU1 | 8 KB | 2 KB | 1 KB | 0x60000000 | 240 KB |
| CPU2 | 8 KB | 2 KB | 1 KB | 0x50000000 | 240 KB |
| CPU3 | 8 KB | 2 KB | 1 KB | 0x40000000 | 240 KB |
| CPU4 | 8 KB | 2 KB | 1 KB | 0x30000000 | 240 KB |
| CPU5 | 8 KB | 2 KB | 1 KB | 0x20000000 | 240 KB |

说明：

| 项目 | 含义 |
| --- | --- |
| CSA | Context Save Area，TriCore 硬件上下文保存区，函数调用与中断切换高度依赖它 |
| USTACK | 普通线程/主循环使用的用户栈 |
| ISTACK | 中断服务例程使用的中断栈 |

### 5.3 堆与偏移计算说明表

| 项目 | 表达式 | 说明 |
| --- | --- | --- |
| HEAP 大小 | LCF_HEAP_SIZE = 4k | 默认堆空间 |
| CSA 偏移 | DSPR_SIZE - 1k - CSA_SIZE | 把 CSA 放到每核 DSPR 高地址附近 |
| ISTACK 偏移 | CSA_OFFSET - 256 - ISTACK_SIZE | 给栈区之间预留间隙 |
| USTACK 偏移 | ISTACK_OFFSET - 256 - USTACK_SIZE | 用户栈位于中断栈下方 |
| HEAP 偏移 | USTACK_OFFSET - HEAP_SIZE | 堆位于用户栈下方 |

这一组公式体现了脚本的设计思路：每核 DSPR 的高地址区域留给运行时保留块，避免与普通 .data/.bss 混放。

### 5.4 复位入口、启动入口、陷阱表、中断表地址表

| 项目 | CPU0 | CPU1 | CPU2 | CPU3 | CPU4 | CPU5 |
| --- | --- | --- | --- | --- | --- | --- |
| 启动入口 cached | 0x80000000 | 0x80400000 | 0x80800000 | 0x80A00000 | 0x80E00000 | 0x81200000 |
| 启动入口 non-cached | 0xA0000000 | 0xA0400000 | 0xA0800000 | 0xA0A00000 | 0xA0E00000 | 0xA1200000 |
| Trap Vector | 0x80000100 | 0x80400100 | 0x80800100 | 0x80A00100 | 0x80E00100 | 0x81200100 |
| Interrupt Vector | 0x803FE000 | 0x807FE000 | 0x809FE000 | 0x80DFE000 | 0x811FE000 | 0x813FE000 |

补充说明：

| 项目 | 说明 |
| --- | --- |
| RESET = LCF_STARTPTR_NC_CPU0 | 复位后从 CPU0 的非缓存地址空间启动 |
| .start_tc0 > pfls0_nc | CPU0 的启动段被固定放到非缓存 PFlash 镜像区 |
| .start_tc1~5 > pflsX_nc | 其他 CPU 的启动入口也都固定放到各自非缓存 PFlash 区 |
| .traptab_tcX > pflsX | 各 CPU trap 表放在各自 PFlash 区 |
| .inttab_tcX | 每个中断向量槽位固定 0x20 字节间隔排列 |

### 5.5 MEMORY 区域汇总表

| 区域名 | 属性 | 起始地址 | 大小 | 典型用途 |
| --- | --- | --- | --- | --- |
| dsram0~5 | w!xp | 0x70000000/0x60000000/.../0x20000000 | 各 240 KB | 各 CPU 的数据 SRAM |
| dsram0_local~5_local | w!xp | 0xD0000000 | 各 240 KB | 各 CPU 本地视角地址 |
| psram0~5 | w!xp | 0x70100000/0x60100000/... | 各 64 KB | 各 CPU 程序 SRAM |
| psram_local | w!xp | 0xC0000000 | 64 KB | 本地程序 SRAM 视图 |
| pfls0 | rx!p | 0x80000000 | 4 MB | CPU0 主 PFlash |
| pfls1 | rx!p | 0x80400000 | 4 MB | CPU1 主 PFlash |
| pfls2 | rx!p | 0x80800000 | 2 MB | CPU2 主 PFlash |
| pfls3 | rx!p | 0x80A00000 | 4 MB | CPU3 主 PFlash |
| pfls4 | rx!p | 0x80E00000 | 4 MB | CPU4 主 PFlash |
| pfls5 | rx!p | 0x81200000 | 2 MB | CPU5 主 PFlash |
| pfls0_nc~pfls5_nc | rx!p | 0xA0000000 起各镜像地址 | 与各自 PFlash 相同 | PFlash 非缓存镜像 |
| ucb | rx!p | 0xAE400000 | 80 KB | 用户配置块 |
| cpu0_dlmu~cpu5_dlmu | w!xp | 0x90000000 起 | 各 512 KB | 各 CPU Data LMU |
| cpu0_dlmu_nc~cpu5_dlmu_nc | w!xp | 0xB0000000 起 | 各 512 KB | 各 CPU Data LMU 非缓存镜像 |
| lmuram | w!xp | 0x90400000 | 5 MB | 共享 LMU RAM |
| lmuram_nc | w!xp | 0xB0400000 | 5 MB | 共享 LMU RAM 非缓存镜像 |

### 5.6 REGION_MAP / REGION_MIRROR / REGION_ALIAS 作用表

| 机制 | 示例 | 作用 | 结果 |
| --- | --- | --- | --- |
| REGION_MAP | REGION_MAP(CPU0, dsram0_local -> dsram0) | 把本地地址视图映射到全局地址视图 | 同一物理内存可以被不同寻址方式访问 |
| REGION_MIRROR | REGION_MIRROR("pfls0", "pfls0_nc") | 建立 cached/non-cached 镜像关系 | 同一存储区拥有两组地址入口 |
| REGION_ALIAS | default_ram = dsram0, default_rom = pfls0 | 为通用节分配提供默认落点 | 当前工程默认把全局数据放 CPU0 DSPR，把代码常量放 CPU0 PFlash |

当前脚本启用的是：

| 别名 | 当前绑定 |
| --- | --- |
| default_ram | dsram0 |
| default_rom | pfls0 |

这意味着如果某些节没有指定按 CPU 拆分的专属放置规则，它们默认会落到 CPU0 的数据 RAM 和 PFlash 中。

### 5.7 固定地址节布局汇总表

| 节类别 | 位置 | 作用 | 特点 |
| --- | --- | --- | --- |
| .ustack | 各 CPU 的 DSPR 高地址保留区 | 用户栈 | 由 LCF_USTACKx_SIZE 与偏移公式决定 |
| .istack | 各 CPU 的 DSPR 高地址保留区 | 中断栈 | 和 .ustack 分开，避免中断污染主栈 |
| .csa | 各 CPU 的 DSPR 高地址保留区 | 上下文保存区 | TriCore 特有，不能随意删除 |
| .start_tc0~5 | 各 CPU non-cached PFlash 启动地址 | 启动代码入口 | 启动入口地址固定 |
| .traptab_tc0~5 | 各 CPU PFlash trap 地址 | 异常/陷阱向量 | 每核独立 |
| .inttab_tc0~5 | 各 CPU PFlash interrupt 地址 | 中断向量表 | 脚本显式为 256 个槽位逐项保留 |
| .interface_const | 0x80000020 | 接口常量区 | 启动初期可直接访问的固定常量 |

### 5.8 中断向量表的结构说明

脚本里中断向量表的写法看起来很长，本质上是重复模式：

| 模式 | 含义 |
| --- | --- |
| .inttab_tcX_000 (__INTTAB_CPUX + 0x0000) | 第 0 个中断槽 |
| .inttab_tcX_001 (__INTTAB_CPUX + 0x0020) | 第 1 个中断槽 |
| ... | 每个槽位递增 0x20 |
| .inttab_tcX_0FF (__INTTAB_CPUX + 0x1FE0) | 第 255 个中断槽 |

这说明每个 CPU 被预留了 256 个中断向量入口，每个入口固定 32 字节，链接器通过 KEEP(*(.intvec_tcX_N)) 保证相关节不会被垃圾回收删除。

### 5.9 小数据区与相对寻址区说明表

| 节/符号 | 落点 | 作用 |
| --- | --- | --- |
| .sdata / .sbss | default_ram | A0 小数据区，可用更短指令访问 |
| _SMALL_DATA_ / __A0_MEM | 由 .sdata 地址推导 | A0 基址符号 |
| .sdata2 | default_rom | A1 小常量区 |
| _SMALL_DATA2_ / __A1_MEM | 由 .sdata2 地址推导 | A1 基址符号 |
| .sdata4 / .sbss4 | lmuram | A9 小数据区 |
| _SMALL_DATA4_ / __A9_MEM | 由 .sdata4 地址推导 | A9 基址符号 |
| .sdata3 | default_rom | A8 小常量区 |
| _SMALL_DATA3_ / __A8_MEM | 由 .sdata3 地址推导 | A8 基址符号 |

这些区域的意义在于让编译器使用 TriCore 的相对寻址寄存器优化对小数据/小常量的访问。

### 5.10 各类数据节放置策略表

| 节类别 | 典型节名 | 放置位置 | 说明 |
| --- | --- | --- | --- |
| 已初始化数据 | .data | 各 CPU 的 dsramX 或 default_ram | 运行前由 ROM 拷贝到 RAM |
| 未初始化数据 | .bss | 各 CPU 的 dsramX 或 default_ram | 上电清零 |
| LMU 初始化数据 | .lmudata | cpuX_dlmu 或 lmuram | 用于更大或共享的数据区 |
| LMU 未初始化数据 | .lmubss | cpuX_dlmu 或 lmuram | 上电清零 |
| 常量数据 | .rodata | 各 CPU 的 pflsX 或 default_rom | 直接留在 Flash |
| 堆 | .heap | default_ram | 动态内存分配空间 |

### 5.11 clear table 的作用表

| 符号/机制 | 作用 |
| --- | --- |
| __clear_table | 记录需要在启动阶段清零的 .zbss/.bss/.lmubss 等节 |
| 启动代码读取 clear table | 按表执行内存清零 | 把链接脚本和启动代码解耦 |

这也是裸机工程常见做法：链接脚本不直接“执行清零”，而是提供一张描述表，启动代码统一遍历处理。

### 5.12 代码节放置策略表

| 节类别 | 放置区域 | 说明 |
| --- | --- | --- |
| CPU 专属代码节 | 各 CPU 对应的 pflsX | 可按核拆分代码落点 |
| 通用代码节 | default_rom | 如果没有特别区分，默认落到 CPU0 PFlash |
| 启动节 | 各 CPU 的 pflsX_nc | 启动阶段更倾向走非缓存镜像 |

## 六、把四个文件串起来看

### 6.1 端到端关系表

| 顺序 | 文件 | 向下游提供什么 |
| --- | --- | --- |
| 1 | cmake/tricore-gcc-toolchain.cmake | 编译器和 binutils 的具体路径 |
| 2 | CMakeLists.txt | 目标定义、编译选项、链接选项、后处理目标 |
| 3 | cmake/AurixProject.cmake | 参与编译的文件集合和 include 集合 |
| 4 | Lcf_Gcc_Tricore_Tc.lsl | 节到物理/逻辑内存区域的映射规则 |

### 6.2 一次完整构建的语义流程表

| 步骤 | 说明 |
| --- | --- |
| CMake 先加载工具链文件，确认 tricore-elf-gcc 等工具存在 |
| 顶层 CMakeLists 读取 ADS 排除规则并合并本地排除目录 |
| 辅助脚本递归扫描工程，把有效 .c/.h/include 目录收集起来 |
| 编译器按 tc4DAx 架构和所选优化等级编译每个源文件 |
| 链接器根据 Lcf_Gcc_Tricore_Tc.lsl 把启动代码、向量表、代码段、数据段放到指定地址 |
| objcopy 把生成的 ELF 转成 HEX，供下载工具使用 |

## 七、阅读和维护建议

### 7.1 修改 CMake 时优先检查的点

| 场景 | 优先检查项 |
| --- | --- |
| 新增源文件没有被编译 | 是否位于被排除目录下；扩展名是否为 .c；.cproject 是否排除了对应目录 |
| 头文件找不到 | 所在目录是否被扫描并加入 include dirs |
| 链接脚本不生效 | AURIX_LINKER_SCRIPT 是否被覆盖；target_link_options 是否正确传入 -T 路径 |
| 生成不了 hex | objcopy 工具路径是否正确； ELF 是否已成功生成 |

### 7.2 修改链接脚本时优先检查的点

| 场景 | 优先检查项 |
| --- | --- |
| 启动异常 | RESET、.start_tc0、trap 表、int 表地址是否仍匹配启动代码假设 |
| 栈溢出或上下文异常 | USTACK/ISTACK/CSA 大小是否足够 |
| 数据跑错核或访问异常 | default_ram/default_rom 是否还指向预期 CPU；专属节是否落在对应 dsramX/pflsX |
| 共享数据访问异常 | lmuram / dlmu 的 cached 与 non-cached 视图是否选对 |

## 八、结论

| 结论 | 说明 |
| --- | --- |
| 这套 CMake 方案的核心是“递归扫描 + 排除规则同步” | 复制工程、增删模块时维护成本较低 |
| 工具链脚本负责“让 CMake 知道怎么编” | 不关心源文件集合，只关心编译器和工具 |
| 顶层 CMakeLists 负责“让目标知道怎么连” | 这里把编译选项、链接选项、后处理目标统一起来 |
| 链接脚本负责“让镜像知道放哪里” | 它决定启动入口、向量表、RAM/Flash/LMU 的具体布局 |

如果后续要继续增强这个工程，最值得优先做的有两项：

1. 在 README 中追加一份 map 文件解读示例，把实际生成的 .map 和本链接脚本一一对应起来。
2. 进一步扩展 aurix_extract_cproject_excluded_dirs，让它支持从 .cproject 中提取多组 sourceEntries/excluding 规则并合并。
