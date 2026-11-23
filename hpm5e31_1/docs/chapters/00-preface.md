# 第 0 章 前言、阅读指南与版本

## 0.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.6.0（2026-11-23，公开发布版） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 对应例程 | 全书 `samples/` |
| 实测日期 | 2026-10-30 起 |
| 可信度 | 高（本书所有实测数据均来自本仓库 `logs/` 中的串口日志） |

## 0.2 本章导览

- 这本书是什么、不是什么；
- 硬件与软件基线（版本号精确到可复现）；
- 四种阅读路径（入门 / 移植 / 查错 / 应用层选型）；
- 全书约定：可信度标签、引用格式、实测方法、日志编号；
- 仓库地图与版本记录；
- 已知局限与"哪些结论还不能当定论"。

## 0.3 为什么写这本书

HPM5E00 系列是先楫半导体面向**实时以太网 + 运动控制**的 RISC-V MCU：单核 RV32-IMAFDCBP
480 MHz、片内 1 MB Flash、544 KB SRAM、4 路 CAN FD、千兆以太网 MAC、3 端口 EtherCAT ESC、
16 通道 HRPWM、2×ADC16（2 MSPS）。这些外设在官方 SDK v1.13.0 里都有驱动和例程，
但**官方例程面向 EVK**，而实际项目往往是从一块最小系统板开始：

- 引脚是自己排的，IOC/GPIOM/引脚复用要重新对；
- 时钟、Flash 启动头、链接脚本要和板子匹配；
- 外设"能出数"和"性能达标"之间还有大量工程细节（DMA、cache、中断优先级、采样点、PLCA 参数……）；
- 官方文档是"寄存器视角"，缺"从零到跑通再到量化"的完整路径。

本书以一块真实的 **hpm5e31_1** 板为靶子，把每个外设从**原理 → 寄存器/API → 例程 → 上板实测 → 性能数据 → 坑**
走一遍，并把可复现的例程、脚本、日志一起放进仓库。目标是三句话：

1. **能照着做完**：复制命令就能编译、烧写、看到输出；
2. **能量化对比**：每个外设都有实测表/图（频率、吞吐、延迟、误差、丢包）；
3. **能查错**：每章都有"现象 → 原因 → 解决"的坑清单，以及寄存器速查与勘误。

第二阶段（第 28~40 章）把视角从"外设能不能动"提升到"**能不能和真实的车载/机器人软件栈对话**"：
UDS 诊断、J1939 商用车网络、CANopen 子系统、XCP 标定、SOME/IP 面向服务、XRCE-DDS/ROS 2、
IEEE 1588 时间同步、DLT/VSS 日志与信号，以及让上位机看到真实 CAN 总线的 cannelloni 网关。
每个方向都配 2~4 个可上板例程、Python/C++/Rust 上位机工具，以及统一口径的延迟/吞吐/资源开销实测。

第三阶段（第 42~50 章）把这套板子推到"能按 AUTOSAR 的方式被上层使用"：从零实现 15 个 AUTOSAR CP MCAL 模块
（含第 7 轮补上的 **Eth / EthTrcv**），照 SWS 写 API、按 AUTOSAR 配置模型组织、寄存器级实现，并用 lwIP 与 FreeRTOS
验证以太网数据通路。第 7 轮还补上了三个此前只有"对比"没有"实现"的方向：**DoIP**（ISO 13400-2）、
**XCP on UDP**（XCPonEth）、**MQTT**（车云遥测），并把两个长期悬着的问题真正修掉：
**I2C 400k 读**（目标侧 T_SUDAT 建站时间不足）与 **CAN FD 5 Mbit/s 数据段**（TDC 偏移越界）。

## 0.4 硬件与软件基线

本书所有实测数据的软硬件基线见表 0-1。

| 项 | 版本/型号 | 说明 |
|---|---|---|
| MCU | HPM5E31IPB1 | RV32-IMAFDCBP，480 MHz，片内 1 MB Flash，544 KB SRAM，148 GPIO |
| 板卡 | hpm5e31_1 最小系统板 | 24 MHz 无源晶振；4×CAN FD；LAN8651 10BASE-T1S；YT8011AN 千兆 PHY；USB0 |
| 调试器 | SEGGER J-Link（J-Link OB-RA4M2-Full，固件 2026-06-25，S/N 1120000012） | JTAG，4 MHz |
| 串口 | CH343 USB-UART（`1a86:55d3`）→ `/dev/ttyACM0` | 921600 8N1 |
| 主机 | Ubuntu 26.04 LTS，24 核，61 GB RAM | 远程 `<user>@<host>` |
| SDK | HPM SDK v1.13.0（`sdk_env_v1.13.0`） | 官方原件，未修改 |
| 工具链 | riscv32-unknown-elf-newlib-multilib，GCC 11.1.0 | 官方 Linux 工具链 |
| 构建 | CMake 4.2.3 + Ninja 1.13.2 | `-DHPM_BUILD_TYPE=flash_xip` |
| 命令行 | letter-shell 3.2.4（移植版） | 见第 03 章 |
| 协议栈 | lwIP（SDK 自带）、cherryusb（SDK 自带） | 见第 13、20 章 |

> 版本敏感：SDK/工具链升级后建议重跑 `tools/build_all.sh` 与本书第 23 章基准，
> 数据会随 GCC 版本与优化选项变化（第 23 章会标注本次数据对应的 GCC 版本）。

本书的例程、脚本、上位机工具与书稿源文件都在同一个仓库里：
<https://github.com/weifengdq/embedded>（`hpm5e31_1/` 子目录）。
书中的路径一律写成仓库相对路径（如 `samples/spi/spi1_spi3_master_slave/`），
拿到仓库后直接 `cd` 进去就能对上。

> **路径与主机约定**：文中 `<host>` 指作者的 Ubuntu 测试主机，`<proxy>` 指可选的外网 HTTP 代理，
> `$HOME` 指 SDK/工具链的安装根目录（第 2 章给出搭建步骤）；`ref/`、`ref2/` 是写作时在开发机上
> 收集的上游资料快照，**不随仓库分发**，对应上游链接见表 27-10、表 27-11。

## 0.5 四条阅读路径

四条阅读路径的适用人群见表 0-2。

- **入门（第 0→1→2→3 章）**：先建立"板子怎么跑起来"的整体认知，再挑外设章节。
- **移植（按外设跳读）**：直接看目标外设那章的"寄存器/API 对照表 + 例程 + 坑"，
  把 `samples/<外设>/` 复制到自己的板级目录，改 `boards/<你的板>/` 里的引脚与时钟。
- **查错（第 26 章 + 各章坑清单）**：先查寄存器速查/勘误表，再按现象定位到章节；
  引脚、接线、测量方法与全部日志索引在第 51 章附录。
- **应用层选型（第 29→41 章）**：先看第 40 章的分层图与选型决策树，再按需跳到具体协议章（诊断看第 30 章 UDS、第 31 章 DoIP；标定看第 34 章 XCP，含 XCP on UDP；车云看第 37 章 MQTT）；
  要接上位机时先读第 28 章（cannelloni 网关/vcan0 测试台）与 `docs/应用层开发约定.md`；
  想把整块板做成"USB 转 4 路 CAN FD 分析仪"，直接看第 41 章（gs_usb / candleLight）；
  想按 AUTOSAR 方式集成，从第 42 章 MCAL 总览进，以太网看第 49 章（Eth/EthTrcv + lwIP/FreeRTOS）。

![图 0-1 本书结构、五部分与四条阅读路径](figures/00/fig-00-1-book-map.png)

**图 0-1** 把全书 52 章按"基础 → 外设 → 系统与性能 → 应用 → MCAL → 附录"排成六部分，并标出四条阅读路径。
第一次读建议顺着第一、二部分走；带着问题来的话，直接跳到对应部分即可，附录（第 51 章）提供引脚、
接线、日志与命令的索引。


## 0.6 全书约定

全书使用统一的引用格式，示例见表 0-3。

### 0.6.1 可信度标签

| 标签 | 含义 |
|---|---|
| 【实测】 | 本仓库 `logs/` 有对应串口日志，数据可复现 |
| 【官方】 | 来自 HPM 官方 DS/RM/Errata/SDK 文档，标注章节与页码 |
| 【推导】 | 由官方数据或实测推导，给出推导过程 |
| 【待确认】 | 尚未在本板验证，给出验证方法；集中在各章"待确认项与风险" |

### 0.6.2 引用格式

`DS §x.y (p.N)`（数据手册）、`RM §x.y (p.N)`（用户手册）、`Errata E000xx`、
`hpm_sdk/路径:行`（SDK 源码）、`samples/...`（本书例程）、`logs/...`（实测日志）。
页码一律为 **PDF 阅读器页码**。

### 0.6.3 实测方法

1. 每个例程用 `scripts/hwrun.sh` 一条龙执行：编译 → J-Link 烧写 → 串口抓取 → 日志落盘；
2. 串口日志文件名 `logs/YYYYMMDD-HHMMSS-<例程名>.log`，书中以该路径引用；
3. 时间类测量优先用 `mcycle`（CPU 周期计数）或 `MCHTMR`（24 MHz），并给出换算公式；
4. 吞吐类测量用"对端计数/时长"，并记录包长、帧率、重传；
5. 所有"性能"数据注明：编译选项、代码位置（ILM/XIP）、cache 状态、室温。

### 0.6.4 不可复现数据的处理

测不到、或受环境限制（例如没有示波器、没有第二块板）的项，一律标【待确认】并写清
"用什么工具、怎么测、预期量级"，不写猜测值。

## 0.7 仓库地图

仓库目录说明见表 0-4。

| 目录 | 内容 |
|---|---|
| `boards/hpm5e31_1/` | 板级支持包：`board.c`（时钟/控制台/Flash 头）、`clock.c`（PLL/时钟组）、`pinmux*.c`（引脚复用）、`bsp/`（letter-shell 移植 + 通用命令） |
| `samples/` | 按外设分类的例程，每个含 `CMakeLists.txt`、`src/`、`README.md` |
| `samples/app/` | 应用层协议例程（第 28~41 章）：网关、ISO-TP/UDS、J1939、CANopen、XCP、SOME/IP、XRCE、PTP、日志与信号、gs_usb 适配器 |
| `components/` | 可复用组件：`app_can`/`app_lwip`/`app_can_gw` 底座 + `app_isotp`/`app_uds`/`app_j1939`/`canopen_node`/`app_xcp`/`app_someip`/`app_xrce`/`app_ptp`/`app_dlt` 协议栈 |
| `host/` | 上位机工具（Python/C++/Rust）：can_bridge、uds、j1939、canopen、xcp、someip、xrce、ptp、canlog、vss、dlt、rust |
| `docs/` | 本书 Markdown 源、图（`docs/figures/`）、自动生成表格（`docs/generated/`）、写作规范 |
| `tools/` | 文档工具链（校验/合成 PDF）、绘图脚本（graphviz/matplotlib）、寄存器表生成 |
| `scripts/` | `env.sh`、`build.sh`、`flash.sh`、`serial_cap.py`、`hwrun.sh`（一条龙 + PID 自愈锁 + `--post`）、`hwlock.sh`、`new_sample.sh` |
| `logs/` | 全部实测串口日志（证据） |
| `hardware/` | 硬件资料：原理图、丝印图（PDF）与 BOM（xlsx） |
| `README.assets/` | 板卡实物照片（正视图 / 斜视图 / 资源标注图） |

## 0.8 版本记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.6.0 | 2026-11-23 | **公开发布版**：全书脱敏（内网主机/路径/仓库地址）、插图压缩（40 MB → 9.2 MB）、日志按书中引用精选（1509 → 474 条）、补硬件资料（原理图/丝印/BOM/板卡照片）、补先楫官方资料与社群入口（第 27 章表 27-12）、PDF 重新合成 |
| v0.1 | 2026-10-30 | 初版：板级移植、letter-shell、GPIO/UART/I2C/SPI/CAN/LAN8651/YT8011/PWM/GPTMR/QEI/ADC/DMA/USB/Flash/基准等章节与例程 |
| v0.2.0 | 2026-10-30 | 29 章外设/基准篇补齐，53→56 例程，全量上板冒烟 |
| v0.6.0 | 2026-10-30 | 第二轮：PDF 三级书签固化、LAN8651 硬件 SPI2 四层根因修复（9.08 Mbit/s）、lwIP 上行优化（466 Mbit/s）、I2C/SPI 双机主从 |
| v1.0.0 | 2026-10-30 | 第三轮（应用层）：第 28~40 章 + 21 个应用层例程 + Python/C++/Rust 上位机 + cannelloni 网关 + 十类协议实测汇总 |
| v1.1.0 | 2026-11-02 | 第四轮：gs_usb / candleLight 适配器（第 41 章），SOME/IP-TP、vsomeip、canplayer 回放、DLT、PTP 60 s 窗口等收口；PDF 499 页/969 条书签 |
| v1.3.0 | 2026-11-02 | 第六轮：AUTOSAR CP MCAL 子集——13 个模块（Mcu/Port/Dio/Gpt/Pwm/Icu/Wdg/Adc/Spi/I2c/Can/Mem/Crc）、28 个例程、SWS API 追溯矩阵、合规性与商用 MCAL 对比（第 42~50 章）；PDF 684 页 / 1222 条三级书签 |
| v1.4.0 | 2026-11-02 | 第七轮：MCAL 补 **Eth/EthTrcv**（15 个模块）并用 **lwIP + FreeRTOS** 上板验证；应用篇补 **DoIP（第 31 章）/ MQTT（第 37 章）/ XCP on UDP（第 34 章）**；修通 **I2C 400k 读** 与 **CAN FD 5 Mbit/s 数据段**；章节重排为 52 章 |
| v1.2.0 | 2026-11-02 | 第五轮：全书待确认项逐条复核（已实测/已定位/已知限制三态）；附录移到末尾并改为第 51 章，全书按五部分重组；统一源码仓库与上游资料链接；新增图 20 余张；SPI1↔SPI3、I2C2↔I2C3 双控制器互测，4 路 CAN 并发满载与 5 Mbit/s 数据段，ISO-TP 大帧、UDS 64 KB 刷写、gs_usb 错误帧/5 Mbit/s、栈水位测量等上板闭环 |

## 0.9 五分钟上手

下面这段命令可以让你在五分钟内看到第一个例程的输出（详细步骤见第 02 章）：

```bash
git clone https://github.com/weifengdq/embedded && cd embedded/hpm5e31_1
source scripts/env.sh                        # 先按第 2 章装好 HPM SDK 与 RISC-V GCC
bash scripts/hwrun.sh shell/letter_shell_basic --secs 5 --send "ver;clk"
```

## 0.10 已知局限

局限与验证状态汇总见表 0-7。

1. **单板**：CAN 用 MCU 内部两节点（CAN0↔CAN1、CAN2↔CAN3）验证，没有第三方 CAN 分析仪；
2. **无示波器/逻辑分析仪**：时序类结论以"寄存器计数 + 串口自证"为主，波形类图为示意图；
3. **10BASE-T1S 与千兆以太网**的对端分别是 USB 适配器与转换盒，性能受对端/USB 限制；
4. HPM5E00 的 DS/RM/Errata 本地方案为 Rev0.4/Rev0.4/V0.1，官网可能更新；
5. 个别外设（如 OWR 1-Wire、EUI、SDM 外部滤波）缺少外部器件，只能做内部回环/位流观测；
6. **应用层协议的"对端"是主机工具或本板另一节点**，没有真实车规 ECU/域控制器参与，
   跨厂商互操作的结论以第 28~39 章各章"待确认项与风险"为准；
7. cannelloni UDP 网关存在 0.1~0.2% 固有丢包，对"无重传"协议（如 CANopen 分段 SDO）会超时，
   这是介质特性而非协议实现缺陷（第 28/32 章有量化数据）；
8. **长稳/环境类**：24 h 连续运行、高低温箱、EMC/ESD 需要专用设备与时间窗，本书只做到分钟级
   长稳（第 25 章）与常温实测，相关结论以"已知限制 + 复测方法"给出；
9. **外部器件类**：QEI 飞线、SDM 外部 ΣΔ 调制器、PPS 输出、电流表/示波器测量等项目，
   本板条件不具备，统一降级为"已知限制"，不再作为本书结论；
10. **待确认项的处理**：第五轮已把各章"待确认项与风险"逐条复核——能测的补了实测数据与日志，
    能定位的写清根因，确实测不了的改成"已知限制（原因 + 一条命令级验证方法）"。读者若发现
    仍标"待确认"的单元格，先看该章表格的"已逐条复核"说明与验证命令。

## 0.11 参考来源

1. HPM5E00 数据手册 Rev0.4（HPM5E00DSV04.pdf）
2. HPM5E00 用户手册 Rev0.4（HPM5E00UMV04.pdf）
3. HPM5E00 勘误 V0.1
4. HPM SDK v1.13.0（`hpm_sdk/`）与在线文档 <https://hpm-sdk.readthedocs.io/zh-cn/latest/>
5. 先楫官网 <https://www.hpmicro.com/>
6. letter-shell 3.2.4 <https://github.com/NevermindZZT/letter-shell>
7. lwIP <https://savannah.nongnu.org/projects/lwip/>、cherryusb <https://github.com/cherry-embedded/CherryUSB>
8. 应用层参考实现：CANopenNode、XCPlite、Micro-XRCE-DDS、vsomeip、cannelloni、dlt-daemon、
   COVESA VSS、can-utils、linuxptp、pyxcp、udsoncan、python-canopen（来源与版本见各章"参考来源"）
9. 本书源码仓库 <https://github.com/weifengdq/embedded>（`hpm5e31_1/` 子目录）；先楫官方资料与社群入口见第 27 章表 27-12。

## 0.12 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 项 | 状态 | 说明/验证方法 |
|---|---|---|
| 官网文档版本 | 已知限制 | 写作基于本地 Rev0.4；关键设计请以官网最新版为准 |
| 对外设性能的长期稳定性 | 已知限制 | 需 24 h 长稳测试（第 25 章给出方法） |
| 部分外设缺外部器件 | 已知限制 | 见第 7 节局限，相关章节标【待确认】 |
| 应用层协议与真实车规 ECU 的互操作 | 已知限制 | 当前只有主机工具/板内双节点对测；需实车或台架 ECU（见第 40 章 U-39-4） |
| 十类协议同时运行的资源与实时性 | 已知限制 | 需合并固件后测 DLM 占用与任务抖动（第 40 章 U-39-1） |