# 第六轮（R6）MCAL 开发约定 —— HPM5E31 AUTOSAR MCAL

> 本轮所有成员开工前必读。Lead 维护，成员不要改本文件；有异议 send_message 给 lead。
> 建立时间：2026-11-02。分支：`autosar`（仓库 `hpm5e31_1`）。只由 Lead 提交 git。

## 0. 一句话目标

在 `autosar` 分支上，为 HPM5E31（hpm5e31_1 板）从零实现一套 **AUTOSAR CP MCAL 子集**（13 个模块），
每个模块有真实可编译的驱动、AUTOSAR 风格配置、可交互例程、**上板实测数据**，并写成书稿第 42~50 章，
最终合成 v1.3.0 PDF（带三级书签，不入 git），推 git、写 handover。

用户原话要点：**"实现 hpm5e31 的 mcal，有条件的需要上板测试，文档（也归到 pdf 里面），测试结果、对比要详尽"**。

## 1. 范围：模块、模块 ID、负责人、例程、章节

| 模块 | AUTOSAR 模块 ID | 参考 SWS（本地抽取文本，见 §10） | 负责人 | 例程（samples/mcal/…） | 章节 |
|---|---|---|---|---|---|
| Mcu | 101 | SWS_MCUDriver | mcal-core | mcu_clock_init、mcu_reset_mode | 44 |
| Port | 124 | SWS_PortDriver | mcal-core | port_pin_config | 44 |
| Dio | 120 | SWS_DIODriver | mcal-core | dio_led_button | 44 |
| Gpt | 100 | SWS_GPTDriver | mcal-timer | gpt_periodic、gpt_oneshot | 45 |
| Pwm | 121 | SWS_PWMDriver | mcal-timer | pwm_basic、pwm_notify | 45 |
| Icu | 122 | SWS_ICUDriver | mcal-timer | icu_capture | 45 |
| Wdg | 102 | SWS_WatchdogDriver | mcal-timer | wdg_trigger、wdg_timeout_reset | 45 |
| Adc | 123 | SWS_ADCDriver | mcal-io | adc_single、adc_bench | 46 |
| Spi | 083 | SWS_SPIHandlerDriver | mcal-io | spi_master_slave、spi_async_job | 46 |
| I2c | 113 | SWS_I2CDriver | mcal-io | i2c_master_slave、i2c_error | 46 |
| Can | 080 | SWS_CANDriver | mcal-can | can_classic_echo、can_fd_echo、can_irq_perf、can_busoff、can_filter | 47 |
| Mem（原 Fls） | 091 | SWS_MemoryDriver（R25-11 起 Fls 并入 Mem） | mcal-sys | mem_flash_rw、mem_bench | 48 |
| Crc | 201 | SWS_CRCLibrary | mcal-sys | crc_bench、crc_integrity | 48 |

补充：

- `mcal/common`（Std/Platform/Compiler/Det/Dem/MemMap/ModuleId）由 **Lead** 维护；`mcal/mcal.cmake` 也是 Lead 的。
- `samples/mcal/mcal_hello`、`samples/mcal/ecu_integration` 由 Lead 做（后者是 13 模块集成演示）。
- 章节 42（总览）、43（基础设施/配置/集成）由 Lead 写；49（合规性与商用 MCAL 对比）由 mcal-ref 写。
- 每章至少 2 张**脚本生成**的图（工具 `tools/figs/mcal_*.py` → `docs/figures/<章号目录>/*.png`）。
- 每个模块要覆盖 **SWS 里"必选 API"的主要部分**，允许用 `*_CFG_*` 开关裁剪可选 API；未实现的 API 在 README 和章节
  "待确认项"里写清"为什么没做 + 实现路径"，**不允许静默缺失**。

## 2. 硬件接线与资源分配（避免抢外设）

板卡接线（用户提供，已核实）：

| 资源 | 引脚/接口 | 用途与分配 |
|---|---|---|
| USR LED | PA02（高电平点亮，兼 BOOT_MODE0） | Port/Dio 例程输出 |
| USR BTN | PA03（按下为高，兼 BOOT_MODE1） | Port/Dio 例程输入；**注意复位时按住会进 ISP，不要按** |
| 8 颗 CAN 状态灯 | CAN0 PF26/PF18、CAN1 PF20/PF16、CAN2 PF22/PF21、CAN3 PF23/PF19（高亮） | Port/Dio 测试；**PF26 同时是 ADC0_IN01**，ADC 例程不要和 Dio 抢 |
| CAN0 / CAN1 | TX PD00/RX PD01 / TX PD05/RX PD04，两路 120 Ω 已连成对 | Can 例程（经典/FD 物理回环） |
| CAN2 / CAN3 | TX PC08/RX PA09 / TX PC15/RX PC14，两路 120 Ω 已连成对 | Can 例程备用/4 路并发 |
| SPI1 ↔ SPI3 | 杜邦线互连：nCS PC11↔PA27、MISO PC12↔PA28、MOSI PC13↔PA29、SCLK PC10↔PA26 | Spi 例程（SPI1 主 / SPI3 从）；也可作 Port/Dio 回环（GPIO 输出↔输入） |
| I2C2 ↔ I2C3 | SCL PD02↔PD07、SDA PD03↔PD06 | I2c 例程（I2C2 主 / I2C3 从） |
| LAN8651（10BASE-T1S） | SPI2：SCLK PA20、MISO PA22、MOSI PA23、CS PA21、RST PA19、INT PA24 | **本轮不碰**，避免与 10BASE-T1S 测试冲突 |
| YT8011AN（千兆 PHY） | RGMII PF02~PF13、MDC PA31/MDIO PA30、RST PB29/INT PB28 | **本轮不碰** |
| USR 串口 | CH343 /dev/ttyACM0 @921600 8N1 | letter-shell 交互、日志 |
| J-Link | HPM5E31xGNx @4 MHz | 烧写 |

MCAL 内部资源划分（**跨模块不许抢**）：

| 资源 | 划分 | 备注 |
|---|---|---|
| GPTMR0 | Gpt 例程（周期 1 ms tick / notification） | BSP 的 `board_delay_us` 用 **GPTMR3**，别动 GPTMR3 |
| GPTMR1 | Gpt 例程（oneshot/时间戳） | 与 GPTMR0 分开，便于同时验证 |
| GPTMR2 | Icu 例程（输入捕获） | 捕获源见下面"回环信号" |
| PWM0 | Pwm 例程（CH0~CH3） | 先查 `hpm5e31_1.yaml`/IOC 看可用引脚；**不要用 PWM1/2/3** 除非确认空闲 |
| ADC0 | Adc 例程（ADC16，通道 1=PF26） | 与 PF26 的 LED 复用冲突时，ADC 例程独占 PF26（Dio 例程用其它灯） |
| SPI1 / SPI3 | Spi 例程（主/从） | SPI2 留给 LAN8651，禁用 |
| I2C2 / I2C3 | I2c 例程（主/从） | I2C0/1 本轮不用 |
| MCAN0~3 | Can 例程 | 4 路全部归 Can 负责人 |
| EWDG0 | Wdg 例程 | - |
| 片内 Flash | Mem 例程：**必须用链接脚本之外的保留扇区**，并在 README 写明地址与验证方法（先跑 `riscv32-unknown-elf-size`/map 确认不覆盖 app） | Flash 写坏会砖，谨慎 |
| HPM CRC 外设 | Crc 例程（若可用）+ 软件查表对照 | 查 `hpm_crc_drv.c` 能力 |
| PA02/PA03、CAN 灯 | Port/Dio 例程 | 不与其他模块抢 |

**回环信号（Icu/Pwm 用）**：板上没有信号源，优先级如下——
1. 用 SPI1↔SPI3 或 I2C2↔I2C3 的杜邦线对做"GPIO 软件翻转 → GPTMR 捕获"（例如 PC11 输出方波、PA27 捕获），
   频率由 Gpt/软件定时决定，**已知频率**，可反推 ICU 测量误差；
2. 若某个 PWM 输出通道正好落在上述已互连引脚之一，则 PWM 输出 → 对端 GPTMR 捕获，测占空比/频率；
3. 两条都不通就如实写"已知限制（缺信号源）+ 复跑命令"，不要编数据。

## 3. 目录与文件布局（严格照做）

```text
mcal/
  common/            # Lead 维护：Std_Types.h / Platform_Types.h / Compiler.h / Mcal_Cfg.h /
                     # Det.[ch] / Dem.[ch] / MemMap.h / Mcal_ModuleId.h / Mcal_Version.h / Mcal.h
  mcal.cmake         # Lead 维护：例程 CMakeLists include 后调用 mcal_add(Mcu Port ...)
  <Mod>/include/<Mod>.h        # 公共 API + 类型 + API ID + 版本宏 + 配置类型
  <Mod>/src/<Mod>.c            # 驱动实现（寄存器级，尽量不调 HPM SDK 高层函数）
  <Mod>/cfg/<Mod>_Cfg.h        # 预编译配置（开关、通道数、DEV_ERROR_DETECT）
  <Mod>/cfg/<Mod>_PBcfg.h      # post-build 配置结构声明
  <Mod>/cfg/<Mod>_PBcfg.c      # post-build 配置实例（引脚/基地址/回调/时序参数）
```

要求：

1. 模块目录名首字母大写：`Mcu Port Dio Gpt Pwm Icu Adc Spi I2c Can Wdg Mem Crc`；
2. `<Mod>.h` 只 include `Std_Types.h`、`Mcal_Cfg.h`、`Mcal_ModuleId.h`（需要时），**不许 include HPM SDK 头**；
3. 驱动 `.c` 可以 include `hpm_<ip>_drv.h`/`hpm_<ip>_regs.h`/`soc.h`，但**外设实例/引脚/时钟参数必须来自 `*_PBcfg.c`**，
   驱动源码里不许出现 `HPM_MCAN0`、`PA02` 这类硬编码（宏定义在 PBcfg 里）；
4. 每个模块必须定义：`<MOD>_MODULE_ID`（用 `Mcal_ModuleId.h` 的值）、`<MOD>_SW_MAJOR/MINOR/PATCH_VERSION`、
   `<MOD>_AR_RELEASE_MAJOR/MINOR/REVISION_VERSION`、以及 SWS 里的 `<MOD>_API_ID_*` 常量；
5. 配置结构体命名：`<Mod>_ConfigType`（post-build），常量实例 `<Mod>_Config`（在 `<Mod>_PBcfg.c` 中，`const`）。

## 4. 编码规范（评审会按这个查）

- **命名**：完全按 AUTOSAR SWS（`Mcu_Init`、`Port_SetPinMode`、`Dio_ReadChannel`、`Spi_AsyncTransmit`、`Can_Write`…）。
  内部函数用 `static`，小写下划线。
- **返回值**：`Std_ReturnType` 只用 `E_OK/E_NOT_OK`；异步模块（Spi/Can/Mem/Adc 组转换）另配
  `<Mod>_StatusType`、`<Mod>_JobResultType` 等 SWS 枚举。
- **Dev Error Detect**：每个 API 入口按 SWS 检查初始化状态/参数/指针，错误时
  `MCAL_DET_REPORT(<MOD>_MODULE_ID, instance, <MOD>_API_ID_XXX, <MOD>_E_XXX)`，并返回 `E_NOT_OK`。
  宏定义在 `Det.h`，`MCAL_DEV_ERROR_DETECT=STD_OFF` 时零开销。
- **ISR**：C 回调命名 `<Mod>_Isr_<instance>`，ISR 里只做状态推进 + 置标志 + 调 notification；
  notification 由用户在 PBcfg 里配函数指针（如 `Gpt_Notification_0` 这种弱符号或 PBcfg 回调二选一，二选一后写进 README）。
- **禁止**：动态内存、递归、浮点（除 Crc/bench 输出换算可用软浮点）、阻塞死等（必须有超时/计数上限）、
  在驱动里 `printf`（调试可用 `MCAL_DET_PRINTF` 开关）。
- **可重入/原子**：多字节共享变量在 ISR/主循环之间访问要关中断或用 `__atomic`（RV32 上 32 位读写本身原子，但读改写要小心）。
- **编译器**：`-Wall -Wextra` 必须 0 warning（自己模块范围内）；用 `uint32_t` 还是 `uint32`？
  **MCAL 公共 API 用 AUTOSAR 的 `uint32` 等（来自 Platform_Types.h）**；驱动内部可用 SDK 的 `uint32_t`，但同一文件别混用导致歧义。
- **注释**：文件头写"参考 SWS 哪一节 + 对应 HPM RM 章节 + 例程"；关键寄存器写入处写位域名。

## 5. 配置模型（AUTOSAR 三种配置阶段都要演示到）

1. **Pre-compile**（`<Mod>_Cfg.h`）：模块开关、通道数、是否支持 DMA/中断、`<MOD>_DEV_ERROR_DETECT` 等；
2. **Link-time**：本工程统一用 post-build 指针，不单独做 link-time 配置（章节 43 说明）；
3. **Post-build**（`_PBcfg.c`）：`const <Mod>_ConfigType <Mod>_Config = { ... };`，含通道表、引脚、时钟、回调。

`<Mod>_PBcfg.c` 必须以**板级映射表**形式写，例如：

```c
/* 引脚/实例映射 —— 与 docs/MCAL开发约定.md §2 一致，改板只改这里 */
const Port_PinConfigType Port_PinConfig[] = {
    { PORT_PIN_USR_LED,  HPM_IOC, IOC_PAD_PA02, ... },
};
```

例程 `main.c` 只做：`bsp_init()` → `<Mod>_Init(&<Mod>_Config)` → 注册 shell 命令 → 主循环 `bsp_shell_poll()`。

## 6. 例程规范

### 6.1 CMakeLists.txt 模板（固定，只改 project 名与 mcal_add 的模块）

```cmake
cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_mcal_mcu_clock_init)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
mcal_add(Mcu Port Dio)
sdk_app_src(src/main.c)
```

### 6.2 main.c 规范

- `#include "bsp.h"` + 模块头文件（`#include "Mcu.h"` …）；
- `int main(void)`：`bsp_init();` → 模块 Init → `printf` banner + 关键配置（时钟/波特率/分频/周期）；
- 注册 shell 命令（`SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC), mcu, cmd_mcu, ...)`）；
- 主循环 `while (1) { bsp_shell_poll(); ... }`；
- **串口自证**：关键数据必须 printf，格式固定，便于 `--expect` 与文档引用；
- 命令命名统一小写前缀：`mcu`/`port`/`dio`/`gpt`/`pwm`/`icu`/`adc`/`spi`/`i2c`/`can`/`wdg`/`mem`/`crc`，
  子命令用空格分隔（如 `gpt start 0`、`can fd on`、`spi xfer 1024`）。

### 6.3 README.md 必填（照 `samples/adc/adc_basic_poll/README.md` 的格式）

1. 标题 + 一句话功能；
2. 硬件要求/接线表（引脚、对端、是否需要外部线）；
3. 编译/运行命令（含 `hwrun.sh` 一行）与命令表；
4. **实测现象**（贴串口输出片段）+ **日志路径**（真实 `logs/...`）；
5. 关键点（AUTOSAR API 用法、坑、与 SDK 直调的区别）；
6. 待确认项（若有）。

### 6.4 每个例程的验收（缺一不可）

- [ ] `bash scripts/build.sh samples/mcal/<name>` 编译通过，0 warning（自己模块范围）；
- [ ] `bash scripts/hwrun.sh mcal/<name> --secs N --send "..."` 上板跑通，日志存 `logs/`，README 引用真实路径；
- [ ] 至少一个**量化数据**（频率/周期/吞吐/延迟/CPU 占用/错误率/精度）；
- [ ] 至少一条**错误路径或边界**测试（非法参数 → Det 计数增加；总线错误/超时等）；
- [ ] 多次运行结果稳定（关键数据至少 2 次，取中位数/范围）。

## 7. 上板纪律（重要，违反会互相踩）

- 一律通过 `scripts/hwrun.sh` 烧写/抓串口（内部有整机锁 `hwlock.sh`，自动串行）；
  **禁止手动并行 flash、禁止直接调 flash.sh**（除非 Lead 同意）。
- 需要设备长时间在线（>90 s）或反复交互时，先占板卡租约：
  `nohup bash scripts/board_lease.sh 110 <名字> >/tmp/lease_<名字>.log 2>&1 &`，用完 `kill %1`；
  查看占用：`cat /tmp/hpm5e31_hw.lock.d/what`。
- 烧写失败（Cannot connect / JLink 报错）→ 等 10 s 重试 1 次；仍失败查孤儿 `JLinkExe` 并 message Lead，**不要反复猛刷**。
- 远程登录 shell 是 **zsh**：不要直接 `source scripts/env.sh`（zsh 下 `BASH_SOURCE` 为空，
  REPO_ROOT 会被算成 `$HOME`，导致 "No board named" 之类的怪错）。
  正确姿势：直接 `bash scripts/build.sh ...`、`bash scripts/hwrun.sh ...`（脚本内部自己 source）；
  需要环境变量时用 `bash -c 'source scripts/env.sh; ...'`。
- 如果 CMake 配置失败留下坏 cache，删掉对应 `build/<name>-debug` 再重试。
- 板子复位后串口会重连，`/dev/ttyACM0` 可能短暂消失；`hwrun` 已处理 0.3 s 延时，若失败重试。
- **不要碰**：LAN8651/SPI2、YT8011AN/RGMII、USB 设备模式、gs_usb 固件相关代码路径；本轮不测这些。
- 不许 `git` 操作（add/commit/push/checkout/rebase 都禁止），提交由 Lead 统一做。

## 8. 文档规范（每章都是"书稿"，不是 README 堆砌）

- 章节文件：`docs/chapters/44-mcal-core.md` 这种命名，**首行 `# 第 44 章 标题`**；
- 门槛（`tools/check_docs.py` 会查）：≥250 行、≥8 个 `##` 二级标题、≥3 张表、≥2 张图、≥1 段代码、
  必须含"文档信息 / 本章导览 / 概述 / 工作原理 / 寄存器 / 例程 / 实测 / 性能 / 坑 / 对比 / 参考来源 / 待确认项与风险"关键词；
- 文档信息表版本行写：`v1.3.0（2026-11-02，R6 MCAL）`；
- 图必须由脚本生成：脚本放 `tools/figs/mcal_<短名>.py`，输出到 `docs/figures/<章号>-<短名>/*.png`（300 dpi 白底），
  正文用 `![图 44-1 说明](figures/44-mcal-core/fig-44-1-xxx.png)` 引用；**图号连续、正文必须引用**；
- 表必须编号（`表 44-1`）并在正文引用；寄存器对照表至少 8 行（寄存器/位域/驱动 API/例程位置）；
- 引用格式：`SWS_Mcu §x.y`（附本地文本路径）、`RM §x.y (p.N)`、`DS §x.y (p.N)`、
  `hpm_sdk/drivers/src/xxx.c:NNN`、`samples/mcal/xxx/`、真实 `logs/...`、上游 URL；
- **对比要详尽**：与 HPM SDK 直调、与 TC4x MCAL、与 NXP S32K3 RTD 同类模块对比（API 完成度、配置方式、
  资源开销、性能、坑），数据来源写清；
- `## 待确认项与风险`：只允许"已实测 / 已定位 / 已知限制（原因 + 一条复跑命令）"三态，**禁止裸"待确认"**；
- 去 AI 味：见 `docs/写作规范.md` §4 黑名单词，多用短句、数字、工程口吻；
- 参考来源必须给上游链接（AUTOSAR、HPM、Infineon、NXP、GitHub），本地路径只作补充。

## 9. 归属、汇报与协作

### 9.1 写作用域（**只写自己目录，越界前先 send_message 给 Lead**）

| 成员 | 可写路径 |
|---|---|
| lead | `mcal/common/**`、`mcal/mcal.cmake`、`mcal/README.md`、`samples/mcal/mcal_hello/**`、`samples/mcal/ecu_integration/**`、`docs/chapters/42-*`、`43-*`、`docs/00-目录.md`、`docs/VERSION`、`tools/gen_toc.py`、`tools/build_pdf.sh` 相关、`README.md`、git |
| mcal-core | `mcal/Mcu/**`、`mcal/Port/**`、`mcal/Dio/**`、`samples/mcal/mcu_*`、`port_*`、`dio_*`、`docs/chapters/44-*`、`docs/figures/44-*`、`tools/figs/mcal_core_*.py` |
| mcal-timer | `mcal/Gpt/**`、`mcal/Pwm/**`、`mcal/Icu/**`、`mcal/Wdg/**`、`samples/mcal/gpt_*`、`pwm_*`、`icu_*`、`wdg_*`、`docs/chapters/45-*`、`docs/figures/45-*`、`tools/figs/mcal_timer_*.py` |
| mcal-io | `mcal/Adc/**`、`mcal/Spi/**`、`mcal/I2c/**`、`samples/mcal/adc_*`、`spi_*`、`i2c_*`、`docs/chapters/46-*`、`docs/figures/46-*`、`tools/figs/mcal_io_*.py` |
| mcal-can | `mcal/Can/**`、`samples/mcal/can_*`、`docs/chapters/47-*`、`docs/figures/47-*`、`tools/figs/mcal_can_*.py` |
| mcal-sys | `mcal/Mem/**`、`mcal/Crc/**`、`samples/mcal/mem_*`、`crc_*`、`docs/chapters/48-*`、`docs/figures/48-*`、`tools/figs/mcal_sys_*.py` |
| mcal-ref | `docs/chapters/49-*`、`docs/figures/50-*`、`tools/figs/mcal_ref_*.py`、`docs/mcal/**`（合规性矩阵等支撑材料） |

### 9.2 汇报格式（send_message 给 lead）

- **开工 5 分钟内**：一句话确认理解范围 + 计划（不用长）；
- **每完成一个模块/例程**：`模块: Mcu; 文件: ...; 编译: OK; 上板: PASS (日志 logs/xxx.log); 关键数据: ...; 阻塞: 无`；
- **完成全部**：`DONE: <成员>; 模块 ...; 例程 N 个全部 PASS; 章节 xx 行/图 M 张; 遗留/风险: ...`；
- 遇到需要 Lead 决策（API 取舍、改公共头、资源冲突）立即问，**不要自己改公共文件**。

### 9.3 板卡冲突处理

- 看到 `hwrun` 等锁超过 3 min，先 `cat /tmp/hpm5e31_hw.lock.d/what` 看谁；是同伴就等，不要杀锁；
- 两个成员都要长时间占用时，用一两个成员的例程合并测（Lead 协调），不要各占 90 s 反复排队。

## 10. 参考资源（全部在本机/远程，可直接读）

| 资源 | 位置 |
|---|---|
| AUTOSAR CP SWS 抽取文本（31 个，**优先读这个**） | 远程 `autosar_txt/AUTOSAR_CP_SWS_*.txt`；PDF 在 `$HOME/tmp2/autosar_pdf/` |
| AUTOSAR 官方文档全套（R25-11） | Windows `E:\dsh\autosar\CP\`（127 个 PDF）；可用 pdftotext（远程）抽取 |
| AUTOSAR 模块 ID 表 | SWS_BSWGeneral §11.3（本地文本约 4710 行起）；已抄进 `mcal/common/Mcal_ModuleId.h` |
| Infineon TC4x MCAL 2.20.0（含代码+文档+需求追溯） | Windows `C:\AURIX_TC4x_MC-ISAR_2.20.0\`（`MCAL/` 下 30 个模块，`Docs/User_Manuals`、`Docs/*_Req_Extract_Autosar_R20-11_R21-11.pdf`） |
| NXP S32K3 RTD/MCAL 6.0.0 | Windows `D:\NXP\SW32K3_S32M27x_RTD_R21-11_6.0.0\eclipse\plugins\<Mod>_TS_*\`（每个模块含 include/src/doc） |
| NXP Integration Reference Examples | `D:\NXP\Integration_Reference_Examples_S32K3XX_2023_04\` |
| HPM SDK v1.13.0 | 远程 `$HOME/sdk_env_v1.13.0/hpm_sdk/`（`drivers/src/hpm_*_drv.c`、`soc/HPM5E31/`、`boards/`） |
| 本仓库现有例程（外设寄存器级实测、坑） | `samples/gpio gptmr pwm adc spi i2c can flash power ...`、书稿 `docs/chapters/04~22` |
| 板级支持包 | `boards/hpm5e31_1/`（`board.c`/`clock.c`/`pinmux.c`/`bsp/shell*.c`）、`hpm5e31_1.yaml` |
| 原理图 | 远程 `hardware/hpm5e31_1_refsch.pdf` |
| 芯片手册/勘误 | 远程 `先楫官方 Doc 包（DS/RM/Errata）`、`先楫官方 Doc 包（DS/RM/Errata）`（Windows 有 PDF） |
| 上游链接模板 | AUTOSAR: <https://www.autosar.org/>；HPM SDK: <https://github.com/hpmicro/hpm_sdk>；先楫: <https://www.hpmicro.com/>；Infineon ILLD/MCAL: <https://github.com/Infineon/illd_release_tc4x>；NXP RTD: <https://github.com/nxp-auto-linux>（RTD 需 NXP 官网账号，写官网链接） |

## 11. 里程碑（Lead 按此推进，不强制精确）

| 时间 | 目标 |
|---|---|
| T+0~45 min | 各模块头文件 + 核心 API + 第一个例程跑通（上板） |
| T+45~120 min | 全部例程 PASS、日志落盘、README 完 |
| T+120~180 min | 章节初稿（含图）、Lead 汇总检查 |
| T+180~240 min | 集成 demo、PDF v1.3.0、commit/push、handover |

## 12. 自检清单（提交前每个成员自查）

- [ ] 我的所有文件都在 §9.3 写作用域内；
- [ ] `mcal/<Mod>/include/<Mod>.h` 未 include 任何 HPM SDK 头；
- [ ] 驱动零硬编码板级引脚/实例（都在 PBcfg）；
- [ ] 每个 API 有 Dev Error 检查（至少参数/未初始化）；
- [ ] 例程编译 0 warning、上板 PASS、README 有真实日志；
- [ ] 章节通过 `python3 tools/check_docs.py --file docs/chapters/xx.md`；
- [ ] 图由脚本生成、正文引用、无孤儿图；
- [ ] 未做 git 操作；
- [ ] 汇报了关键数据 + 遗留风险。
