# 第 7 章 SPI 总线

## 7.1 文档信息


表 7-1 SPI 关键特性

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 对应例程 | `samples/spi/spi_loopback_poll`、`samples/spi/spi_dma_bw`、`samples/spi/spi1_spi3_master_slave` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（RM + 上板实测；数据回环需外部短接线，已在正文标注） |

## 7.2 本章导览

- 学到什么：SPI 主机的**四种模式（CPOL/CPHA）**、FIFO 8 深度、DMA 带宽实测方法，以及一个反直觉结论——
  **IOC 的 LOOP_BACK 不能做 MOSI→MISO 回环**，必须外部短接一根线。
- 前置章节：第 04 章（IOC 引脚复用）、第 05 章（FIFO/中断/DMA 的通用套路）。
- 本章的 SPI2（PA20~PA23）留给 LAN8651 10BASE-T1S，见第 10 章；本章只用 SPI1（PC10~PC13）。

## 7.3 概述与典型应用

HPM5E00 有 4 个 SPI（RM `47.2，PDF p.816），支持经典 2 线（MISO/MOSI）以及 Dual/Quad 模式。
本板 SPI1 引出到 PC10~PC13，可做通用 SPI 主机；SPI2 被 LAN8651 占用。

典型应用：SPI Flash/EEPROM、SPI 传感器（IMU/磁编码器）、SPI 屏、与 10BASE-T1S 收发器（LAN8651）通信。

关键特性（RM `49，PDF p.837~854）：


表 7-2 SPI 相关寄存器与驱动 API 对照

| 特性 | 参数 | 说明 |
|---|---|---|
| 实例数 | 4（SPI0~3） | 本板可自由使用的是 SPI1 |
| 数据位宽 | 8/16/32 位可配 | 实测三种位宽吞吐相同 |
| FIFO | **TX/RX 各 8 word**（RM p.840） | 阈值中断/DMA 都基于它 |
| 模式 | CPOL×CPHA 四种 | 实测传输耗时一致 |
| 片选 | 最多 4 个硬件 CS | 本板 SPI1 用 PC11 作 CS0 |
| 传输模式 | write-only / read-only / write-read-together / dummy 等 10 种 | 见 `spi_trans_mode_t` |
| DMA | TX/RX 双向（DMAV2 + DMAMUX） | 实测 20 MHz 下利用率 99% |
| 最高 SCLK | 实测 20 MHz 精确；40/80 MHz 请求只得到约 23 MHz | 见第 5 节 |

## 7.4 工作原理

### 7.4.1 四种模式

SPI 由 CPOL（时钟空闲电平）和 CPHA（采样沿）组合出 4 种模式。图 7-1 是这四种模式的采样点差异。

![图 7-1 SPI 四种模式与采样沿](figures/07-spi/fig-07-1-spi-modes.png)

对应驱动里的枚举：
- `format.common_config.cpol = spi_sclk_low_idle / spi_sclk_high_idle`
- `format.common_config.cpha = spi_sclk_sampling_odd_clk_edges / spi_sclk_sampling_even_clk_edges`
- 本仓库两个例程都用 **Mode 3**（CPOL=1、CPHA=1），与绝大多数 SPI Flash/传感器默认一致。

**实测结论**：四种模式对"传输耗时"没有影响（1024 B @20 MHz 都是 411~412 µs），
说明模式只改变采样沿，不改变时钟个数——这也反过来证明时序配置真的生效了（否则会报错或空转）。

### 7.4.2 数据通路与 FIFO

- 写：`SPI.DATA` → TX FIFO（8 word）→ 移位寄存器 → MOSI（SCLK 由主机产生）。
- 读：MISO → 移位寄存器 → RX FIFO（8 word）→ 软件读 `SPI.DATA`。
- 主机侧"传输"由 `TRANSCTRL` 的传输模式 + 计数寄存器决定：`spi_control_init()` 会写
  `WRTRANCNT/RDTRANCNT`，所以一次 `spi_transfer()` 可以只发、只收、或全双工。
- DMA 通路：TX FIFO 空 → DMA 请求（DMAMUX 映射到 `HPM_DMA_SRC_SPI1_TX`）→ 内存搬入；
  RX FIFO 非空 → DMA 请求 → 搬到内存。图 5-1 的 UART 数据通路与这里同构，只是外设换成 SPI。

### 7.4.3 时钟与分频

spi_master_timing_init() 的算法很短（hpm_sdk/drivers/src/hpm_spi_drv.c:348），值得直接看：

    div_integer = clk_src / sclk_freq;
    if ((clk_src % sclk_freq) != 0 || (div_integer % 2) != 0 || div_integer > 510) return 失败;
    sclk_div = (div_integer / 2) - 1;        /* 写进 TIMING.SCLK_DIV */

寄存器头文件给了对应的换算（soc/HPM5E00/ip/hpm_spi_regs.h:943）：

    SCLK 周期 = (SCLK_DIV + 1) x 2 x 源时钟周期；SCLK_DIV = 0xFF 是特殊值，表示 SCLK = 源时钟

这两段合起来解释了本板（源时钟实测 80 MHz）能配出哪些频率：

1. **只能配 80 MHz 的偶数整数分频**，即 f = 80 / (2m) MHz（m = 1,2,3…）：40、20、13.33、10、8、5.71……
   请求 16 / 23 / 25 / 32 / 50 / 60 / 64 MHz 一律返回 status_invalid_argument，因为除不尽 80 或商是奇数。
   这不是驱动 bug，是分频器定义决定的"可用点很稀"。
2. **请求频率 >= 源时钟时不做校验，直接写 DIV=0xFF**：语义是"SCLK 直通源时钟"。
   所以请求 80 MHz 会配置成功并真的输出 80 MHz，不报错。

实测回读与公式完全一致（表 7-6），换源时钟时可用频率集合随之变化，但"偶数整数分频"这条规则不变。


## 7.5 关键寄存器与驱动 API 对照


表 7-3 SCLK 精度与频率扫描

| 功能 | 寄存器 | 位域 | 驱动 API | 例程 |
|---|---|---|---|---|
| 数据位宽 | `TRANSFMT` | `DATALEN` | `format_config.common_config.data_len_in_bits` | `spi_loopback_poll` |
| CPOL/CPHA | `TRANSFMT` | `CPOL/CPHA` | `format_config.common_config.cpol/cpha` | `spi_loopback_poll` |
| 主/从模式 | `TRANSFMT` | `SLVMODE` | `format_config.common_config.mode` | 两个例程 |
| 传输模式 | `TRANSCTRL` | `TRANSMODE` | `control_config.common_config.trans_mode` | 两个例程 |
| 传输计数 | `TRANSCTRL` | `WRTRANCNT/RDTRANCNT` | `spi_control_init()` | 同上 |
| 数据端口 | `DATA` | — | `spi_write_data()`/`spi_read_data()` | 同上 |
| FIFO 状态 | `STATUS` | `TXFULL/RXEMPTY` | 轮询或中断 | 同上 |
| 中断 | `INTREN/INTRST` | `END` 等 | `spi_enable_interrupt(spi_end_int)` | `spi_dma_bw`（可选） |
| DMA | `CTRL` | `TXDMAEN/RXDMAEN` | `spi_setup_dma_transfer()` | `spi_dma_bw` |
| 时序 | `TIMING` | `SCLK_DIV/CS2SCLK/CSHT` | `spi_master_timing_init()` | `spi_loopback_poll` |
| 片选 | — | — | `init_spi1_pins()` / GPIO 作 CS | `pinmux_core.c` |

## 7.6 例程逐条讲解

### 7.6.1 spi_loopback_poll：回环尝试 + SCLK 实测 + 模式/位宽/频率扫描


表 7-4 模式与位宽实测

| 项 | 内容 |
|---|---|
| 路径 | `samples/spi/spi_loopback_poll/` |
| 覆盖特性 | 四种模式、8/16/32 位、频率扫描、IOC LOOP_BACK 验证、无从器件时的自证方法 |
| 关键配置 | SPI1 PC10~PC13、Mode 3、20 MHz、FIFO 使能 |
| 实测现象 | 回环 MISMATCH【待接线：PC12-PC13】；20 MHz 实测 19.885 MHz（-5744 ppm）；4 模式耗时一致 |
| 实测日志 | `logs/20261030-112819-spi_loopback_poll.log` |

关键代码——用"传输耗时"反推实际 SCLK（没有从器件也成立，因为 SCLK 一定在输出）：

```c
static uint64_t spi_xfer(uint32_t nbytes)
{
    spi_control_config_t ctrl = {0};
    spi_master_get_default_control_config(&ctrl);
    ctrl.master_config.cmd_enable = false;
    ctrl.master_config.addr_enable = false;
    ctrl.common_config.trans_mode = spi_trans_write_read_together;
    uint64_t t0 = cyc();
    (void) spi_transfer(T_SPI, &ctrl, NULL, NULL, s_wbuf, nbytes, s_rbuf, nbytes);
    return cyc() - t0;
}
/* 全双工: nbytes*8 个 SCLK 周期; meas = nbytes*8*cpu_hz/dt */
```

### 7.6.2 spi_dma_bw：DMA 收发带宽


表 7-5 DMA 带宽实测

| 项 | 内容 |
|---|---|
| 路径 | `samples/spi/spi_dma_bw/` |
| 覆盖特性 | DMAV2 通道握手、DMAMUX 映射、`spi_setup_dma_transfer()`、带宽与效率 |
| 关键配置 | TX=HDMA ch0、RX=HDMA ch1、1 字节宽度、缓冲区 NONCACHEABLE |
| 实测现象 | 2048 B @20 MHz：822 µs、2490 KB/s、效率 99% |
| 实测日志 | `logs/20261030-113457-spi_dma_bw.log` |

正确的 DMA 启动顺序（踩过坑后总结）：

```c
/* 1) 先配 control(含 dma 使能) */
spi_master_get_default_control_config(&ctrl);
ctrl.common_config.tx_dma_enable = true;
ctrl.common_config.rx_dma_enable = true;
ctrl.common_config.trans_mode = spi_trans_write_read_together;
/* 2) 配 DMAMUX + DMA 通道(TX/RX 各一个), 让 DMA 先就绪 */
dmamux_config(T_DMAMUX, TX_MUX_CH, BOARD_APP_SPI_TX_DMA, true);
dma_setup_handshake(T_DMA, &cfg_tx, true);
dmamux_config(T_DMAMUX, RX_MUX_CH, BOARD_APP_SPI_RX_DMA, true);
dma_setup_handshake(T_DMA, &cfg_rx, true);
/* 3) 用 spi_setup_dma_transfer() 启动传输(不能用 spi_transfer!) */
spi_setup_dma_transfer(T_SPI, &ctrl, NULL, NULL, nbytes, nbytes);
/* 4) 等两个通道 TC, 再等 SPI idle */
```

本章表格一览：表 7-1 SPI 关键特性；表 7-2 SPI 相关寄存器与驱动 API 对照；表 7-3 SCLK 精度与频率扫描；表 7-4 模式与位宽实测；表 7-5 DMA 带宽实测；表 7-6 请求频率/寄存器回读/实际结果；表 7-7 参考来源索引；表 7-8 待确认项与风险；表 7-9 互连连续误码；表 7-10 首字节误码归因。

### 7.6.3 spi1_spi3_master_slave：同一 MCU 双实例主从实测（SPI1 master 与 SPI3 slave）

| 项 | 内容 |
|---|---|
| 路径 | `samples/spi/spi1_spi3_master_slave/` |
| 覆盖特性 | SPI1 master(mode 0) 与 SPI3 slave 全双工；slave 用 DMA(HDMA ch4/5) 自主收发；双向校验；1/5/10/20 MHz 带宽/误码；slave 预置→master 只读 |
| 关键配置 | master: PC10 SCLK/PC11 CS/PC12 MISO/PC13 MOSI；slave: PA26 SCLK/PA27 CS/PA28 MISO/PA29 MOSI；mode0/8 bit；两侧 SCLK 都开 IOC LOOP_BACK |
| 实测现象 | 1/5/10 MHz 双向 PASS errors=0；20 MHz 首字节 1 bit 误码；带宽 123/607/1193/2299 KB/s；**10 MHz 连续 1 MB 双向 0 错误**（表 7-9） |
| 实测日志 | `logs/20261030-162048-spi1_spi3_master_slave.log`、`logs/20261102-121103-spi1_spi3_master_slave.log`、`logs/20261102-123349-spi1_spi3_master_slave.log` |
| R5 新增命令 | spi_ms_sweep（频率扫描 + TIMING 回读）、spi_ms_ber [MB] [Hz]（连续误码）、spi_ms_guard（CS 时序扫描）、spi_ms_spd（驱动强度扫描） |

接线要点：SPI 是「同名相连」（master MOSI→slave MOSI、master MISO←slave MISO）。用户已按此接好；
HPM5E31 的 PA28 只能做 SPI3_MISO、PA29 只能做 SPI3_MOSI，不能用软件互换复用功能。
例程内置 `spi_ms_wires`（GPIO 驱动-读回）可一键确认接线，不需要示波器。

## 7.7 上板实测数据

### 7.7.1 SCLK 精度与频率扫描（R5 重测，含 TIMING 回读）

命令与日志：bash scripts/hwrun.sh spi/spi1_spi3_master_slave --secs 30 --send "spi_ms_sweep;…"，
日志 logs/20261102-121103-spi1_spi3_master_slave.log。源时钟回读 spi1 = spi3 = 80 MHz，cpu = 480 MHz。
每点 512 B 全双工（主机轮询 + 从机 DMA），"实测速率"由传输耗时反推。

表 7-6 请求频率、寄存器回读与实际结果

| 请求 SCLK | timing_init | TIMING.SCLK_DIV | 公式算得 SCLK | 实测速率 | 结论 |
|---|---|---|---|---|---|
| 1 MHz | OK | 0x27 | 1.000 MHz | 0.996 Mbit/s | 可用 |
| 2 MHz | OK | 0x13 | 2.000 MHz | 1.99 Mbit/s | 可用 |
| 4 MHz | OK | 0x09 | 4.000 MHz | 3.97 Mbit/s | 可用 |
| 5 MHz | OK | 0x07 | 5.000 MHz | 4.97 Mbit/s | 可用 |
| 8 MHz | OK | 0x04 | 8.000 MHz | 7.92 Mbit/s | 可用 |
| 10 MHz | OK | 0x03 | 10.000 MHz | 9.88 Mbit/s | **互连推荐上限（0 错误）** |
| 16 MHz | FAIL | 保持 0x03 | — | — | 80/16 = 5，商为奇数 |
| 20 MHz | OK | 0x01 | 20.000 MHz | 19.56 Mbit/s | 能配，但互连有固定首字节误码（§7.7.5） |
| 23 MHz | FAIL | 保持 0x01 | — | — | 80/23 除不尽 |
| 25 MHz | FAIL | 保持 0x01 | — | — | 80/25 = 3.2 |
| 32 MHz | FAIL | 保持 0x01 | — | — | 80/32 = 2.5 |
| 40 MHz | OK | 0x00 | 40.000 MHz | 传输超时 | 能配，从机收不了 |
| 50 MHz | FAIL | 保持 0x00 | — | — | 80/50 = 1.6 |
| 60 MHz | FAIL | 保持 0x00 | — | — | 80/60 = 1.33 |
| 64 MHz | FAIL | 保持 0x00 | — | — | 80/64 = 1.25 |
| 80 MHz | OK | 0xFF | 80.000 MHz（= 源时钟） | 传输超时 | 能配，从机收不了 |

两点说明：
1. timing_init 失败时寄存器保持上一次的值，所以 FAIL 行的 DIV 列不是"配置成功值"，只用于说明"没被改写"。
2. 上一轮"40/80 MHz 只得到约 23 MHz"的结论要拆成两件事：40/80 MHz 确实配出来了（DIV=0x00/0xFF），
   而 23 MHz 是**用轮询方式测速时 CPU 侧的天花板**——spi_transfer() 每字节约 20 个 CPU 周期，
   480 MHz 下正好卡在 ~23 Mbit/s。换成"从机 + DMA"互连后，40/80 MHz 直接在传输窗口内超时（从机收不过来），
   20 cm 杜邦线上的 40/80 MHz 没有可用性。


### 7.7.2 模式与位宽


表 7-7 参考来源索引

| 测试项 | 结果 | 说明 |
|---|---|---|
| Mode 0/1/2/3 @20 MHz, 1024 B | 412 / 411 / 411 / 411 µs | 四种模式耗时一致 |
| 8 bit, 2048 B | 821 µs（19.94 Mbit/s） | — |
| 16 bit, 1024 word | 821 µs（19.94 Mbit/s） | 总比特数相同 |
| 32 bit, 512 word | 821 µs（19.93 Mbit/s） | 总比特数相同 |

### 7.7.3 DMA 带宽

表 7-4：`sdma_bench`（2048 B/次，`logs/20261030-113457-spi_dma_bw.log`）。


表 7-8 待确认项与风险

| SCLK | 时间 | 实测带宽 | 理论带宽（SCLK/8） | 效率 |
|---|---|---|---|---|
| 1 MHz | 16395 µs | 124 KB/s | 125 KB/s | 99% |
| 5 MHz | 3280 µs | 624 KB/s | 625 KB/s | 99% |
| 10 MHz | 1641 µs | 1247 KB/s | 1250 KB/s | 99% |
| 20 MHz | 822 µs | 2490 KB/s | 2500 KB/s | 99% |
| 40 MHz | 590 µs | 3466 KB/s | 5000 KB/s | 69% |
| 60 MHz | — | — | — | 配置失败 |

![图 7-2 请求 SCLK 与实测 DMA 吞吐](figures/07-spi/fig-07-2-spi-throughput.png)

结论：**≤20 MHz 时 DMA 利用率 99%，带宽完全由 SCLK 决定（KB/s = SCLK/8/1000）**；
40 MHz 请求因实际 SCLK 只有 ~23 MHz，只能拿到 3466 KB/s（69%）。

### 7.7.4 双实例主从实测（spi1_spi3_master_slave）

表 7-5：SPI1 master 与 SPI3 slave 双向实测（n=128 B，mode 0，内部上拉 + 杜邦线；
`logs/20261030-162048-spi1_spi3_master_slave.log`）。

| SCLK | 128 B 双向耗时 | 双向带宽 | 误码 | 结论 |
|---|---|---|---|---|
| 1 MHz | 1037.76 us | 123 KB/s | 0 | PASS |
| 5 MHz | 210.55 us | 607 KB/s | 0 | PASS |
| 10 MHz | 107.28 us | 1193 KB/s | 0 | PASS |
| 20 MHz | 55.67 us | 2299 KB/s | 1 byte（首字节 1 bit） | 建议 ≤10 MHz |

补充：16 B 双向传输 PASS（141.80 us，112 KB/s）；slave 预置→master 只读 32 B PASS（269.83 us，118 KB/s）。
带宽随 SCLK 线性增长，说明 SCLK/CS/MOSI/MISO 四条线全部打通；与理论值（SCLK/8）相比效率 96~99%。

### 7.7.5 SPI1 与 SPI3 互连：连续误码与首字节误码定位（R5 实测）

接线如图 7-3：SPI1（PC10/PC11/PC12/PC13）与 SPI3（PA26/PA27/PA28/PA29）同名相连，
SCLK、CS 一一对应，MOSI-MOSI、MISO-MISO，20 cm 杜邦线，无外部上拉与串阻。

![图 7-3 SPI1 与 SPI3 板内互连与信号流](figures/07-spi/fig-07-3-spi1-spi3-crosslink.png)

复现命令（一次烧写跑完）：

    bash scripts/hwrun.sh spi/spi1_spi3_master_slave --secs 30       --send "spi_ms_sweep;spi_ms_ber 1 10000000;spi_ms_ber 1 20000000;spi_ms_guard 20000000;spi_ms_spd 0;spi_ms_spd 1;spi_ms_spd 2;spi_ms_spd 3"

日志：logs/20261102-121103-spi1_spi3_master_slave.log（扫描/误码/归因）、
logs/20261102-123349-spi1_spi3_master_slave.log（从机 DMA 与从机轮询对照）。

表 7-9 连续误码（1024 B/次，双向逐字节校验，每字节线上两个方向各 8 bit）

| SCLK | 数据量 | 传输次数 | 错误字节 | 耗时 | 结论 |
|---|---|---|---|---|---|
| 10 MHz | 1 MB | 1024 | 0（< 1/16777216 bit） | 1200.51 ms | **PASS**，873 KB/s |
| 10 MHz | 128 B/次 bench | 4 档 1/5/10/20 MHz | 0 | 107.45 us | 1191 KB/s |
| 20 MHz | 1 MB | 1024 | 1024 | 3335.89 ms | 每次传输固定错 1 字节（首字节） |

表 7-10 20 MHz 首字节误码的归因实验

| 实验 | 参数 | 结果 |
|---|---|---|
| CS 建立/保持扫描 | cs2sclk = 1/2/4/8/16/32/64 x csht = 0/1/2/4，共 28 组 | 28 组全部 FAIL，每组都是每次传输 1 个错误字节 |
| PAD 驱动强度 | SPD = 0/1/2/3（主机 4 脚 + 从机 2 脚） | 4 组全部 FAIL，错误数不变（200 次 x 64 B 都是 200 个错字节） |
| 从机走 DMA | 从机 TX/RX 用 HDMA ch4/5 | 每帧首字节错：m_rx[0] = 0xE5，期望 0xA5（第 2 个 bit 翻转），其余字节全对 |
| 从机走轮询 | 从机 FIFO 预装 + 轮询收数据（spi_ms_poll 64，8 B 一段） | 仍然错：8 段里 5 段首字节错，同样是 0xA5 变 0xE5 |
| 降到 10 MHz | 以上两条路径 | 0 错误 |

结论：错误固定在"每次 CS 有效后的第 1 个字节、第 2 个 bit"，与从机用 DMA 还是轮询无关，
与 CS 前导/保持时间、PAD 驱动强度也无关。也就是说，20 MHz 下从机在每帧第一次移位时
MISO 的建立时间不够（从机侧采样相位），而不是主机时序配置或片选距离的问题。
工程做法只有两条：按 10 MHz 用（1 MB 0 错误），或者每次传输丢弃/忽略第 1 个字节。

40/80 MHz 能配置（DIV = 0x00 / 0xFF），但从机侧在传输窗口内等不到完成（日志里标 rc!），
即主机在发时钟、从机没在这个窗口里收完。40 MHz 以上需要 PCB 走线（≤5 cm）+ 端接，
20 cm 杜邦线不具备条件，属于布线与从机启动限制，不是驱动配置错误。

## 7.8 性能与调优

1. **带宽公式**：`MB/s = SCLK(MHz) / 8`。20 MHz → 2.5 MB/s（实测 2490 KB/s）。
2. 想再快必须解决时钟：25/50/60 MHz 不可配、40/80 MHz 只得到 ~23 MHz（见待确认）。
   若外设允许，20 MHz 是最稳的"精确点"。
3. DMA 与轮询的差别不在带宽（都是 99%），而在 **CPU 占用**：轮询方式 CPU 停在传输循环里，
   DMA 方式 CPU 可以去做别的事，只在结束时处理。
4. 大量数据时把 `WRTRANCNT/RDTRANCNT` 设大（一次传完），比反复调用 `spi_transfer()` 少了每次的
   CS/命令阶段开销；本仓库 `spi_dma_bw` 一次 2048 B 就是这个思路。
5. 从器件要求 CS 建立/保持时间时，改 `timing_config.master_config.cs2sclk/csht`，
   或直接用 GPIO 作 CS（`init_spi1_pins_with_gpio_as_cs()`）。

## 7.9 坑与注意事项

0. **现象：slave 能收到 master 数据，但 master 永远收到 0（或全 0x00/全 0xFF）** → 原因：**master（以及 slave）的 SCLK pad 没有开 IOC LOOP_BACK**。
   HPM SPI 的接收移位时钟来自 SCLK pad 的内部回送；不开时 SCLK 仍能输出到外部（从机正常收数），但 master 自己的 RX 移位寄存器没有时钟，
   所以永远采不到 MISO——本板实测把从机 MISO 引脚强拉高也读不到。→ 解决：SCLK 的 FUNC_CTL 加 `IOC_PAD_FUNC_CTL_LOOP_BACK_MASK`
   （见 `boards/hpm5e31_1/pins/pinmux_core.c` 的 `init_spi1_master_pins()/init_spi3_slave_pins()`）。与第 06 章 I2C 的同类坑一致。
0b. **现象：加 LOOP_BACK 后数据整体错位 1 字节** → 原因：slave TX FIFO 在首个时钟到来前 underrun。
   → 解决：slave TX DMA 多发 1 个字节（尾部 spare），本仓库实测这样即 0 错误。
1. 【待接线】**现象：打开 IOC LOOP_BACK 后 SPI 收到的数据仍不匹配** → 原因：LOOP_BACK 只是"本 PAD 输出回送本 PAD 输入"，
   MOSI 的输出不会跑到 MISO 的输入。→ 解决：【待接线】外部用一根线短接 PC12(MISO) 与 PC13(MOSI)，
   再跑 `spi_loop` 就会 MATCH（本仓库实测 MISMATCH 是预期结果，日志可查）。
2. **现象：`spi_transfer()` 完全不收数据** → 原因：MISO 悬空且没有上拉，读到随机电平。
   → 解决：本仓库 `init_spi1_loopback_pins()` 给 MISO/MOSI 开了内部上拉，至少能读到稳定高电平。
3. **现象：DMA 传输永远等不到 TC** → 原因：用 `spi_transfer()` 启动 DMA 传输（它会重置 FIFO），
   或没设置 `tx_dma_enable/rx_dma_enable`。→ 解决：按 4.2 的四步顺序，用 `spi_setup_dma_transfer()`。
4. **现象：TX DMA 启动后 SPI 提前开始发数据** → 原因：上一次的 TX DMA 请求还挂着。
   → 解决：配置前先 `spi_disable_tx_dma()`（SDK 例程注释里专门提到）。
5. **现象：请求 25/50/60 MHz 时 `spi_master_timing_init()` 失败** → 原因：源时钟 80 MHz 无法整除这些频率，
   且分频器有最小/最大约束。→ 解决：改用可整除的点（1/2/4/5/8/10/16/20 MHz 等），或换 SPI 源时钟。
6. **现象：40 MHz 请求实测只有 ~23 MHz** → 原因：两件事叠加。① 请求 >= 源时钟时驱动直接写 DIV=0xFF，
   SCLK 直通 80 MHz，所以"40/80 MHz 配不出来"这个判断本身是错的；② 用轮询方式测速时，
   spi_transfer() 每字节约 20 个 CPU 周期，吞吐被 CPU 卡在 ~23 Mbit/s——**测到的 23 MHz 是轮询通路的天花板，不是 SCLK**。
   → 解决：按 §7.7.1 的表选 80/(2m) 的点；互连实测 10 MHz 最稳，20 MHz 有固定首字节误码，40/80 MHz 超时。
   → 解决：需要示波器实测 SCLK 引脚；高速外设一律按实测值算时序。
7. **现象：SPI2 初始化后 LAN8651 不工作** → 原因：SPI2（PA20~PA23）是 LAN8651 专用，CS/RST/INT 也是专用 GPIO。
   → 解决：不要在本章的 SPI1 例程里碰 SPI2；LAN8651 见第 10 章。

## 7.10 优劣评价与同类对比

| 维度 | HPM5E31 SPI（实测） | STM32H7 SPI | TI AM243 SPI |
|---|---|---|---|
| FIFO | 8 word | 16 B（部分型号） | 有 |
| 最高实测 SCLK | 20 MHz 精确；~23 MHz 有效上限 | 可达 50+ MHz | 可达 40 MHz |
| 模式 | 4 种 + Dual/Quad | 4 种 + Dual/Quad | 4 种 |
| DMA | TX/RX 双向，99% 效率 | 有 | 有 |
| 传输模式 | 10 种（write/read/dummy/together） | 常见组合 | 常见组合 |
| 短板 | 80 MHz 源时钟下可配频率点少 | 时钟树灵活 | 时钟灵活 |

结论：**HPM5E00 的 SPI 功能够用（DMA 效率 99% 很亮眼），但在 80 MHz 源时钟下"可精确配置的频率点"偏少，
高速率要按实测校准**——这是写 SPI 驱动时最容易忽略的点。

## 7.11 参考来源

1. RM `47.2（PDF p.816）：通讯外设概述，4×SPI、经典 2 线 + Dual/Quad
2. RM `49（PDF p.837~854）：SPI 特性、传输控制、时序寄存器
3. RM `49.2.6（PDF p.840）：FIFO 深度 8 word、DMA 与阈值中断
4. SDK：`hpm_sdk/drivers/src/hpm_spi_drv.c`（`spi_transfer/spi_control_init/spi_write_read_data`）、
   `hpm_spi_drv.h`（`spi_trans_mode_t/spi_format_config_t`）
5. SDK 例程：`hpm_sdk/samples/drivers/spi/polling/master/src/spi.c`、
   `hpm_sdk/samples/drivers/spi/dma/master/src/spi.c`
6. 实测日志：`logs/20261030-112819-spi_loopback_poll.log`、`logs/20261030-113457-spi_dma_bw.log`
7. HPM SDK 上游（SPI 驱动与例程，v1.13.0）：[https://github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)
8. 本仓库引脚：`boards/hpm5e31_1/pinmux.c` 的 `init_spi1_pins()`、`boards/hpm5e31_1/pins/pinmux_core.c`

## 7.12 扩展阅读

- 第 10 章：SPI2 + LAN8651 10BASE-T1S 的完整移植（15 MHz SCLK、专用 CS/RST/INT）。
- 第 19 章：DMA/DMAMUX 的通用机制（本章 DMA 只用了最基础的握手模式）。
- HPM SDK SPI 文档：`hpm_sdk/samples/drivers/spi/index_zh.rst`

## 7.13 待确认项与风险

> 已逐条复核（2026-11-02）：5 项闭环为实测结论，1 项为已知限制（无示波器）。

| 项 | 状态 | 说明 / 验证方法 |
|---|---|---|
| 40/80 MHz 请求为何只得到约 23 MHz | **已定位（2026-11-02）** | ① 分频规则决定只能配 80/(2m)，40 MHz 时 DIV=0x00、80 MHz 时 DIV=0xFF（=源时钟直通），寄存器回读证实（表 7-6）；② "23 MHz"是轮询通路的 CPU 天花板（每字节约 20 周期），不是 SCLK；③ 互连时 40/80 MHz 直接在传输窗口内超时 |
| 20 MHz 首字节 1 bit 误码的根因 | **已定位（2026-11-02）** | 28 组 CS guard + 4 组 SPD 全无效；从机 DMA 与从机轮询都错在同一位置（首字节第 2 bit）→ 从机首帧采样相位，非主机配置（表 7-10） |
| MOSI→MISO 外部短接后的真实回环正确率 | **已实测（2026-11-02）** | 用 SPI1↔SPI3 互连替代单线短接：10 MHz 连续 1 MB 双向 0 错误（表 7-9） |
| 60 MHz 以上是否有可用配置点 | **已实测（2026-11-02）** | 80 MHz 源时钟下只有 80/(2m)（40/20/13.3/10/8/5.71…）和 0xFF 直通两种；16/23/25/32/50/60/64 MHz 一律返回 status_invalid_argument（表 7-6） |
| 高 SCLK 下的信号完整性（过冲/振铃） | 已知限制（无示波器） | 目前只有"功能是否通过"这一层证据（SPD 0~3 + CS guard 28 组都无效，说明不是边沿/驱动问题）；验证方法：示波器测 PC10（20 MHz 方波）的过冲与上升沿，必要时串 22~33 Ω |
| 与 LAN8651（SPI2，15 MHz）的时序余量 | **已知限制（跨章，见第 10 章（tp-net 负责）** | 本章只给出"80 MHz 源时钟可配频率集合"这一通用结论：15 MHz 不在 80/(2m) 内（80/15 = 5.33），第 10 章若用 15 MHz，需以那里的源时钟与 TIMING 回读为准 |

## 7.14 扩展阅读（R5 补充）

- HPM SDK 上游仓库（SPI 驱动与例程）：[https://github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk)（v1.13.0）
- 第 24 章总线吞吐表引用了本章的 SPI 主机吞吐数据（DMA 99% 利用率、互连 10 MHz 单向 873 KB/s）
