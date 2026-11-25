# 第 19 章 DMA / DMAMUX 与数据搬运

## 19.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/dma/dma_mem2mem/`、`samples/dma/dma_uart_rx_idle/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（RM 原理 + 本板上板实测，全部带宽数字来自串口日志） |

## 19.2 本章导览

- 学到什么：DMAv2 的 XDMA / HDMA 双控制器架构、DMAMUX 请求路由、传输描述符与链式搬移；
  如何用 DMA 把内存搬移做到 **400 MB/s 量级**，以及什么参数会让带宽掉到 1/16。
- 前置章节：第 02 章（构建烧写）、第 05 章（UART，了解外设 DMA 请求）、第 23/24 章（总线与性能）。
- 配套例程：`samples/dma/dma_mem2mem/`（含 README 与实测日志）。
- 实测平台：HPM5E31 @480 MHz，AXI SRAM 256 KB（其中 64 KB 配成 non-cacheable），AHB SRAM 32 KB，
  mchtmr 24 MHz 自由运行计数器做 µs 级计时。

## 19.3 概述与典型应用

HPM5E31 内有 **2 个 DMAv2 控制器**（RM §33 p.561）：

- **XDMA**：主设备接 AXI 系统总线，面向高带宽的"内存 ↔ 内存 / 外部存储器"搬移；
- **HDMA**：主设备接 AHB 外设总线，面向"外设寄存器 ↔ 内存"的低延迟搬移；
- 两者**编程模型相同**，各有 16 个通道（`DMA_SOC_CHANNEL_NUM = 32`，见
  `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_feature.h:72`）；
- 两者各 32 个 DMA 请求输入，经 **DMAMUX**（DMA 请求路由器，基址 `0xF00C4000`）把
  `0xF00C8000` 附近的 HDMA 与 `0xF00C4000` 的 XDMA 连接到 64 个请求输出：
  **输出 0~31 → HDMA，输出 32~63 → XDMA**（RM §33 p.561）。

典型应用：

| 场景 | 用哪个控制器 | 原因 |
|---|---|---|
| ADC 序列采样结果入内存（每 1 µs 一组） | HDMA 或 XDMA 均可 | ADC 走 AHB，数据量中等 |
| UART 不定长帧接收（IDLE + DMA） | HDMA | 外设寄存器在 AHB 侧，延迟低 |
| SPI/ENET 大块数据搬运 | XDMA | 需要 AXI 带宽 |
| LCD/JPEG/大缓冲内存搬运 | XDMA | 400 MB/s 级 |
| 链式多段缓冲区（描述符串联） | 两者都支持 | 免 CPU 干预 |

表 19-1 关键特性（来源：RM §33 p.561、SDK SoC 头文件）：

| 特性 | XDMA | HDMA | 出处 |
|---|---|---|---|
| 通道数 | 16 | 16 | `DMA_SOC_CHANNEL_NUM=32` |
| 最大传输位宽 | 8 B（DOUBLE_WORD） | 4 B（WORD） | `hpm_soc_feature.h:70` |
| 最大突发个数 | 1024 T | 128 T | `hpm_soc_feature.h:71` |
| 总线 | AXI | AHB | RM §33 p.561 |
| 链式描述符 | 支持 | 支持 | RM §33 p.565 |
| 地址自增/固定 | 支持 | 支持 | 驱动 `DMA_ADDRESS_CONTROL_*` |
| 字节序/交换 | 8/16/32 位交换 | 8/16 位交换 | RM §33 p.561 |

## 19.4 工作原理

### 19.4.1 总线拓扑

![图 19-1 DMAv2 总线拓扑与 DMAMUX 路由](figures/19/fig-19-1-dmav2-topology.png)

如图 19-1 所示，两个控制器共享同一套寄存器模型，但"挂在哪条总线上"决定了它们的擅长领域：
XDMA 直连 AXI fabric，可以打满 AXI SRAM 带宽；HDMA 在 AHB 外设总线上，访问 AXI SRAM 必须
经过 AHB→AXI 桥，突发长度受桥的限制（见第 7 节易错点 2）。

### 19.4.2 一次传输的展开

一次通道配置（`dma_setup_channel()`）会写入以下要素：

1. 源/目的地址与地址控制（自增或固定，外设寄存器固定）；
2. 源/目的位宽（1/2/4/8 B）；
3. 突发个数：每次总线事务连续搬几个"位宽"的数据（1T/2T/…/1024T）；
4. 总字节数 `size_in_byte`（必须能被"位宽 × 突发"整除）；
5. 优先级、中断掩码（TC/HALF_TC/ERROR）、链式描述符指针。

传输启动后 DMA 会按"突发"为单位发起总线事务，搬完 `size_in_byte` 置 TC 标志并可选触发中断。
本仓库例程用轮询 TC 的方式计时，避免中断抖动污染 µs 级测量。

### 19.4.3 链式描述符

链式传输把多个"段"串起来：当前段搬完后硬件自动装载 `linked_ptr` 指向的描述符继续搬，
直到某段 `linked_ptr = 0`。描述符必须按 `DMA_LINKED_DESCRIPTOR_ALIGN` 对齐，
且每段的 `size_in_byte` 必须能被"位宽 × 突发"整除（RM §33 p.565，驱动
`hpm_dmav2_drv.c` 中 `dma_config_linked_descriptor()` 会做对齐检查）。

### 19.4.4 与 cache 的关系

HPM5E31 的 CPU0 带 L1 D-Cache，而 DMA 是总线主设备，**不经过 CPU 的 cache**。因此：

- DMA 写目的缓冲 → CPU 读之前必须 `l1c_dc_invalidate()`；
- CPU 写源缓冲 → DMA 读之前必须 `l1c_dc_flush()`；
- 最省事的做法是把 DMA 缓冲放到 **non-cacheable 区**（链接脚本里的
  `AXI_SRAM_NONCACHEABLE`，由 `ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT` 指定），
  本仓库所有 DMA 例程都这么做。

## 19.5 关键寄存器与驱动 API 对照

表 19-2 常用寄存器/位域与驱动 API（寄存器名见 RM §33 的寄存器列表，SDK 封装见 `hpm_dmav2_drv.h`）：

| 功能 | 寄存器 | 关键位域 | 驱动 API | 例程位置 |
|---|---|---|---|---|
| 通道控制 | `CHCTRL` | `SIZE` / `SRCWIDTH` / `DSTWIDTH` | `dma_setup_channel()` | `src/main.c` |
| 通道状态 | `CHSTATUS` | `TC` / `ERROR` / `HALF_TC` | `dma_check_transfer_status()` | 同上 |
| 源地址 | `SRCADDR` | 32 位地址 | `ch_config.src_addr` | 同上 |
| 目的地址 | `DSTADDR` | 32 位地址 | `ch_config.dst_addr` | 同上 |
| 链式指针 | `LLPOINTER` | 描述符地址（对齐） | `dma_config_linked_descriptor()` | `cmd_dmachain()` |
| 中断使能 | `INTEN` | TC/HALF/ERROR | `ch_config.interrupt_mask` | 第 19.4 节 |
| 握手/请求 | `HANDSHAKE` | 请求源编号 | `dma_setup_handshake()` | 第 05 章 UART 例程 |
| DMAMUX 路由 | `DMAMUX->MUXCFG` | `SOURCE` / `ENABLE` | `dmamux_config()` | 第 5/7 章例程 |

## 19.6 例程逐条讲解

### 19.6.1 例程：`samples/dma/dma_mem2mem/`

| 项 | 内容 |
|---|---|
| 路径 | `samples/dma/dma_mem2mem/` |
| 覆盖特性 | 突发扫描、位宽矩阵、XDMA/HDMA 对比、AHB SRAM 路径、链式描述符、memcmp 校验 |
| 关键配置 | 缓冲 16 KB 在 non-cacheable AXI SRAM；mchtmr 24 MHz 计时；链式 3 段 × 4 KB |
| 实测现象 | XDMA 峰值 407 MB/s；HDMA AXI 64B 117.7 MB/s、128B 掉到 25.9 MB/s；AHB SRAM 327.7 MB/s |
| 实测日志 | `logs/20261030-110400-dma_mem2mem.log`、`logs/20261030-110512-dma_mem2mem.log` |

关键代码（去掉打印后的核心，来自 `samples/dma/dma_mem2mem/src/main.c`）：

```c
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(8) static uint8_t s_src[16384];
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(8) static uint8_t s_dst[16384];

static bool dma_copy_memcpy(DMAV2_Type *ptr, uint8_t ch, uint32_t dst, uint32_t src,
                            uint32_t size, uint32_t burst_bytes)
{
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    /* 位宽由 burst/地址/长度自动选最大对齐值, 突发必须是 2^n 字节 */
    if (dma_start_memcpy(ptr, ch, dst, src, size, burst_bytes) != status_success) {
        return false;
    }
    while (1) {
        uint32_t st = dma_check_transfer_status(ptr, ch);
        if (st & DMA_CHANNEL_STATUS_ERROR) { return false; }
        if (st & DMA_CHANNEL_STATUS_TC)    { break; }
    }
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    printf("burst=%lu B  %.1f MB/s\r\n", (unsigned long) burst_bytes,
           (double) size / ((double) ticks * 1e6 / 24000000.0));
    return true;
}
```

这段代码解决"如何在 µs 级准确测一次 DMA 搬移"的问题：计时只用 mchtmr，不用
`bsp_uptime_ms()`（1 ms 分辨率不够）；搬完立刻用 `memcmp()` 校验，避免"带宽很高但数据错了"。

### 19.6.2 例程 B：`samples/dma/dma_uart_rx_idle/`（UART + DMA + IDLE 收帧）

| 项 | 内容 |
|---|---|
| 路径 | `samples/dma/dma_uart_rx_idle/` |
| 覆盖特性 | DMAMUX 请求路由、UART TX/RX DMA（handshake）、RX 空闲（IDLE）检测与中断、内部 modem 回环自测 |
| 关键配置 | UART4 115200 8N1（PC16/PC17）、idle 阈值 10 bit、HDMA 通道 0/1、帧长 1~64 B 变化 |
| 实测现象 | **IDLE 帧界识别 20/20（loss=0 ppm）**；TX DMA 每帧 avg 470 µs；TX→IDLE 延迟 min 166/avg 2388/max 5038 µs；**RX DMA 数据异常**（见易错点 6） |
| 实测日志 | `logs/20261030-123512-dma_uart_rx_idle.log` |

关键代码（帧界检测 + DMA 发）：

```c
/* RX 空闲中断：一帧结束（线上高电平超过 10 bit）时进入 */
SDK_DECLARE_EXT_ISR_M(BOARD_APP_UART_IRQ, isr_uart_idle)
void isr_uart_idle(void)
{
    if (uart_is_rxline_idle(BOARD_APP_UART_BASE)) {
        uart_clear_rxline_idle_flag(BOARD_APP_UART_BASE);
        s_idle_tick = mchtmr_get_count(HPM_MCHTMR);   /* 用于测 TX->IDLE 延迟 */
        s_idle_seen = true;
        s_idle_cnt++;                                  /* 帧计数 = 帧界识别 */
    }
}

/* TX 用 DMA(handshake): 内存 -> UART THR, DMAMUX 把 UART4_TX 请求接到 HDMA 通道 1 */
dmamux_config(HPM_DMAMUX, TX_DMAMUX_CH, HPM_DMA_SRC_UART4_TX, true);
dma_default_handshake_config(HPM_HDMA, &cfg);
cfg.ch_index = 1; cfg.src = (uint32_t) buf; cfg.dst = (uint32_t) &UART4->THR;
cfg.dst_fixed = true; cfg.size_in_byte = len; cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
dma_setup_handshake(HPM_HDMA, &cfg, true);
```

表 19-7 UART4 + DMA + IDLE 实测（logs/20261030-123512）：

| 指标 | 实测值 | 说明 |
|---|---|---|
| 内部 modem 回环自检 | OK | 发 1 B 轮询收到同样字节 |
| 帧界识别（IDLE 中断） | **20/20，loss=0 ppm** | 帧间 1 ms 空闲足够分开 |
| TX DMA 每帧耗时 | avg 470 µs / max 2085 µs | 含每帧重建通道开销 |
| TX 开始 → IDLE 延迟 | min 166 / avg 2388 / max 5038 µs | 与帧长成正比 |
| RX DMA 数据正确率 | **0/20**（异常，见易错点 6） | 轮询对照同帧可读到正确字节 |

### 19.6.3 命令一览

```text
dmainfo                      # 控制器能力/缓冲/计时器
dmasweep [bytes]             # 突发扫描 (两个控制器)
dmawidth [bytes]             # 显式位宽 x 突发 矩阵
dmahb                        # HDMA 在 AHB SRAM 内部的带宽
dmachain                     # 3 段链式描述符
```

## 19.7 上板实测数据

### 19.7.1 XDMA 突发扫描（16 KB 拷贝，non-cacheable AXI SRAM）

表 19-3 XDMA 突发长度与带宽（logs/20261030-110400-dma_mem2mem.log）：

| 突发 | 16 KB 耗时 | 带宽 | 说明 |
|---|---|---|---|
| 8 B | 321.29 µs | 51.0 MB/s | 每 2 个 4B 突发一次总线事务，开销大 |
| 32 B | 97.00 µs | 168.9 MB/s | 单调上升 |
| 128 B | 42.46 µs | 385.9 MB/s | 接近平台 |
| 512 B | 40.75 µs | 402.1 MB/s | 进入平台区 |
| 2048 B | 40.25 µs | 407.1 MB/s | 峰值 |
| 8192 B | 40.33 µs | 406.2 MB/s | 平台区 |

### 19.7.2 HDMA 与 AHB SRAM

表 19-4 HDMA 在不同目标存储上的带宽（同上日志 + logs/20261030-110512）：

| 目的存储 | 突发 | 带宽 | 结论 |
|---|---|---|---|
| AXI SRAM（16 KB） | 8 B | 24.1 MB/s | 桥接开销 |
| AXI SRAM（16 KB） | 64 B | 117.7 MB/s | HDMA 访问 AXI 的最佳点 |
| AXI SRAM（16 KB） | 128 B | 25.9 MB/s | **异常掉速，见易错点 2** |
| AXI SRAM（16 KB） | 512 B | 25.9 MB/s | 同上 |
| AHB SRAM（8 KB） | 64 B | 305.3 MB/s | HDMA 的主场 |
| AHB SRAM（8 KB） | 512 B | 327.7 MB/s | HDMA 峰值 |

![图 19-2 DMAv2 实测带宽对比](figures/19/fig-19-2-dma-bandwidth.png)

如图 19-2 所示，同一颗芯片上"放哪儿 + 多大突发"能让带宽相差 **15 倍**（26 → 407 MB/s）。
这不是 DMA 本身慢，而是总线拓扑与桥的限制。

### 19.7.3 显式位宽矩阵与链式

表 19-5 XDMA 位宽 × 突发（4096 B 拷贝，explicit 配置）：

| 位宽 | 突发 | 带宽 |
|---|---|---|
| 1 B | 1 T | 7.2 MB/s |
| 1 B | 256 T | 85.9 MB/s |
| 4 B | 32 T | 234.6 MB/s |
| 8 B | 64 T | 348.6 MB/s |
| 8 B | 256 T | 348.6 MB/s |

链式 3 段 × 4 KB：25.33 µs 完成 12 KB，折算 **485.1 MB/s**，`memcmp` 通过。
描述符装载开销可以忽略（3 段与单段同量级）。

### 19.7.4 ILM / DLM 的 DMA 带宽与地址映射（2026-11-02）

`core_local_mem_to_sys_address()` 在 HPM5E31 上是**恒等函数**（`soc/HPM5E00/HPM5E31/hpm_misc.h` 里定义了
`CORE0_ILM_SYSTEM_BASE=0x011C0000` / `CORE0_DLM_SYSTEM_BASE=0x011E0000`，但 helper 直接返回原地址）。
所以 ILM/DLM 到底能不能被 DMA 访问、用哪个地址，只能实测。结果见表 19-6（8 KB 拷贝、请求 512 B 突发、XDMA）。

表 19-6 不同存储组合与地址别名的 DMA 带宽（logs/20261102-123937-dma_mem2mem-r5c.log）

| 源 → 目的 | 地址 | 带宽 | 结论 |
|---|---|---|---|
| AXI_SCC → AXI_SCC | 0x01230000 → 0x01234000 | 143.4 MB/s | 8 KB 时只有 143，**16 KB 才是 402**（固定开销摊薄） |
| AXI_SCC → ILM | 0x01230000 → 0x00002858（本地） | 334.4 MB/s | DMA 可直接访问 ILM 本地地址 |
| AXI_SCC → ILM | 0x01230000 → 0x011C2858（系统别名） | 334.9 MB/s | ILM 双映射，两种地址等价 |
| ILM → AXI_SCC | 0x00000858 → 0x01234000 | **48.1 MB/s** | **读 ILM 只有写的 1/7** |
| ILM → ILM | 0x858 → 0x2858 | **39.3 MB/s** | 同上 |
| AXI_SCC → DLM | 0x01230000 → 0x00202000（本地） | 333.8 MB/s | DLM 本地地址可被 DMA 访问 |
| AXI_SCC → DLM | 0x01230000 → 0x013E2000（别名） | **FAIL（等待超时）** | **DLM 没有 0x011E0000 这个系统别名** |
| DLM(别名) → AXI | 0x013E0000 → … | FAIL | 同上 |
| HDMA AXI → ILM | 0x01230000 → 0x2858 | 21.1 MB/s | HDMA 访问 ILM 很慢，别这样用 |

三个可用的结论：

1. **DMA 能直接进出 ILM/DLM**，用本地地址即可（0x00000000/0x00200000），不需要 `core_local_mem_to_sys_address()`
   做转换——因为它是恒等函数。ILM 的 0x011C0000 是额外的系统总线别名（等效）；DLM 的 0x011E0000 别名
   在本板**不通**（3 个组合全部等待超时）。
2. **ILM/DLM 写快读慢**：写 334 MB/s、读 39~48 MB/s，差 7~8 倍。把"只写一次、外设/DMA 读多次"的缓冲放
   ILM/DLM 是错的；反过来，高频写日志/计数放那里合适。
3. **小于 4 KB 的 DMA 拷贝被固定开销吃掉**：同样 512 B 突发，8 KB 只有 143 MB/s，16 KB 到 402 MB/s。
   驱动 API 之外每次搬运还有通道配置 + TC 轮询的几十微秒，选型时按"≥16 KB 才值得上 DMA"来估。

### 19.7.5 突发没有 96 B：非 2^n 组合全部被驱动拒绝（2026-11-02）

"HDMA burst 96 B" 这种非 2^n 组合在 `dma_start_memcpy()` 里直接判死（`hpm_dmav2_drv.c`：
`count_set_bits(burst_len_in_byte) > 1 || burst_len_in_byte & 0x1` → `status_invalid_argument`）。实测：

表 19-10 非 2^n 突发的返回码（16 KB 拷贝，burst 参数直接传字节数）

| 请求突发 | 结果 | 返回码 |
|---|---|---|
| 48 B / 96 B / 192 B / 384 B / 768 B / 1536 B | 全部 REJECT | **2 = status_invalid_argument** |

硬件层面突发计数就是 2^n 个传输，没有 96 B 这一档；要做到等效 96 B，只能拆成 64 B + 32 B 两次搬。

有效突发的实测曲线（表 19-8）：HDMA 在 64 B 达到 117.7 MB/s，128 B 以上掉到 26 MB/s（跨桥代价，见易错点 2）；
XDMA 从 8 B 的 57.5 单调升到 2048 B 的 407.1，之后平台化。

表 19-8 HDMA / XDMA 有效突发带宽（16 KB 拷贝，logs/20261102-123624-dma_mem2mem-r5b.log）

| 突发 | 8 B | 16 B | 32 B | 64 B | 128 B | 256 B | 512 B | 2048 B | 8192 B |
|---|---|---|---|---|---|---|---|---|---|
| HDMA (MB/s) | 24.1 | 58.6 | 88.4 | **117.7** | 25.9 | 26.0 | 26.0 | — | — |
| XDMA (MB/s) | 57.5 | 102.8 | 168.9 | 271.0 | 385.9 | 396.0 | 401.7 | **407.1** | 406.6 |

### 19.7.6 CPU 与 DMA 并发：单核运算不影响 DMA 带宽（2026-11-02）

同一显式配置（4 B 宽 × 16 T = 64 B 突发，16 KB）下测三组，CPU 负载在 DMA 搬运期间持续运行：

表 19-9 CPU 并发下的 XDMA 带宽

| 场景 | 带宽 | 相对空载 |
|---|---|---|
| 只有 DMA（CPU 只轮询完成位） | 202.8 MB/s | — |
| DMA + CPU 纯寄存器运算（LCG，不占总线） | 204.7 MB/s | **+0.9%（噪声内）** |
| DMA + CPU 连续读 AHB SRAM（占总线） | 203.6 MB/s | **−0.4%（噪声内）** |

**CPU 单核跑满不影响 DMA 带宽。** 第一版实验报出的"下降 49.8%"是假象：空载基线用的是
`dma_start_memcpy()` 自动选的 1024 B 突发（405.4 MB/s），负载组用的显式 64 B 突发，差的是突发长度不是 CPU 争用。
这个易错点值得记：**做并发对比必须锁住其他变量**。

![图 19-3 DMA 带宽与存储位置/CPU 并发的关系](figures/19/fig-19-3-dma-mem-bw.png)

如图 19-3 所示，左边是同一 8 KB 拷贝换存储位置后的带宽（ILM 写快读慢、DLM 别名不通），
右边是 CPU 并发前后几乎不变的柱状对比。

## 19.8 性能与调优

1. **先定位存储**：内存 ↔ 内存用 XDMA；外设 ↔ 内存用 HDMA；HDMA 的缓冲尽量放 AHB SRAM。
2. **突发尽量大**：XDMA≥512 B、HDMA（AXI 目标时）≤64 B、HDMA（AHB 目标时）≥64 B。
3. **位宽用 4/8 B**：1 B 位宽即使 256T 也只有 85 MB/s。
4. **避免 cache 维护**：缓冲放 non-cacheable 区，比每次 flush/invalidate 便宜。
5. **链式减少中断**：多段拼接用描述符，TC 中断只来一次。
6. **CPU 争抢可以忽略**：同一突发配置下 CPU 纯运算/连续读 AHB 时 XDMA 带宽变化 <1%（表 19-9）。
   真正影响带宽的是"突发长度 + 源/目的在哪个存储"，先把这两个定下来再谈调优。

## 19.9 易错点与注意事项

1. **现象**：`dma_start_memcpy()` 返回 2（`status_invalid_argument`）。
   **原因**：突发不是 2^n 字节，或超过 `位宽上限 × 突发上限`（HDMA 是 4 B × 128 T = 512 B）。
   **解决**：按控制器能力裁剪突发；本仓库 `dmahb` 命令里 1024 B 以上突发会被拒绝，属正常。
2. **现象**：HDMA 搬 AXI SRAM，突发从 64 B 提到 128 B 后带宽从 117.7 掉到 25.9 MB/s（4.5 倍）。
   **原因**：HDMA 在 AHB 侧，AXI SRAM 在另一侧，ABH→AXI 桥对大突发的拆分效率骤降。
   **解决**：HDMA 访问 AXI SRAM 时突发 ≤64 B，或改用 XDMA / 把缓冲放 AHB SRAM。
3. **现象**：链式描述符 `dma_config_linked_descriptor()` 返回失败。
   **原因**：每段 `size_in_byte` 不能被"位宽 × 突发"整除（本仓库最初用 16384/3 = 5461 B，遇到）。
   **解决**：段长取突发整数倍（改成 16384/4 = 4096 B 后通过）。
4. **现象**：CPU 读到的目的缓冲与源不一致。
   **原因**：缓冲在 cacheable 区且读之前没 invalidate。
   **解决**：放 non-cacheable 区，或读前 `l1c_dc_invalidate()`。
5. **现象**：测量带宽抖动大（±20%）。
   **原因**：用 ms 级定时器或测量窗口太短（<10 µs 时启动开销占比大）。
   **解决**：搬运 ≥8 KB，用 mchtmr，重复 3 次取最快。

## 19.10 优劣评价与同类对比

表 19-11 与同类 MCU 的 DMA 能力对比（数据来源：各厂商公开数据手册，非本板实测）：

| 项 | HPM5E31（本板实测） | STM32H7 DMA | TC397 DMA |
|---|---|---|---|
| 控制器数量 | 2（XDMA+HDMA） | 2（DMA1/2 + BDMA） | 1（多通道） |
| 通道数 | 16+16 | 8+8+8 | 16 |
| 最大位宽 | 8 B | 4 B | 8 B |
| 链式描述符 | 支持 | 支持 | 支持 |
| 内存带宽（实测） | 407 MB/s（AXI SRAM） | 典型 200~400 MB/s（依赖 AXI 频率） | 典型 300~500 MB/s |
| DMAMUX 灵活路由 | 64 输出 | 有 DMAMUX | 有 |

优势：双控制器分工清晰、DMAMUX 路由灵活、驱动 API 统一；
劣势：HDMA 跨到 AXI SRAM 时有明显突发敏感性问题，文档里不显眼（本仓库实测才发现）。

## 19.11 参考来源

**上游链接（2026-11-02 核对）**

- HPM SDK 源码（DMAv2 驱动 @@hpm_dmav2_drv.c@@ / 例程）：<https://github.com/hpmicro/hpm_sdk>
- HPM5E00 数据手册（总线/存储映射）：<https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf>


1. RM §33「DMA 控制器」（p.561，XDMA/HDMA 架构与 DMAMUX 路由）、§33 链式传输（p.565）。
2. RM §33 DMAMUX 寄存器（p.584，`DMAMUX base 0xF00C4000`）。
3. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_feature.h:70-74`（位宽/突发/DMA 通道数）。
4. `hpm_sdk/drivers/src/hpm_dmav2_drv.c:147`（`dma_start_memcpy()` 位宽自动选择逻辑）。
5. `hpm_sdk/drivers/inc/hpm_dmav2_drv.h:120`（`dma_channel_config_t` 字段）。
6. 例程：`samples/dma/dma_mem2mem/`。
7. 实测日志：`logs/20261030-110400-dma_mem2mem.log`、`logs/20261030-110512-dma_mem2mem.log`。

## 19.12 扩展阅读

- [HPM SDK DMA 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/drivers/dma)
- [HPM SDK DMAv2 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/drivers/dmav2)
- 本仓库第 05 章（UART 空闲中断 + DMA 收帧）、第 07 章（SPI + DMA）、第 18 章（ADC 序列 + DMA）。

## 19.13 待确认项与风险

> 已逐条复核（2026-11-02）：本节每条都有实测数据、定位结论，或"已知限制 + 原因 + 验证方法"；不再保留裸"待确认/未做"条目。

| 项 | 状态 | 验证方法 |
|---|---|---|
| DLM/ILM 的 DMA 带宽 | **已实测（2026-11-02）**：写 ILM/DLM 334 MB/s、读 ILM 39~48 MB/s；ILM 双映射(0x0 与 0x011C0000 等价)、DLM 的 0x011E0000 别名不通；`core_local_mem_to_sys_address()` 是恒等函数 | `logs/20261102-123937-dma_mem2mem-r5c.log`（`dmamem`） |
| CPU 与 DMA 并发时的带宽下降比例 | **已实测（2026-11-02）**：同配置下 202.8→204.7（ALU）/203.6（读 AHB）MB/s，变化 <1%，单核 CPU 不抢 DMA 带宽 | `logs/20261102-123937-dma_mem2mem-r5c.log`（`dmacpu`） |
| HDMA 死区（burst 96 B 等非 2^n） | **已实测（2026-11-02）**：48/96/192/384/768/1536 B 全部被 `dma_start_memcpy()` 拒绝（返回 2 = invalid_argument），突发只能是 2^n；等效 96 B 需拆 64+32 | 表 19-10（`dmaburst`） |
| 外设 DMA 请求（UART/SPI/ADC）的端到端带宽 | **已实测（引用章节实测）**：UART4 @921600 = 85.79 KB/s（波特率上限，第 24 章表 24-1）；SPI 主机 DMA @20 MHz = 2490 KB/s、DMA 效率 99%（第 07 章 tp-bus）；ADC16 PWM 触发 + DMA 连续采样 ≈ 1 MSPS × 2 B ≈ 2 MB/s（第 18 章 5.2.1） | 见第 5/7/18/24 章日志 |
| UART RX DMA 伪数据根因 | **已定位（2026-11-02，代码级）**：RX FIFO 超时事件必须由 `FCRR.TMOUT_RXDMA_DIS` 屏蔽，否则超时会给 DMAMUX 多发一次请求、把上一帧尾巴当新帧；SDK `hpm_uart_drv.c` 已置位。手写寄存器漏置位就会出伪数据 | 验证：`samples/dma/dma_uart_rx_idle` 跑 1024 B × 1000 次，置位前后比较末字节；代码见 `drivers/src/hpm_uart_drv.c` |
