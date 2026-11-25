# 第 30 章 UDS 诊断服务：会话、安全访问、DTC 与刷写

## 30.1 文档信息

| 项 | 内容 |
|---|---|
| 章版本 / 日期 | v1.0 / 2026-10-30 |
| 适用芯片 / 板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 协议标准 | ISO 14229-1:2013（UDS，Unified Diagnostic Services） |
| 仓库组件 | `components/app_uds/`（服务端 + DTC 内存 + 刷写后端） |
| 对应例程 | `samples/app/uds_ecu/`、`samples/app/uds_flash/` |
| 上位机 | `host/uds/`（uds_tester.py / mini_flasher.py / obd_scan.py / ecu_sim.py） |
| 实测日期 | 2026-10-30 17:23（uds_ecu）、18:05（uds_flash）；2026-11-02 复核待确认项 |
| 主日志 | `logs/20261030-172347-uds_ecu.log`、`logs/20261030-180524-uds_flash.log` |
| 可信度 | 板内数据全部来自上板串口日志；主机侧数字来自 vcan 模拟器，单独标注 |

## 30.2 本章导览

读完本章你应该能：

1. 说清 UDS 的"服务 + 子功能 + DID + NRC"四层结构，以及每个服务在哪个会话、要不要安全访问；
2. 自己实现 0x27 种子-密钥流程，并且不会被 3 次错误锁定、S3 超时这些细节易错点到；
3. 设计一张 DID 表和一个 DTC 故障内存（状态位 + 冻结帧 + 扩展记录）；
4. 用 0x34/0x36/0x37 + 0x31 + 0x11 完成一次 32 KB 镜像的下载、校验与复位，并知道**为什么绝不能擦正在执行的应用镜像**；
5. 用 udsoncan / 自写客户端两种方式对 ECU 做回归测试。

前置章节：第 29 章（ISO-TP，UDS 的传输层）。
相关章节：第 21 章（XPI Flash 与启动布局）、第 28 章（CAN 网关，把诊断报文搬到主机 vcan0）。

## 30.3 概述与典型应用

UDS 是汽车电子的"通用诊断语言"：诊断仪发一条请求，ECU 回一条响应，请求/响应里带服务号、DID、DTC、例程号。
它规定了**服务语义**（读/写/安全/刷写/复位）和**时序**（P2/P2*/S3），但不规定物理层——
UDS 可以跑在 CAN（ISO-TP）、CAN FD、以太网（DoIP）上，本仓库同时给出了 CAN 与 DoIP 的落地（第 28/29 章）。

典型应用：4S 店诊断仪读故障码、产线 EOL 刷写、远程 OTA 的分段刷写、售后读取 VIN/软件版本。

| 服务 | 名称 | 本实现 | 依赖 |
|---|---|---|---|
| 0x10 | DiagnosticSessionControl | 01/02/03 | — |
| 0x11 | ECUReset | 01/02/03（延迟 200 ms 执行） | — |
| 0x14 | ClearDiagnosticInformation | 组 FFFFFF | — |
| 0x19 | ReadDTCInformation | 01/02/04/06 | DTC 内存 |
| 0x22 | ReadDataByIdentifier | 多 DID | DID 表 |
| 0x27 | SecurityAccess | 01/02 一级、03/04 二级 | 种子算法 |
| 0x28 | CommunicationControl | 00/01/02/03 | 扩展会话 |
| 0x2E | WriteDataByIdentifier | 单 DID | 扩展会话 + 安全 |
| 0x2F | InputOutputControlByIdentifier | 00/01/02/03 | 扩展会话 |
| 0x31 | RoutineControl | 01/02/03，RID 可注册 | 扩展/编程会话 |
| 0x34 | RequestDownload | ALFID 0x44、DFI 0x00 | 编程会话 + 安全 |
| 0x36 | TransferData | 256 B/块、序号校验 | 0x34 |
| 0x37 | RequestTransferExit | — | 0x36 |
| 0x3E | TesterPresent | 00/80（抑制正响应） | — |
| 0x85 | ControlDTCSetting | 01/02 | 扩展/编程会话 |

**表 30-1** ：服务矩阵。`"本实现"列就是 `components/app_uds/src/app_uds.c` 的 `switch (sid)` 分支`。

## 30.4 工作原理

### 30.4.1 分层与后端注入

UDS 服务端本身不该关心"数据存在哪"。本仓库把后端全部做成可替换的：DID 表是数组、DTC 是 RAM 表、
例程是函数表、刷写是函数指针表（`app_uds_flash_driver_t`）。这样同一份 0x34/0x36/0x37 逻辑既能刷片内 Flash，
也能在主机单元测试里刷内存缓冲。

![图 30-1 UDS 服务端分层与后端](figures/30/fig-30-1-layers.png)

**图 30-1** ：请求从 ISO-TP 上来，经过 `app_uds_handle()` 的 SID 分发与会话/安全门控，再落到各后端；
响应原路返回。NRC 也由这一层统一生成。

### 30.4.2 会话与安全访问

UDS 用"会话"限制能做什么，用"安全访问"限制谁能做。

![图 30-2 会话与安全访问状态机](figures/30/fig-30-2-session-fsm.png)

**图 30-2** ：默认会话只能读；扩展会话能写 DID、做 IO 控制；编程会话才能刷写。
任何会话切换都会把安全等级清零；S3（5000 ms）内没有请求就自动退回默认会话。

会话与安全门控矩阵见表 30-2。

| 操作 | 默认 0x01 | 扩展 0x03 | 编程 0x02 | 需要安全等级 |
|---|---|---|---|---|
| 0x22 读 DID | 允许 | 允许 | 允许 | 0 |
| 0x19 读 DTC | 允许 | 允许 | 允许 | 0 |
| 0x2E 写 F190 | NRC 0x7F | 允许 | 允许 | 1 |
| 0x2F IO 控制 | NRC 0x7F | 允许 | 允许 | 0 |
| 0x28 / 0x85 | NRC 0x7F | 允许 | 允许 | 0 |
| 0x31 0x0203 自检 | NRC 0x7E | 允许 | 允许 | 0 |
| 0x31 0xFF00 / FF01 | NRC 0x7E | NRC 0x7E | 允许 | 1 |
| 0x34/0x36/0x37 | NRC 0x7F | NRC 0x7F | 允许 | 1 |
| 0x27 一级 | 允许 | 允许 | 允许 | — |
| 0x27 二级 | NRC 0x7E | 允许 | 允许 | 已有一级可选 |

**表 30-2** ：本实现的会话/安全门控矩阵（与 ISO 14229-1 的建议一致，工程上可按 OEM 诊断规范微调）。

0x27 的算法是"示例算法"（真实项目用 AES-CMAC 或 OEM 私有算法），但流程完整：

```c
uint32_t app_uds_calc_key(uint32_t seed, uint8_t level)
{
    uint32_t k = seed;
    for (uint32_t i = 0; i < 8u; i++) {
        k = (k << 1) | (k >> 31);          /* ROL 1 */
        k ^= 0x3A5C9E17u;
        if (k & 1u) { k += 0x5A5A5A5Au; }
    }
    k ^= (uint32_t) level * 0x01020304u;
    k = (k << 7) | (k >> 25);              /* ROL 7 */
    return k;
}
```

同一份算法在 `host/uds/uds_sec.py` 与 C 端逐位一致，测试向量：seed=0xD2E3FB68、level=1 → key=0x2B589DBE
（板内日志与主机模拟器算出的都是这个值，见 31.6 节）。

### 30.4.3 NRC：负响应码

所有失败都用 `7F <SID> <NRC>` 三字节回应。NRC 的取值决定了诊断仪怎么重试。

NRC 与触发场景见表 30-3。

| NRC | 名称 | 本实现触发场景 |
|---|---|---|
| 0x10 | generalReject | 保留 |
| 0x11 | serviceNotSupported | 未实现的 SID |
| 0x12 | subFunctionNotSupported | 未知子功能 |
| 0x13 | incorrectMessageLengthOrInvalidFormat | 长度/DID 数据长度不对 |
| 0x22 | conditionsNotCorrect | 例程已在运行等 |
| 0x24 | requestSequenceError | 没取种子就发 key / 没 0x34 就 0x36 / 例程未启动就要结果 |
| 0x31 | requestOutOfRange | 未知 DID / RID / DTC、地址越界、ALFID 非法 |
| 0x33 | securityAccessDenied | 未解锁就写 DID / 刷写 |
| 0x35 | invalidKey | 密钥错（未达 3 次） |
| 0x36 | exceedNumberOfAttempts | 第 3 次密钥错，进入 10 s 锁定 |
| 0x37 | requiredTimeDelayNotExpired | 锁定期间再请求种子 |
| 0x72 | generalProgrammingFailure | Flash 擦/写失败 |
| 0x73 | wrongBlockSequenceCounter | 0x36 块序号错 |
| 0x78 | requestCorrectlyReceived-ResponsePending | 擦除等长操作前主动发，P2* 续期 |
| 0x7E | subFunctionNotSupportedInActiveSession | 编程专用服务在非编程会话 |
| 0x7F | serviceNotSupportedInActiveSession | 扩展服务在默认会话 |

**表 30-3** ：NRC 与触发场景。0x78 是唯一"还会继续发最终响应"的负响应，客户端必须继续等。

### 30.4.4 时间参数：P2、P2*、S3

P2/P2*/S3 的取值与实测证据见表 30-4。

| 参数 | 定义 | 本实现 | 实测证据 |
|---|---|---|---|
| P2 | 收到请求到发出首个响应的上限 | 50 ms | 0x10 03 响应报文里回 `00 32`（50 ms） |
| P2* | 发 0x78 后再等最终响应的上限 | 5000 ms | 慢例程 150 ms 只用掉 3% |
| S3 | 非默认会话无请求自动回默认 | 5000 ms | 静默 5.5 s 后 2E 返回 NRC 0x7F |
| 0x27 锁定时长 | 3 次错误后禁止再试 | 10 s | 第 4 次取种子返回 0x37 |

**表 30-4** ：P2/P2*/S3。注意 P2* 是**每次收到 0x78 都重新计时**，所以擦除 32 KB（14.7 ms）远小于 P2*，
但为了兼容慢速 Flash 与更大的镜像，代码里 0x34 一律先发 0x78。

### 30.4.5 DTC 状态位与快照

statusOfDTC 的 8 个状态位见表 30-5。

| 位 | 名称 | 含义 |
|---|---|---|
| 0 | testFailed | 当前测试失败 |
| 1 | testFailedThisOperationCycle | 本操作循环内失败过 |
| 2 | pendingDTC | 待定故障 |
| 3 | confirmedDTC | 已确认（点灯） |
| 4 | testNotCompletedSinceLastClear | 清码后还没测完 |
| 5 | testFailedSinceLastClear | 清码后失败过 |
| 6 | testNotCompletedThisOperationCycle | 本循环还没测完 |
| 7 | warningIndicatorRequested | 请求点亮警告灯 |

**表 30-5** ：statusOfDTC 的 8 个位。本实现 `availability_mask = 0x09`（只承诺 bit0 与 bit3）。

快照（0x19 04）存 3 个 DID：0x0100 车速、0x0101 蓄电池电压、0x0102 冷却液温度；
扩展数据记录（0x19 06，record 0x02）存发生次数与老化计数。板内日志里 0xC10087 的状态是 0x5F，
即 testFailed + pending + confirmed + 两个 notCompleted 位同时置位——这是"当前还在失败"的故障。

## 30.5 寄存器与驱动 API 对照

UDS 动作与底层寄存器/API 的对照见表 30-6。

| UDS 概念 / 动作 | 硬件/驱动机制 | 本仓库 API | 代码位置 |
|---|---|---|---|
| 请求/响应传输 | ISO-TP（第 29 章） | `app_isotp_send/on_frame` | `components/app_isotp/` |
| 0x11 复位 | PPOR 软复位 | `bsp_reboot()` | `boards/hpm5e31_1/bsp/bsp.c` |
| 0x34 擦除 | XPI NOR ROM API `rom_xpi_nor_erase` | `fl_begin()` | `components/app_uds/src/app_uds_flash.c` |
| 0x36 写入 | `rom_xpi_nor_program`（按页切分） | `fl_write()` | 同上 |
| 0x31 FF00 校验 | XIP 直读 + CRC32 | `fl_crc32()` | 同上 |
| 写后读一致性 | L1 D-Cache 无效化 | `l1c_dc_invalidate()` | 同上 |
| 擦写期间取指安全 | 关全局中断（MSTATUS.MIE） | `irq_save/irq_restore` | 同上 |
| 刷写窗口限制 | Flash 布局（应用之后的保留区） | `app_uds_flash_window()` | 同上 |
| 会话保持计时 | MCHTMR 24 MHz | `app_uds_poll(now_us)` | `components/app_uds/src/app_uds.c` |
| DID 存储 | 静态数组（可改） | `app_uds_did_t.data` | 同上 |
| DTC 存储 | RAM 表（可扩展 Flash） | `app_uds_dtc_mem_t` | `app_uds_dtc.c` |

**表 30-6** ：UDS 动作与底层寄存器/API 对照。

## 30.6 例程与上板实测

### 30.6.1 samples/app/uds_ecu：完整服务序列

uds_ecu 例程速览见表 30-7。

| 项 | 内容 |
|---|---|
| 路径 | `samples/app/uds_ecu/` |
| 角色 | CAN0 = ECU（0x7E0/0x7E8），CAN1 = 板内测试仪 |
| 覆盖 | 13 步序列：10 03 → 22×4 DID → 2E 未解锁(NRC 33) → 27 01/02 → 2E 成功 → 22 回读 → 31 01 0203 → 19 02 → 14 FFFFFF → 19 02 空 → 3E 00 → 10 01 |
| 额外 | `pend`（0x78 + P2*）、`s3`（S3 超时）、`sec`（3 次错 key）、`dtc`（故障内存） |
| 日志 | `logs/20261030-172347-uds_ecu.log` |

**表 30-7** ：uds_ecu 例程速览。

实测（经典 500 kbit/s，ISO-TP BS=0/STmin=0，串口日志逐条判定）：

| 步骤 | 响应 | RTT µs | 结论 |
|---|---|---|---|
| 10 03 扩展会话 | `50 03 00 32 01 F4` | 565 | PASS |
| 22 F190/F195/F187/F18C | 62 B（4 个 DID） | 3085 | PASS |
| 2E F190 未解锁 | `7F 2E 33` | 1175 | PASS |
| 27 01 取种子 | seed=0xD2E3FB68 | 479 | PASS |
| 27 02 发密钥 | key=0x2B589DBE → `67 02` | 473 | PASS |
| 2E F190 已解锁 | `6E F1 90` | 1171 | PASS |
| 22 F190 回读 | VIN=HPM5E31NEWVIN0002 | 1166 | PASS |
| 31 01 0203 自检 | `71 01 02 03 00 01` | 498 | PASS |
| 19 02 读 DTC | 15 B（3 个 DTC） | 1190 | PASS |
| 14 FFFFFF 清码 | `54` | 473 | PASS |
| 19 02 清码后 | `59 02 09`（0 个 DTC） | 468 | PASS |
| 3E 00 | `7E 00` | 468 | PASS |
| 10 01 默认会话 | `50 01 00 32 01 F4` | 480 | PASS |

**表 30-8** ：uds_ecu 13/13 PASS，avg RTT ≈ 0.97 ms，最短 0.47 ms、最长 3.09 ms（62 B 多帧响应）。

三个专项：

1. **0x78 pending**：`31 01 0301`（150 ms 慢例程）测到 **1 个 0x78**，最终响应 `71 01 03 01 00 01`，
   总 RTT 150710 µs —— 与例程里的 150 ms 延时吻合，说明 P2* 续期生效。
2. **S3 超时**：10 03 后静默 5.5 s，再发 2E 得到 NRC 0x7F，证明会话确实退回了默认会话（日志里 s3_timeout=1）。
3. **安全访问锁定**：第 1/2 次错误 key 得 0x35，第 3 次得 0x36，紧接着取种子得 0x37 —— 三条曲线全部符合预期。
4. **DTC 内存**：`dtc` 命令打印 3 条故障，status=0x5F、0x58、0x54，快照 3 个 DID 全在。

### 30.6.2 samples/app/uds_flash：32 KB / 64 KB 镜像下载

uds_flash 例程速览见表 30-9。2026-11-02 把 `IMG_SIZE` 从 32768 改成 65536，正好占满最后 64 KB
安全窗口，0x36 从 128 块变 256 块；编译后 FLASH 96.8 KB、AXI_SRAM 81.7 KB（含 64 KB 镜像缓冲）。
上板 10/10 PASS，逐项数据见 31.6.2 末尾的对照表（日志 `logs/20261102-123311-uds_flash.log`）。

| 项 | 内容 |
|---|---|
| 路径 | `samples/app/uds_flash/` |
| 下载区 | 0x800F0000 ~ 0x80100000（1 MB 片内 Flash 最后 64 KB 安全区，正好放一个 64 KB 镜像） |
| 镜像 | 64 KB：`[0..7]` 魔数 HPMIMG01，`[8..11]` 长度，`[12..15]` 数据区 CRC32，`[256..]` 数据 |
| 流程 | 10 02 → 27 01/02 → 34 → 36×256 → 37 → 31 01 FF00 → 31 01 FF01 → 11 01 |
| 日志 | `logs/20261030-180524-uds_flash.log`（32 KB）、`logs/20261102-123311-uds_flash.log`（64 KB） |

**表 30-9** ：uds_flash 例程速览。

![图 30-3 刷写时序与实测耗时](figures/30/fig-30-3-flash-seq.png)

**图 30-3** ：32 KB 镜像的刷写时间分布。总耗时 **1.35 s**，其中 0x36 数据传输 1.324 s 占 98%，
真正写 Flash 只用了 118.9 ms。这就是"诊断刷写的瓶颈在 CAN 带宽，不在 Flash"的直观证据。

64 KB 镜像的实测（同一套代码，只改 `IMG_SIZE`，2026-11-02）：

| 项 | 32 KB 版 | 64 KB 版 | 说明 |
|---|---|---|---|
| 0x34 请求下载 RTT | 16.14 ms | 3.34 ms | 都带 1 个 0x78 pending |
| 0x36 块数 / 每块 | 128 / 256 B | 256 / 256 B | 块大小不变 |
| 0x36 块 RTT 均值 / 最大 | 10.20 / 10.86 ms | 10.19 / 10.85 ms | 单块时间与镜像大小无关 |
| 0x36 总耗时 | 1.324 s | 2.617 s | 随块数线性翻倍 |
| 有效吞吐 | 24.2 KB/s | 24.5 KB/s | 受 ISO-TP/500 kbit/s 限制 |
| Flash 擦除 | 14.66 ms | **1.87 ms** | 反直觉，见下 |
| Flash 编程 | 118.9 ms | 238.4 ms | 268.5 KB/s，线性翻倍 |
| 31 01 FF00 CRC32 | 0x791B3AB8 | 0x336CED9F | 与本地计算一致 |
| XIP 直读全镜像 | PASS | PASS | memcmp identical |
| 11 01 复位后校验 | PASS | `len=65536 crc=0x336CED9F` | 掉电保持 |

**表 30-14** ：32 KB 与 64 KB 镜像刷写对照（`logs/20261030-180524-uds_flash.log`、
`logs/20261102-123311-uds_flash.log`）。

擦除时间的反常值得记一笔：32 KB 用 8 个 4 KB 扇区擦了 14.66 ms，64 KB 反而只花 1.87 ms。
两次的 `rom_xpi_nor_erase()` 调用参数只差地址范围，最可能的解释是 64 KB 范围
（0x800F0000 起、64 KB 对齐）被 ROM API 合并成一次块擦除，而 32 KB 只能逐扇区擦。
这条解释还没做实（要读 XPI 的 block/sector 属性），但两次原始日志都在，下一轮加一行
`xpi_nor_property_block_size` 打印就能确认。对刷写流程没有影响：两种镜像都擦干净了，
31 01 FF00 读回的 CRC 与本地一致。

| 阶段 | 实测 | 说明 |
|---|---|---|
| 34 请求下载（含擦除 32 KB） | 16137 µs，pending=1 | 擦除 14664 µs → 2.23 MB/s（与第 21 章一致） |
| 36 传输 128 块 × 256 B | 1324486 µs，24.2 KB/s | 每块 RTT 10203 µs（含 37 帧 ISO-TP 往返） |
| 37 退出传输 | 常规 | — |
| 31 01 FF00 校验 CRC32 | ECU=0x791B3AB8 = 本地 | 读回一致 |
| 31 01 FF01 依赖检查 | 魔数/长度/CRC 全对 | status=0x00 |
| XIP 直读全镜像 | memcmp identical | 32768 B 逐字节一致 |
| Flash 写入总量/耗时 | 32768 B / 118941 µs | **269 KB/s**（裸写速率，第 21 章为 280 KB/s） |
| 11 01 复位 | `51 01` → PPOR 复位 | 复位后启动自检 PASS |

**表 30-10** ：uds_flash 10/10 PASS（`pass=10 fail=0`）。复位后固件重新启动，打印
`PASS post-reset: 0x800F0000 存在有效镜像 magic=HPMIMG01 len=32768 crc=0x791B3AB8`，
同时验证了"0x11 01 真的复位了"和"Flash 内容掉电保持"。

刷写速率有两个口径，别混用：

- **总线侧 24.2 KB/s**：这是"诊断仪视角"的速度，受经典 CAN 500 kbit/s + ISO-TP 限制；
- **Flash 侧 269 KB/s**：这是"芯片视角"的裸写速度（不含传数据）。

换成 CAN FD（第 29 章实测 189.6 KB/s 双向）后，同样 32 KB 的理论刷写时间可从 1.35 s 降到约 0.2 s。

### 30.6.3 上位机工具（host/uds）

上位机工具清单见表 30-11。

| 工具 | 作用 | 依赖 | 验证结果 |
|---|---|---|---|
| `isotp_test.py` | ISO-TP 回显测试 | 裸 SocketCAN 实现 + can-isotp 对照 | raw 7/7 PASS |
| `uds_tester.py` | 13 步诊断序列 | 裸实现 + udsoncan 对照 | raw 12/12、udsoncan 10/10 PASS |
| `mini_flasher.py` | 34/36/37 + 31 校验 | 裸实现 | 模拟器 32 KB PASS，CRC MATCH |
| `obd_scan.py` | OBD-II PID 扫描（01/09） | 裸实现 | 6/6 PID PASS |
| `ecu_sim.py` | 主机侧 ECU 模拟器（vcan） | 裸实现 | 供上面四个工具回归 |
| `uds_sec.py` | 0x27 算法离线校验 | 无 | 8 组测试向量 |

**表 30-11** ：上位机工具清单。

主机侧实测（**vcan5 上的模拟器，不是板卡**，用于证明工具本身正确）：

```text
== UDS 诊断序列（impl=raw, vcan5）==
  10 03 扩展会话      50 03 00 32 01 F4    129 us PASS P2=50 ms P2*=5000 ms
  22 F190/...         62 F1 90 ...          160 us PASS 62 B
  2E F190 未解锁       NRC 0x33               29 us PASS
  27 01/02 安全访问     67 02                  34 us PASS seed=0xD2E3FB68 key=0x2B589DBE
  ...
[uds_tester] PASS steps=12/12 avg_rtt=39 us
[uds_tester] PASS steps=10/10   (impl=udsoncan)
[mini_flasher] PASS 刷写速率 3560.1 KB/s (ERR: 内存模拟器，非真实 Flash)
[obd_scan] PASS pids=6/6
```

说明：模拟器的 RTT 是"内核 vcan 环回"，微秒级，**不能**当作真实链路延迟；
真实链路的延迟见 31.6.1 的板内数据（0.47~3.09 ms）与第 28 章网关数据（往返 2.26 ms）。

## 30.7 性能与调优

性能汇总见表 30-12。

| 指标 | 数值 | 来源 |
|---|---|---|
| 单帧请求/响应 RTT（板内） | 0.47~0.57 ms | 表 30-8 |
| 多帧响应（62 B） | 3.09 ms | 表 30-8 |
| 慢例程（150 ms + 0x78） | 150.71 ms | logs/20261030-172347 |
| 32 KB 刷写总耗时 | 1.35 s | 表 30-10 |
| 总线侧刷写吞吐 | 24.2 KB/s | 表 30-10 |
| Flash 裸写速率 | 269 KB/s | 表 30-10 |
| Flash 擦除速率 | 2.23 MB/s（32 KB/14.7 ms） | 表 30-10 |

**表 30-12** ：UDS 性能汇总。

调优建议：

1. **先把 CAN FD 打开**：刷写吞吐从 24.2 → 理论约 190 KB/s，收益最大且不改协议；
2. **0x36 块大小取到 maxNumberOfBlockLength**：本实现 258（含 2 字节头），每块 256 B 数据；
   块再大只是减少 0x36 请求次数，收益有限（每块 RTT 主要花在 37 帧 ISO-TP 上）；
3. **擦除要流水化**：现在 0x34 里一次性擦 32 KB（14.7 ms），镜像更大时应按扇区边收边擦或后台擦；
4. **不要在中断里跑 UDS**：本实现全部在主循环，擦写期间关中断，避免 XIP 取指打到正在编程的 Flash。

## 30.8 易错点

1. **绝不能擦正在 XIP 执行的应用镜像**。1 MB 片内 Flash 布局是 nor_cfg_option@0x400 / boot_header@0x1000 /
   应用@0x3000；本示例把下载区放在最后 64 KB（0x800F0000 起）。擦掉 0x3000~0x14000 会让 CPU 立刻取不到指令。
2. **NOR Flash 的 Page Program 在页内回绕**。一次编程跨 256 B 页边界时，超出部分会写回本页开头。
   本示例最初从 0x800F0010 开始写、每块 256 B，结果 0x800F0100 之后的 240 B 全部回绕，
   魔数被写坏、CRC 对不上。解决：镜像头占满 1 页（256 B），数据区从 0x800F0100 起整页编程；
   `fl_write()` 里还保留了按页切分的兜底逻辑。
3. **ROM API 写完必须 invalidate D-Cache**。XIP 直读可能命中旧 cacheline，读到"看起来没写进去"的旧数据。
4. **擦写期间要关全局中断**。中断向量表在 Flash 里，XPI 忙时取指会拿到无效数据。
5. **0x78 不是"错误"**。它是"我收到了，但还要时间"，客户端必须继续等最终响应，不能当失败；
   ECU 侧每次长操作前都要发，否则主机会在 P2=50 ms 处超时。
6. **S3 超时后会话与安全等级一起丢**。很多实现只回会话、忘了清安全等级，会留下"已解锁但不该解锁"的漏洞。
7. **0x27 每次发 key 前必须重新取种子**。本实现把 `seed_pending` 清 0，第二次直接发 key 会得到 NRC 0x24
   （第一版板内测试就是这么失败的，见 `logs/20261030-172347` 之前的调试）。
8. **0x36 块序号是 1..255 循环**，且必须与 ECU 期望值一致，否则 NRC 0x73。
   注意 0x36 的 258 字节请求本身是 ISO-TP 多帧消息，序号错和 ISO-TP 的 SN 错是两回事，别混。
9. **DTC 内存在 `app_uds_init()` 里不能 memset**。第一版在 init 里自动清了 DTC 表，
   把上层预置的故障码抹掉，19 02 永远读到 0 条（板内日志里 19 02 只有 3 字节那一版就是证据）。
10. **0x11 复位要延迟执行**。响应还没发完就复位，主机会收不到 `51 01`。本实现延迟 200 ms。

## 30.9 对比：UDS、KWP2000 与 DoIP

UDS、KWP2000 与 DoIP 的对比见表 30-13。

| 维度 | UDS (ISO 14229-1) | KWP2000 (ISO 14230) | DoIP (ISO 13400) |
|---|---|---|---|
| 年代 / 定位 | 2006+，现代统一诊断 | 1990s，K 线/CAN 时代 | 2012+，以太网诊断 |
| 服务号 | 1 字节，服务极多 | 1 字节，服务较少 | 复用 UDS 服务 |
| 传输层 | ISO-TP（CAN）/ TCP（DoIP） | KWP2000 传输层 | TCP 13400 |
| 会话/安全 | 0x10 多会话 + 0x27 种子密钥 | StartSession + SecurityAccess | 同 UDS |
| 刷写 | 0x34/0x36/0x37 | 0x34/0x36/0x37（早期实现类似） | 同 UDS，带宽高得多 |
| 本板实测 | 本章 | 未做 | 未做（第 28 章有 SOME/IP 网关） |
| 典型 RTT | 0.47~3.1 ms（ISO-TP） | 与 CAN 相近 | 0.2~1 ms |

**表 30-13** ：UDS / KWP2000 / DoIP 对比。KWP2000 在 2010 年后基本被 UDS 取代，
DoIP 则是"把 UDS 服务原样搬到以太网"，所以本章实现的服务层代码将来可以直接复用。

## 30.10 参考来源

上游与官方链接（核对日期 2026-11-02）：

- ISO 14229-1:2020 <https://www.iso.org/standard/72439.html>（UDS 服务与 NRC；本书按 2013 版编号，服务号两版一致）
- ISO 15765-2:2016 <https://www.iso.org/standard/66574.html>（DoCAN 传输层）
- python-udsoncan 文档 <https://udsoncan.readthedocs.io/>、源码 <https://github.com/pylessard/python-udsoncan>
- python-can 文档 <https://python-can.readthedocs.io/>
- EcuBus-Pro（开源诊断工具，用于互操作对照）<https://github.com/ecubus/EcuBus-Pro>
- Linux 内核 ISO-TP 实现 <https://github.com/torvalds/linux/blob/master/net/can/isotp.c>


1. ISO 14229-1:2013 *Road vehicles — Unified diagnostic services (UDS) — Part 1: Specification and requirements*。
2. ISO 14229-2:2013（会话层时间参数 P2/P2*/S3）。
3. ISO 15765-2:2016（ISO-TP，本仓库第 29 章）。
4. udsoncan 官方文档与源码（`host/uds/uds_tester.py --impl udsoncan` 用它做对照）。
5. 本仓库第 21 章（XPI Flash 布局与擦写速度）、第 28 章（CAN 网关）、第 29 章（ISO-TP）。
6. 实测日志：`logs/20261030-172347-uds_ecu.log`、`logs/20261030-180524-uds_flash.log`。
7. 图脚本：`tools/figs/app31_uds_layers.py`、`tools/figs/app31_uds_fsm.py`、`tools/figs/app31_flash_seq.py`。

## 30.11 待确认项与风险

表中条目已逐条复核（2026-11-02）：能上板测的给了数据和日志路径；本轮确实测不了的降级为已知限制，写明原因和一条可执行的验证方法，不再保留待确认/未尝试的裸条目。


| 编号 | 项 | 状态 | 验证方法 |
|---|---|---|---|
| U-31-1 | 经 CAN 网关（Bench B，vcan0）的真实 UDS RTT | 已知限制（本轮无 Bench B 窗口） | 原因：网关路径要独占 cannelloni + vcan，板卡 8 人共享排队。验证方法：`hwrun.sh uds_ecu --no-flash --secs 60 --post "timeout 60 python3 host/uds/uds_tester.py --channel vcan0"`，把结果与表 31-x 的板内 RTT 并列（网关应多约 2.3 ms） |
| U-31-2 | udsoncan over vcan0 对真实 ECU 的互操作 | 已知限制（主机依赖 pyudsoncan，本轮未装/未排窗口） | 模拟器已 10/10 PASS。验证方法：`pip install udsoncan` 后跑 `python3 host/uds/uds_tester.py --channel vcan0 --impl udsoncan`，并在 `data_identifiers` 里补全 F187/F190/F010 的编解码 |
| U-31-3 | 64 KB 镜像刷写（当前 32 KB） | **已实测（2026-11-02）**：10/10 PASS，256 块、总耗时 2.62 s、24.5 KB/s、programmed=65536 B、CRC32 0x336CED9F、XIP 逐字节一致、0x11 复位后镜像仍在 | 见 31.6.2 表 30-14；日志 `logs/20261102-123311-uds_flash.log`；擦除时间反常（1.87 ms）已单独说明 |
| U-31-4 | DTC 的 Flash 持久化 | 已知限制（未实现 NVM 后端） | 原因：DTC 表由调用者放在 RAM（`app_uds_dtc_mem_t`），组件只提供 RAM 操作，写 Flash 需要在 0x14/故障置位时加一个"序列化 + 页擦写"钩子（约 80 行，参考 `components/app_uds/src/app_uds_flash.c`）。验证方法：加 NVM 后跑 `uds_ecu` 的 `fault` 命令置 2 个 DTC → `reset` 软复位 → `19 02` 读回数量应为 2；再掉电 10 s 上电复读 |
| U-31-5 | 0x2F IO 控制对真实执行器的效果 | 已知限制（未接执行器） | 原因：板上 0x2F 只改 DID F010 的控制字节并回读，GPIO 输出没接 LED/继电器。验证方法：把 `0x2F` 处理函数里的回读值接到 `board_gpio_write()` 驱动板载 LED（第 04 章 GPIO 例程），用 `0x2F 03 01 03 F010` 写 1/0，肉眼或万用表看 LED 通断 |
| U-31-6 | 0x28 通信控制对总线的实际影响 | 已知限制（需网关侧配合） | 原因：通信控制只记录状态，没有真正在 CAN 驱动层屏蔽非诊断帧；单板双节点环境里另一端是我们自己的主机，屏蔽了也看不出效果。验证方法：板端加"按 0x28 状态过滤下发帧"的逻辑后，主机 `candump` 统计周期报文是否停发 |
| U-31-7 | 刷写期间断线/掉电的恢复 | 已知限制（无 A/B 分区） | 原因：只有镜像头魔数 + CRC 校验，坏镜像能识别，但没有"回滚到旧镜像"的第二分区（Flash 布局没预留）。验证方法：刷写中途断电后上电，例程应打印 `post-reset: 下载区无有效镜像`；A/B 方案需在链接脚本里预留 2 个 64 KB 区并加启动选择逻辑 |
| U-31-8 | OBD-II（服务 0x01/0x09）的板端实现 | 已知限制（板端未创建 OBD 例程） | 组件已留 `cfg.obd` 钩子，主机模拟器 6/6 PID PASS。实现方式：在 `app_uds_cfg_t.obd` 挂 0x01/0x09 处理函数，DID/DTC 复用现有表；验证用 `host/uds/obd_scan.py --channel vcan0` 扫 0x01 00/0C/0D 与 0x09 02 |
