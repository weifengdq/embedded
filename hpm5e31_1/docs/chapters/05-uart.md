# 第 5 章 UART 与串口通信

## 5.1 文档信息


表 5-1 UART 关键特性

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 对应例程 | `samples/uart/uart_loopback`、`samples/uart/uart_irq_ring`、`samples/uart/uart_dma` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（RM + 上板实测，含回环与 MCR 内部 LOOP 两种验证） |

## 5.2 本章导览

- 学到什么：UART 的**波特率误差公式**（80 MHz 源时钟下 921600 实际只有 909090）、
  FIFO/中断/环形缓冲/DMA 四种接收方式的取舍、以及"没有接线也能自证"的 **MCR 内部回环**技巧。
- 前置章节：第 03 章（命令行框架）、第 04 章（IOC 引脚复用）。

## 5.3 概述与典型应用

HPM5E00 有 8 个 UART（RM §47.1，PDF p.816）。本板 UART0 是控制台，UART4（PC16/PC17）留给应用，
板级与 `hpm5e00evk` 一致，所以可以直接复用 SDK 例程的引脚定义。

典型应用：调试控制台、AT 指令模组、Modbus RTU、RS-485 半双工、与上位机协议通信、日志输出。

关键特性（RM §48.1，PDF p.818）：


表 5-2 UART 相关寄存器与驱动 API 对照

| 特性 | 参数 | 本板实测/配置 |
|---|---|---|
| 数据位 | 5~9 位 | 8 |
| 停止位 | 1 / 1.5 / 2 | 1 |
| 校验 | 奇/偶/粘位 | 无 |
| FIFO | **32 字节 TX + 32 字节 RX** | 实测 `uart_get_fifo_size()=32` |
| 波特率 | 独立波特率时钟，OSC 8~32 偶数，DIV 16 位 | 源时钟 80 MHz |
| 过采样 | `OSCR` 寄存器 | 921600 时 OSC=8, DIV=11 |
| DMA | 支持（FCR.DMAE + DMAMUX） | uart_dma 例程实测 |
| 硬件流控 | 支持 | 本板未接线 |
| RX IDLE 检测 | 支持 | 未用（样例用 FIFO 触发级） |

## 5.4 工作原理

### 5.4.1 数据通路

图 5-1 给出四种接收路径：轮询 LSR.DR、FIFO 触发中断 + 环形缓冲、DMA 搬运，以及发送侧的对称路径。

![图 5-1 UART FIFO/中断/DMA 数据通路](figures/05-uart/fig-05-1-uart-dataflow.png)

- 发送：写 `THR` → TX FIFO（32B）→ 移位寄存器 → TXD 引脚。
  `THR` 有空间时 `LSR.THRE` 置 1；**注意 THRE 只表示 FIFO/保持寄存器空，不代表最后一个停止位已发完**，
  真正发完看 `LSR.TEMT`。
- 接收：RXD → 移位寄存器 → RX FIFO（32B）→ 软件读 `RBR`。
  FIFO 达到触发级（`FCR.RFIFOT4`）产生中断；超时也会产生 RX timeout 中断。
- 波特率：`baud = src_clk / (OSC × DIV)`，OSC 取 8~32 的偶数，DIV 为 16 位。

### 5.4.2 波特率误差公式与 921600 的真相

驱动按"最接近的 divisor"选参数，容差 3%（`hpm_uart_drv.c:16-22` 的 `HPM_UART_BAUDRATE_TOLERANCE`）。
本板 UART 源时钟实测 **80 MHz**，于是 921600 只能取 `OSC=8, DIV=11`：

```text
实际波特率 = 80 000 000 / (8 × 11) = 909 090.9
误差        = (909090.9 - 921600) / 921600 = -1.3574% = -13574 ppm
```

这个误差对 UART 本身完全可用（<3%），但有几个连带影响：
1. 与 USB-UART 桥（CH343 等）通信时是"两个都有偏差"的叠加，长包更易出错；
2. 若上位机严格按 921600 计算超时（如 Modbus 3.5 字符间隔），会偏差 1.36%；
3. 反过来，**要求精确 921600 时应改 UART 源时钟**（8 位分频 + 偶数过采样约束下，本板 80 MHz
   在 1 M / 2 M / 4 M / 5 M 这些点上是精确的，见表 5-2）。

![图 5-2 80 MHz 源时钟下各标称波特率的实际误差](figures/05-uart/fig-05-2-baud-error.png)

### 5.4.3 内部回环（MCR.LOOP）：没有线也能自证

UART 的 `MCR` 寄存器有 `LOOP` 位（`hpm_uart_drv.h:350` 的 `uart_modem_enable_loopback()`）。
置 1 后控制器**内部把 TX 接到 RX**，外部引脚不再输出数据。这样单板就能验证：
发送通路、接收通路、FIFO、中断、DMA、波特率精度——全部不需要杜邦线。
本章三个例程都基于这个技巧；代价是**看不到真实引脚波形**，波形验证需示波器（见第 11 节）。

## 5.5 关键寄存器与驱动 API 对照


表 5-3 uart_loopback 例程信息

| 功能 | 寄存器 | 位域 | 驱动 API | 例程 |
|---|---|---|---|---|
| 数据位/停止位/校验 | `LCR` | `WLS/STB/PEN/EPS` | `uart_init()` | 本仓全部例程 |
| 波特率分频 | `OSCR/DLL/DLM` | `OSC/DIV` | `uart_init()`、`uart_set_baudrate()` | `uart_loopback` |
| FIFO 使能/触发级 | `FCR`（或 `FCRR`） | `FIFOE/RFIFOT4/TFIFOT4/DMAE` | `uart_init()` | `uart_irq_ring` |
| 收发状态 | `LSR` | `DR/THRE/TEMT/OE/FE` | `uart_check_status()` | `uart_loopback` |
| 读数据 | `RBR` | — | `uart_read_byte()` | `uart_irq_ring` |
| 写数据 | `THR` | — | `uart_write_byte()` | 全部 |
| 中断使能 | `IER` | RX avail / TX slot | `uart_enable_irq()` | `uart_irq_ring` |
| 中断识别 | `IIR` | irq id | `uart_get_irq_id()` | `uart_irq_ring` |
| 内部回环 | `MCR` | `LOOP` | `uart_modem_enable_loopback()` | `uart_loopback` |
| DMA 请求 | `FCR.DMAE` + DMAMUX | — | `dmamux_config()`、`dma_setup_handshake()` | `uart_dma` |

## 5.6 例程逐条讲解

### 5.6.1 uart_loopback：波特率误差 / 完整性 / FIFO / 吞吐


表 5-4 uart_irq_ring 例程信息

| 项 | 内容 |
|---|---|
| 路径 | `samples/uart/uart_loopback/` |
| 覆盖特性 | MCR 内部回环、波特率误差表、数据完整性、FIFO 溢出、回环吞吐 |
| 关键配置 | UART4、PC16/PC17、FIFO 32B、源时钟 80 MHz、8N1 |
| 实测现象 | 921600 实测 909081（-13583 ppm）；1024 B 回环 0 错误；吞吐 90 KB/s |
| 实测日志 | `logs/20261030-111055-uart_loopback.log` |

测量波特率的关键是**等 TEMT 而不是 THRE**：

```c
static int measure_baud(uint32_t baud, uint32_t *meas, uint32_t *reg)
{
    if (uart_setup(baud, true) != status_success) { return -1; }
    u_drain_rx();
    uint32_t n = baud / 200UL;                       /* 让 10 bit/byte 的总时间约 50 ms */
    if (n < 256U) n = 256U;
    if (n > 4096U) n = 4096U;
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) { u_putc(0x55U); }
    u_wait_temt();                                   /* 关键: 等最后一个停止位发完 */
    uint64_t dt = cyc() - t0;
    *meas = (uint32_t)(((uint64_t)n * 10ULL * s_cpu_hz) / dt);
    *reg = uart_reg_baud(T_UART, s_src_hz, NULL, NULL);
    u_drain_rx();
    return 0;
}
```

如果只等 `THRE`，会少算最后 10 bit，N 较小时产生几千 ppm 的假误差——这是本章第一个易错点。

### 5.6.2 uart_irq_ring：中断 + FIFO + 环形缓冲


表 5-5 uart_dma 例程信息

| 项 | 内容 |
|---|---|
| 路径 | `samples/uart/uart_irq_ring/` |
| 覆盖特性 | RX 中断、FIFO 触发级、256B 环形缓冲、溢出计数 |
| 关键配置 | RX 触发级 = not_empty（≥1 字节）、IRQ 优先级 2 |
| 实测现象 | burst 128 B：ISR=128、verified=128、errors=0、89 KB/s；burst 512 B：ISR=512、verified=512、errors=0、90 KB/s |
| 实测日志 | `logs/20261030-113457-uart_irq_ring.log` |

### 5.6.3 uart_dma：HDMA 收发 + 吞吐表


表 5-6 80 MHz 源时钟下的波特率误差实测

| 项 | 内容 |
|---|---|
| 路径 | `samples/uart/uart_dma/` |
| 覆盖特性 | DMAMUX 映射、TX/RX 双向 DMA、DMA 通道握手与 TC 等待 |
| 关键配置 | TX=HDMA ch2、RX=HDMA ch3、1 字节宽度、缓冲区 NONCACHEABLE |
| 实测现象 | 1024 B @921600 用时 11266 µs、0 错误、90 KB/s |
| 实测日志 | `logs/20261030-112250-uart_dma.log` |
本章表格一览：表 5-1 UART 关键特性；表 5-2 UART 相关寄存器与驱动 API 对照；表 5-3 uart_loopback 例程信息；表 5-4 uart_irq_ring 例程信息；表 5-5 uart_dma 例程信息；表 5-6 80 MHz 源时钟下的波特率误差实测；表 5-7 轮询/中断/DMA 对比；表 5-8 UART 与同类 MCU 横向对比；表 5-9 参考来源索引；表 5-10 待确认项与风险。

## 5.7 上板实测数据

### 5.7.1 波特率误差表（源时钟 80 MHz，UART4 内部回环）

表 5-2：`uart_err` 命令输出（`logs/20261030-111055-uart_loopback.log`）。


表 5-7 轮询/中断/DMA 对比

| 请求波特率 | 回读分频算得 | 实测（mcycle 反推） | 回读误差 | 实测误差 |
|---|---|---|---|---|
| 9600 | 9596 | 9596 | -416 ppm | -416 ppm |
| 38400 | 38461 | 38460 | +1588 ppm | +1562 ppm |
| 115200 | 114942 | 114940 | -2239 ppm | -2256 ppm |
| 460800 | 454545 | 454541 | -13574 ppm | -13582 ppm |
| **921600** | **909090** | **909081** | **-13574 ppm** | **-13583 ppm** |
| 1500000 | 1481481 | 1481459 | -12346 ppm | -12360 ppm |
| 2000000 | 2000000 | 1999961 | 0 ppm | -19 ppm |
| 3000000 | 3076923 | 3076820 | +25641 ppm | +25606 ppm |
| 4000000 | 4000000 | 3999846 | 0 ppm | -38 ppm |
| 5000000 | 5000000 | 4999769 | 0 ppm | -46 ppm |

两个结论：
1. **"回读分频"和"实测"两种方法互相印证**（差值 <50 ppm），说明测量方法可靠；
2. **3 Mbps 请求会得到 3.077 Mbps（+2.56%）**，虽然仍在 3% 容差内，但对外设通信风险明显高于 921600。

### 5.7.2 FIFO / 中断 / DMA 对比（同一块板、同一 UART4 回环）


表 5-8 UART 与同类 MCU 横向对比

| 方式 | 单次传输 | 吞吐 | 错误 | CPU 占用特征 | 出处 |
|---|---|---|---|---|---|
| 轮询（逐字节 send/receive） | 1024 B | 90 KB/s（理论 92 的 97%） | 0 | 100% 忙等 | `logs/20261030-111055-uart_loopback.log` |
| 中断 + 环形缓冲 | 128 B | 92 KB/s（含校验/打印） | 0 | ISR 每字节一次 | `logs/20261030-113457-uart_irq_ring.log` |
| DMA + 回环 | 1024 B @921600 | 90 KB/s | 0 | 几乎为 0 | `logs/20261030-112250-uart_dma.log` |
| DMA 吞吐表 | 2048 B | 11 / 45 / 90 / 199 / 307 KB/s | 0 | 0 | 同上（115200/460800/921600/2M/3M） |

FIFO 溢出实测（`uart_fifo` 命令）：连续发 96 字节不读，只收到 33 字节（32 B FIFO + 1 B 移位寄存器），
其余溢出——这就是必须上中断/DMA 的直接证据。

### 5.7.3 中断方式的量化数据

`uart_irq_ring` 上电 burst 128 字节：`isr=128、rx_bytes=128、ring_drop=0`，
即 RX 触发级 `not_empty` 下**每字节一次中断**。把触发级改成半满后中断次数降到 1/16（表 5-11）。

### 5.7.4 FIFO 触发级对中断次数的影响（R5 实测）

同一份 ISR、同一个 128 B 内部回环突发，只改 `cfg.rx_fifo_level` 重新 `uart_init()`：

表 5-11 RX 触发级与中断次数（921600 8N1，128 B）

| 触发级 | 枚举值 | 中断次数 | 每多少字节一次中断 | 校验 |
|---|---|---|---|---|
| not_empty（1 字节） | `uart_rx_fifo_trg_not_empty` | 128 | 1 | 0 错误 |
| gt_half（16 字节） | `uart_rx_fifo_trg_gt_half` | 8 | 16 | 0 错误 |

命令与日志：bash scripts/hwrun.sh uart/uart_irq_ring --secs 60 --send "uart_ring_trig 0;uart_ring_trig 2;uart_ring_trig 3;…"，
数据在 logs/20261102-121245-uart_irq_ring.log（trig 0 与 trig 2 两行）；trig 3（3/4 满）那次窗口被切断，没有取到数。

结论：传输 128 B 时，触发级从"非空"改成"过半"，ISR 次数从 128 降到 8，**正好 16 倍**，
与 32 B FIFO 的半满阈值一致。这就是 FIFO 触发级的实际价值：同样数据量下 CPU 中断负载降一个数量级。
代价是延迟：半满要等 16 字节才中断，最后一包不足 16 字节时靠 RX 超时中断收尾，
所以 `uart_intr_rx_data_avail_or_timeout` 里两种中断都要使能。

## 5.8 性能与调优

1. **吞吐上限 = baud/10**（8N1）。921600 理论 92 KB/s，实测 90 KB/s（97%），瓶颈在物理层；
   DMA 方式同样 90 KB/s（证明 CPU 不再是瓶颈）。
2. 想更快：提高波特率（本板可到 5 Mbps 且实测 0 错误，注意表 5-2 中 1M/2M/4M/5M 误差为 0）；
   或改用硬件流控 + 更大 FIFO 的外设。
3. 接收架构选择：
   - 命令/交互类低速率：`uart_irq_ring`（简单、可靠）；
   - 大块数据/高波特率：`uart_dma`（CPU 几乎 0 占用，注意 cache 与 DMA 地址）；
   - 低速率调试打印：轮询（`printf`）足够。
4. **`printf` 是阻塞的**：日志量决定实时性。实测 1 KB 日志约 11 ms（90 KB/s），
   中断里千万不要打印。
5. 错误处理：`LSR` 的 `OE/FE/PE/ERRF` 位读一次即清，必须在读数据后立刻取样，否则永远读到 0。

## 5.9 易错点与注意事项

1. **现象：测出的波特率比实际低几千 ppm** → 原因：用 `THRE` 判断发送结束，少算最后一个字节的 10 bit。
   → 解决：等 `TEMT`（本仓库 `u_wait_temt()`）。
2. **现象：`uart_send_byte()` 在低波特率下返回 `status_timeout`** → 原因：驱动内部有重试上限
   （`HPM_UART_DRV_RETRY_COUNT`）。→ 解决：低波特率用自定义阻塞写（本仓库 `u_putc()`），或加大重试上限。
3. **现象：921600 通信偶发丢包** → 原因：实际 909090（-1.36%），与对端误差叠加。
   → 解决：优先用误差小的点，或改 UART 源时钟；对端也校验一遍。
4. **现象：FIFO 模式下一次中断只收到部分字节** → 原因：ISR 里没有读空 FIFO 的 while 循环
   （`while (uart_check_status(T_UART, uart_stat_data_ready))`）。→ 解决：按本仓库 `isr_uart()` 的写法，读空为止。
5. **现象：开了 RX timeout 中断后 ISR 反复进入** → 原因：`uart_intr_rx_data_avail_or_timeout` 包含两种中断，
   ISR 没判断 `uart_get_irq_id()`。→ 解决：先判 id 再处理。
6. **现象：内部回环时示波器测 TXD 没有波形** → 原因：16550 语义下 `MCR.LOOP=1` 会断开外部引脚。
   → 解决：测波形时关 LOOP，改为外部短接 TX-RX。
7. **现象：DMA 收到的数据是旧值** → 原因：缓冲区在 cacheable 内存且没做 cache 维护。
   → 解决：用 `ATTR_PLACE_AT_NONCACHEABLE` 或传输前后做 invalidate/writeback。
8. **现象：PC16/PC17 配成 UART4 后程序跑飞** → 排查：PC16 还可复用为 `XPI0_CA_DQS`、`PPI0_DQ_16`。
   本板实测**不影响 XIP 启动**（UART4 例程全部正常运行）；但若板子改了 Flash 引脚组，必须重新核对（见待确认）。

## 5.10 优劣评价与同类对比


表 5-9 参考来源索引

| 维度 | HPM5E31 UART（实测） | STM32H7 USART | TI AM243 UART |
|---|---|---|---|
| FIFO | 32 B | 16 B（多数型号） | 128 B |
| 最高波特率 | 5 Mbps 实测无错（源 80 MHz） | 可达 10+ Mbps | 较高 |
| 过采样 | OSC 8~32 可配 + 16 位 DIV | 8/16 倍过采样 | 分数分频 |
| DMA | HDMA/XDMA 双控制器 | 有 | 有 |
| 硬件流控 | 有 | 有 | 有 |
| 内部回环 | MCR.LOOP（本章核心技巧） | 有（LOOP 位） | 有 |

结论：HPM5E00 的 UART 属于"够用且好用"：32 B FIFO 比 STM32H7 大一倍，DMA 能力完整，
**主要短板是源时钟 80 MHz 导致的常见波特率误差**，且没有独立的高精度波特率时钟可选。

## 5.11 参考来源

1. RM §47.1（PDF p.816）：通讯外设概述，8×UART、4×SPI、4×I2C
2. RM §48.1（PDF p.818）：UART 特性；32 字节 TX/RX FIFO、5~9 位、1/1.5/2 停止位、DMA、独立波特率时钟
3. RM §48.2.5（PDF p.819）：波特率控制，OSCR 过采样与 16 位分频器
4. SDK：`hpm_sdk/drivers/src/hpm_uart_drv.c:66-100`（波特率计算与 3% 容差）、`hpm_uart_drv.h:178-231`（配置结构）
5. SDK 例程：`hpm_sdk/samples/drivers/uart/uart_irq/src/uart.c`
6. 实测日志：`logs/20261030-111055-uart_loopback.log`、`logs/20261030-113457-uart_irq_ring.log`、
   `logs/20261030-112250-uart_dma.log`、`logs/20261030-110211-letter_shell_basic.log`
7. 相关章节：第 03 章（shell 输入环形缓冲）、第 19 章（DMA/DMAMUX）

## 5.12 扩展阅读

- HPM SDK UART 文档：`hpm_sdk/samples/drivers/uart/index_zh.rst`
- Modbus RTU 的 3.5 字符静默时间与波特率误差的关系，可结合表 5-2 自行推算。
- 第 07 章 SPI、第 06 章 I2C：同样是"先配 IOC、再配外设"的流程，可对照阅读。

## 5.13 待确认项与风险

> 已逐条复核（2026-11-02）。本章本轮补了 FIFO 触发级的实测对比（表 5-11）；3 Mbps 连续误码已补实测（18:04，512 KB 0 误码），921600 下 CH343 的丢包按"已知限制 + 原因 + 一条命令复跑"处理。

| 项 | 状态 | 说明 / 验证方法 |
|---|---|---|
| 921600 下真实引脚波形与上升沿质量 | 已知限制（无示波器） | 现有证据：回读分频 909090（-13574 ppm）、实测 909081（-13583 ppm），两者互证测量可靠（表 5-2）。测波形需关 MCR.LOOP 后示波器测 PC16 |
| FIFO 触发级从 not_empty 改为半满后中断次数下降比例 | **已实测（2026-11-02）** | 128 B 突发：not_empty 128 次中断，gt_half 8 次中断，正好 16 倍（表 5-11） |
| 3 Mbps（实际 3.077 Mbps）长时间误码率 | **已实测（2026-11-02 18:04）** | 512 KB 内部回环 @3 000 000 baud：verified=524 288 B、err=0、drop=0、overrun=0、isr=524 288，用时 1 750 ms、299 KB/s、BER 0（日志 `logs/20261102-180437-uart_irq_ring.log`）。原因为历史记录，保留复跑方法：例程已加 `uart_ring_ber` 命令，执行 `bash scripts/hwrun.sh uart/uart_irq_ring --secs 40 --send "uart_ring_ber 512 3000000"` 即内部回环发 512 KB 并逐字节校验，打印 err/drop/overrun 与吞吐 |
| 921600 与 CH343 的实际丢包率 | **已定位（2026-11-02）** | CH343（lsusb 1a86:55d3）下抓日志反复出现"打印字符丢失"，且固定掉同一个字符 r（`spi_ms_ber` 打成 `spi_ms_be`、`uart_ring_trig` 打成 `uart_ring_tig`），同时回环数据校验 0 错误 → 921600 + CH343 在连续输出方向已接近极限（-13574 ppm 叠桥片容差）。验证方法：换 FT232 或降到 460800 重跑同一批命令，统计掉字符数 |
| 源时钟改为 100 MHz 是否更优 | 已知限制（本轮未改时钟树） | 原因：改 UART 源时钟会影响同板其它成员正在测的串口日志。验证方法：改 clock.c 的 UART 时钟源后重跑 `uart_err`，比较 921600 的 ppm 误差（当前 -13574） |

## 5.14 参考来源补充（R5，上游/官方链接）

- HPM SDK 上游仓库（UART 驱动 `drivers/src/hpm_uart_drv.c`：波特率计算、FIFO 触发级、DMA）：
  [https://github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)（v1.13.0，BSD-3-Clause）
- HPM SDK 里的 UART 驱动源码（在线）：
  [https://github.com/hpmicro/hpm_sdk/blob/main/drivers/src/hpm_uart_drv.c](https://github.com/hpmicro/hpm_sdk/blob/main/drivers/src/hpm_uart_drv.c)
- HPM SDK 在线文档（例程与 API）：
  [https://hpm-sdk.readthedocs.io/zh-cn/latest/](https://hpm-sdk.readthedocs.io/zh-cn/latest/)
- CH343 数据手册（本板控制台 USB-UART 桥，沁恒官方）：
  [https://www.wch-ic.com/downloads/CH343DS1.PDF.html](https://www.wch-ic.com/downloads/CH343DS1.PDF.html)
- Linux termios 手册（8N1、波特率、VMIN/VTIME 的术语来源）：
  [https://man7.org/linux/man-pages/man3/termios.3.html](https://man7.org/linux/man-pages/man3/termios.3.html)
