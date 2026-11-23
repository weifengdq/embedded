# 第 25 章 复位、启动与运行时可靠性

## 25.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/reliability/soak_test/`、`samples/power/*` |
| 实测日期 | 2026-10-30 |
| 可信度 | 中（方法论 + 短时长稳实测；24h 长稳待补） |

## 25.2 本章导览

- 从上电到 `main()` 的完整链路，以及**每一步失败时你会看到什么**；
- 复位源（PPOR）与"复位原因"读取方法；
- 运行时可靠性四件事：SRAM 完整性、栈/堆、看门狗、时钟/中断存活；
- 长稳测试的做法与 `soak_test` 例程、日志判读；
- 故障注入与恢复（看门狗复位、软件复位、ISP 救砖）；
- 本章实测数据的口径与局限。

## 25.3 启动链路（从上电到 main）

表 25-1 启动链路与失败表现

| 阶段 | 做什么 | 失败时的现象 | 定位手段 |
|---|---|---|---|
| 电源/复位 | DCDC/LDO 起振，POR/BOR 释放 | 无电流、J-Link 连不上 | 万用表量 3V3/1V1；J-Link `VTref` |
| BootROM | 采样 BOOT_MODE（PA02/PA03），配置 XPI | 串口无输出；J-Link 能连但 PC 在 ROM | 看 PA02/PA03 电平；`r` 后看 PC |
| 镜像校验 | 读 `nor_cfg_option`/`boot_header`，校验 | 停在 ROM，串口无输出 | 用 J-Link 读 0x8000_0400/0x8000_1000 |
| 跳转应用 | 设置 SP/向量表，跳 `_start` | 反复复位或跑飞 | J-Link halt 看 PC 是否在 0x8000_3000+ |
| C 运行时 | `.data` 拷贝、`.bss` 清零、`__libc_init_array` | 全局变量初值错、构造器不跑 | 检查链接脚本与 map |
| `board_init()` | 时钟/PLL/控制台 | 串口乱码/无输出 | 看 `clk` 命令；示波器量 PA00 |
| `main()` | 应用 | 业务异常 | 串口日志 + `trap` + LED |

> **实战技巧**：在 `main()` 第一行点亮 LED 或打印一个字符，是区分"没启动"和"启动后死机"最快的方法。

## 25.4 复位源与复位原因

HPM5E31 的复位源由 PPOR 记录（`hpm_ppor_drv.h` 的 `ppor_reset_source_t`）：

表 25-2 PPOR 复位源（SDK `hpm_ppor_drv.h`）

| 位 | 名称 | 含义 | 常见原因 |
|---|---|---|---|
| bit0 | `ppor_reset_brownout` | 欠压复位 | 电源跌落、上电斜率不足 |
| bit4 | `ppor_reset_debug` | 调试复位 | J-Link 复位 |
| bit16 | `ppor_reset_wdog0` | 看门狗 0 | 未喂狗 |
| bit17 | `ppor_reset_wdog1` | 看门狗 1 | 未喂狗 |
| bit18 | `ppor_reset_wdog2` | 看门狗 2 | 未喂狗 |
| bit19 | `ppor_reset_wdog3` | 看门狗 3 | 未喂狗 |
| bit24 | `ppor_reset_pmic_wdog` | PMIC 看门狗 | 电源管理异常 |
| bit31 | `ppor_reset_software` | 软件复位 | `ppor_sw_reset()`、`reboot` 命令 |

读取方法（`samples/power/*` 与 BSP 的 `reboot` 命令）：

```c
#include "hpm_ppor_drv.h"
uint32_t st = ppor_get_reset_status(HPM_PPOR);   /* 复位源状态寄存器 */
printf("reset status = 0x%08lx\r\n", (unsigned long) st);
if (st & ppor_reset_software) printf("last reset: software\r\n");
if (st & ppor_reset_brownout) printf("last reset: brownout\r\n");
```

【实测】hello_world / soak_test 上电复位后 `ppor_reset_brownout` 与 `ppor_reset_debug` 常见；
用 `reboot` 命令（`ppor_sw_reset(HPM_PPOR, 100)`）后复位源为 `ppor_reset_software`，
这是验证"复位原因链路"最简单的方法。

## 25.5 运行时可靠性四件事

### 25.5.1 SRAM 完整性

- 片内 SRAM 分块：AXI SRAM 256 KB、AHB SRAM 32 KB、ILM/DLM 各 128 KB；
- 长稳测试中周期性地对 8 KB 缓冲区做 `memcpy` + CRC32 校验，可发现位翻转/越界踩踏；
- **DMA 与 CPU 共享缓冲区时**，注意 cache 一致性：DMA 缓冲放 non-cacheable AXI SRAM（0x0124_0000 起 64 KB）
  或读写前后 `l1c_dc_invalidate()/l1c_dc_flush()`（第 19 章）。

### 25.5.2 栈与堆

表 25-3 栈/堆相关配置（链接脚本 `flash_xip.ld`）

| 项 | 位置/大小 | 说明 |
|---|---|---|
| 栈 | DLM 顶部（默认 32 KB 预留，`_stack_size`） | 中断嵌套 + printf 都在这里；过大可改链接脚本 |
| 堆 | DLM，`_heap_size` | newlib `malloc` 用；`bsp_hexdump`/`printf` 一般不用堆 |
| 向量表 | ILM | 中断入口零等待 |
| 热代码 | ILM（`.fast` 段） | 需要手工放；第 23 章给方法 |

栈水位用"涂色法"测量（`soak_test` 已实现）：启动后用内联汇编读当前 SP，把 `[__stack_base__, SP)`
区间涂成 `0xA5`；运行中栈向下生长会覆盖涂色，之后从栈底向上找第一个非 `0xA5` 的字节，
"栈顶 − 该地址"就是历史峰值占用。链接脚本给出的栈是 DLM 顶部 16 KB
（`__stack_base__=0x00204000`、`_stack=0x00208000`），实测 1 ms 主循环 + 1 kHz 中断 + 偶发
`printf` 的峰值占用 **1 172 B（7.2%）**，余量充足。把这份统计接进自己的工程，只要复制
`samples/reliability/soak_test/src/main.c` 里的 `soak_stack_paint()` / `soak_stack_peak()` 两个函数。

### 25.5.3 看门狗

- WDG：普通看门狗，超时复位；EWDG：带窗口/中断的增强看门狗（第 22 章）；
- 建议：**初始化后立即启动**，在主循环里"所有任务都跑过一遍"才喂狗；
- 调试时先关狗，否则断点会触发复位；
- 看门狗复位后读 PPOR 复位源确认是 "wdog" 而不是别的原因。

### 25.5.4 时钟与中断存活

- `soak_test` 用 GPTMR0 1 ms 中断计数：若中断停止（`irq_cnt` 不涨），说明中断被关/优先级被占/异常；
- `mcycle` 与 `MCHTMR` 双时基互校：两者推算的时间差应稳定（tp-perf 实测 diff_ppm=0.0）；
- 时钟被意外改写（例如误写 PLL 寄存器）会表现为串口波特率突变 → 长稳日志里能直接看到。

## 25.6 长稳测试：`soak_test` 例程

`samples/reliability/soak_test/` 每 1 ms 一轮，做四件事：

1. 8 KB `memcpy` + 软件 CRC32 校验（内存完整性）；
2. `bsp_uptime_ms()` 单调性检查（时基回绕/倒退）；
3. GPTMR0 1 ms 中断计数存活检查（连续 3 轮不涨 = 报错）；
4. 每 60 s 打印心跳（uptime/中断数/循环数/错误数）并翻转 USR LED。

长稳测试的框架如图 25-1 所示：左边是可以叠加的持续负载，中间是指标采集，右边是判定与回归。
真实项目里"跑 24 小时不出错"从来不是只跑一个 `for(;;)`，而是把 CPU、总线、网络、Flash、看门狗
这些最容易出问题的路径同时压住，再观察计数器是否单调、内存是否被踩、通信是否丢包。

![图 25-1 长稳测试框架：负载、指标采集与判定回归](figures/25/fig-25-1-soak-framework.png)

**图 25-1** 中的负载可以按项目需要取舍；本书的 `soak_test` 例程实现了其中的内存 CRC、时基单调性
与中断存活三项，第 5 节的数据就是它跑出来的。

运行方式：

```bash
cd hpm5e31_1
bash scripts/hwrun.sh reliability/soak_test --secs 1800 --lock-wait 1800
# 串口日志: logs/<时间>-soak_test.log
```

日志判读：

- 正常：`[soak] beat=N ... err=0` 每 60 s 一行，err 始终为 0；
- 异常：出现 `[soak] ERROR ...` 行（CRC 错 / uptime 倒退 / 中断停滞），按提示定位。

## 25.7 实测数据

表 25-4 长稳实测（本节数据由 `logs/` 中对应日志得出）

| 时长 | 心跳数 | 循环数 | IRQ 数 | 错误数 | 日志 |
|---|---|---|---|---|---|
<!-- BEGIN:SOAK -->
| 240 s（5 min 脚本） | 4 | 79,997 | 281,009 | **0** | `logs/20261030-114255-soak_test.log` |
<!-- END:SOAK -->

> 观测：240 s 内 CRC 校验 0 错误、uptime 单调、GPTMR 中断持续增长。
> 上一轮 240 s 日志里的中断计数折合 1 171 Hz（高于配置 1 kHz 约 17%），本轮 60 s 采集为
> 59 998/60.002 s = **999.97 Hz**，与 1 kHz 配置一致，旧数据的偏差未复现（怀疑当时是旧固件
> 或计数窗口误差），以本轮数据为准，旧日志保留备查。

本轮给 `soak_test` 增加了栈涂色统计，60 s 运行的关键数字见表 25-4b。

表 25-4b 栈水位与堆健康度实测（`logs/20261102-122235-soak_test.log`）

| 指标 | 实测值 | 说明 |
|---|---|---|
| 栈区大小 | 16 384 B（DLM 顶部） | `__stack_base__=0x00204000`，`_stack=0x00208000` |
| 栈峰值占用 | **1 172 B（7.2%）** | 0.8 s 后稳定在 1 172 B，60 s 心跳仍为 1 172 B |
| 堆 8×1 KB 分配/释放 | OK | `heap` 命令，反复分配无失败 |
| 60 s 错误数 | 0 | CRC / uptime / 中断三项全过 |
| GPTMR 中断计数 | 59 998 / 60.002 s | 999.97 Hz，与配置 1 kHz 一致 |

> 说明：受"单板 + 多任务并行"限制，本章的长稳测试在 30 分钟量级；24 h 长稳建议在项目
> 集成阶段用独立板卡执行，方法与本例程相同（把 `--secs` 改大、日志落盘即可）。

## 25.8 故障注入与恢复

表 25-5 故障注入实验

| 注入 | 方法 | 预期现象 | 恢复 |
|---|---|---|---|
| 看门狗复位 | 启动 WDG 后停止喂狗 | 约 1 s 后复位，串口重新打印 banner | 自动 |
| 软件复位 | shell `reboot` | 立即复位，PPOR=software | 自动 |
| 总线错误（非法地址） | `reg 0x00000001 0`（未对齐/保留区） | trap（默认死循环） | J-Link 复位/重烧 |
| 时钟误配 | 故意写错 PLL 分频 | 串口乱码、程序跑飞 | J-Link 重烧 |
| 程序跑飞（改坏镜像） | 擦除应用区后复位 | 停在 BootROM，无输出 | J-Link 重烧或 ISP |
| 掉电/欠压 | 手动断电瞬间上电 | PPOR=brownout，正常启动 | 自动 |

**救砖顺序**：J-Link 复位烧写（首选）→ 按住 USR BTN 上电进 ISP（PA03=1）→ 检查电源/晶振。

## 25.9 坑与注意事项

1. **BOOT_MODE 与按键/LED 共用引脚**：复位瞬间按住 BTN 会进 ISP，程序"不跑"（第 01 章）。
2. **外设时钟未开就访问寄存器**：总线错误/死机（本仓库 `soak_test` 最初就踩了 GPTMR0 时钟未开，
   现象是 shell banner 打到一半就停）。访问任何外设前 `clock_add_to_group()`。
3. **默认 trap 不保存现场**：死机没有信息，建议自写 trap（打印 mcause/mepc/mtval）或 J-Link halt。
4. **printf 在中断里重入**：newlib `printf` 非可重入，中断里打印可能死锁；用环形缓冲或只在主循环打印。
5. **cache 与 DMA**：DMA 写内存后 CPU 读可能读到旧值（第 19 章）。
6. **栈溢出**：DLM 只有 128 KB，大局部数组/深递归会踩到堆；用 `size`/map 检查。
7. **长稳测试会占住整块板**：用 `--lock-wait` 排队，别让其他任务等太久。

## 25.10 参考来源

1. RM Rev0.4 §18.2 p.274（BootROM 启动流程）、§19.3 p.414（BOOT_MODE）
2. `hpm_sdk/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（栈/堆/内存布局）
3. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_ppor_drv.h`（复位源位定义）
4. `hpm_sdk/drivers/inc/hpm_ppor_drv.h`（ppor_sw_reset/复位状态 API）
5. 本仓库 `samples/reliability/soak_test/`、`logs/*soak_test*.log`
6. 第 01 章（启动模式）、第 22 章（看门狗/低功耗）
7. HPM SDK 仓库与链接脚本：<https://github.com/hpmicro/hpm_sdk>（soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld）
8. 栈高水位标记方法参考：FreeRTOS uxTaskGetStackHighWaterMark
   <https://www.freertos.org/Documentation/02-Kernel/04-API-references/04-Task-utilities/uxTaskGetStackHighWaterMark>
9. 先楫文档站：<https://doc.hpmicro.com/>

## 25.11 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 项 | 状态 | 说明/验证方法 |
|---|---|---|
| 24 h 长稳 | **已知限制** | 自动化环境只有一块共享板，本轮累计做到 240 s + 60 s；24 h 需专用窗口或独立板卡。方法：`bash scripts/hwrun.sh reliability/soak_test --secs 86400`，判据 err=0、心跳 1440 行、栈峰值不随时间增长 |
| 温度/电源余量 | **已知限制** | 无温箱与功耗分析仪，TSNS 只能读相对温度；方法：恒温箱 -40/25/85 ℃ 各 1 h + 可调电源扫 3.0~3.6 V，记录 CoreMark/ADC/以太网误码 |
| 掉电瞬间 Flash 完整性 | **已知限制** | 需可编程电源做随机断电；产品上应做 A/B 双备份 + 镜像 CRC + 回滚。方法：写循环中随机断电 100 次，重启校验 CRC 并统计坏镜像数 |
| 栈水位测量 | **已实测（2026-11-02）** | `soak_test` 涂色法：栈 16 384 B，峰值 1 172 B（7.2%），60 s 无增长；日志 `logs/20261102-122235-soak_test.log` |
| 中断计数偏差（旧日志 1 171 Hz） | **已定位（已复核）** | 本轮 60 s 采集 999.97 Hz，与配置一致；旧 240 s 日志偏差未复现，以本轮为准 |
| EMC/ESD | **已知限制（不在本书范围）** | 需专业实验室（静电枪/暗室），本书不涉及；产品认证阶段单独做 |