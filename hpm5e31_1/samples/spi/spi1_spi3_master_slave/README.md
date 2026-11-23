# spi1_spi3_master_slave —— 同一 MCU 双 SPI 实例主从实测（SPI1 master ↔ SPI3 slave，双向 PASS）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 状态：已打通（1/5/10 MHz 双向 0 错误；20 MHz 首字节 1 bit 误码，见"实测数据"）

## 功能

用**同一颗 MCU 的两个 SPI 实例**做真实主从全双工通信（用户已用杜邦线连接）：

- **SPI1 = master**（SCLK=PC10 / CS=PC11 / MISO=PC12 / MOSI=PC13），**mode 0**、8 bit、硬件 CS。
- **SPI3 = slave**（SCLK=PA26 / CS=PA27 / MISO=PA28 / MOSI=PA29），用 **DMA(HDMA ch4/5)** 自主收发；
  master 侧阻塞 `spi_transfer()` 发时钟，slave 侧由 DMA 自动搬数据。
- 双向校验：master_rx 必须等于 slave_tx，slave_rx 必须等于 master_tx。
- 测 1 / 5 / 10 / 20 MHz 的带宽与误码；支持"slave 预置数据、master 只读"模式。
- 内置硬件接线探测 `spi_ms_wires`（GPIO 驱动-读回，不需要示波器）。

## 硬件 / 接线（正确接法，用户已按此接好）

| SPI1（master） | 方向 | SPI3（slave） |
|---|---|---|
| PC10 SCLK | ↔ | PA26 SCLK |
| PC11 CS | ↔ | PA27 CS |
| PC12 MISO | ← | PA28 MISO |
| PC13 MOSI | → | PA29 MOSI |

> SPI 是"同名相连"（MOSI↔MOSI、MISO↔MISO）。**HPM5E31 的 PA28 只能做 SPI3_MISO、PA29 只能做 SPI3_MOSI**，
> 不能用软件互换复用功能，接错只能改线（本板曾接成 MOSI↔MISO 交叉，现象是 master 全 0x00、slave 全 0xFF）。

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh spi/spi1_spi3_master_slave
bash scripts/hwrun.sh spi/spi1_spi3_master_slave --secs 15 --lock-wait 900 \
  --send "spi_ms_wires;spi_ms_test 16 1000000;spi_ms_read 32;spi_ms_bench"
```

## 命令

| 命令 | 作用 |
|---|---|
| `spi_ms_info` | 主从引脚/时钟/DMA 配置 + 接线提示 |
| `spi_ms_test [bytes] [hz]` | 一次双向传输 + 双向校验 + 带宽 |
| `spi_ms_bench` | 1/5/10/20 MHz 带宽与误码表 |
| `spi_ms_read [bytes]` | slave 预置数据 → master 只读校验 |
| `spi_ms_wires` | GPIO 驱动-读回探测实际接线 |
| `spi_ms_dbg` | 打印两侧 TRANSFMT/CTRL/TRANSCTRL/STATUS 与 PAD 配置 |
| `spi_ms_cs_probe` / `spi_ms_miso_probe` | 诊断 CS 边缘 / master MISO 输入通路 |

## 实测数据（logs/20261030-162048-spi1_spi3_master_slave.log）

```text
bidir        n=16   PASS errors=0  time=141.80 us  bw=112 KB/s
slave->mst   n=32   PASS errors=0  time=269.83 us  bw=118 KB/s
spi master/slave bidirectional bench (n=128, mode0):
  hz=1000000   bench  n=128  PASS errors=0  time=1037.76 us  bw=123 KB/s
  hz=5000000   bench  n=128  PASS errors=0  time=210.55 us   bw=607 KB/s
  hz=10000000  bench  n=128  PASS errors=0  time=107.28 us   bw=1193 KB/s
  hz=20000000  bench  n=128  FAIL errors=1  time=55.67 us    bw=2299 KB/s   <- 首字节 1 bit 误码
```

| SCLK | 128 B 双向耗时 | 双向带宽 | 误码 | 结论 |
|---|---|---|---|---|
| 1 MHz | 1037.76 us | 123 KB/s | 0 | PASS |
| 5 MHz | 210.55 us | 607 KB/s | 0 | PASS |
| 10 MHz | 107.28 us | 1193 KB/s | 0 | PASS |
| 20 MHz | 55.67 us | 2299 KB/s | 1 byte（首字节 1 bit） | 建议 ≤10 MHz |

## 关键点 / 坑（本板实测）

1. **【最关键】SPI master 和 slave 的 SCLK pad 都必须开 IOC LOOP_BACK！**
   不开时：slave 能正常收到 master 数据（s_rx 正确），但 **master 永远收不到 MISO（m_rx 全 0）**，
   连把 slave MISO 引脚强拉高也读不到 —— 因为 master 的 RX 移位时钟来自 SCLK pad 的内部回送。
   加上 `IOC_PAD_FUNC_CTL_LOOP_BACK_MASK` 后立即恢复。与 I2C 章节的同类坑一致。
2. **slave TX DMA 要发 n+1 字节**（尾部多一个 spare）：实测只发 n 字节时，slave TX FIFO 在首个时钟前
   underrun，数据整体错位 1 字节；多发 1 字节后标准对齐、全部匹配。
3. slave 的 DMA 必须在 master 发时钟**之前**武装（`spi_setup_dma_transfer()`），因为 master 的
   `spi_transfer()` 是阻塞调用，期间 CPU 不会回来配置 slave。
4. 20 MHz 下首字节有 1 bit 误码（本板内部上拉 + 杜邦线，边沿余量不足），1/5/10 MHz 全 0 错误；
   高速场景建议外部上拉/缩短线或降到 ≤10 MHz。
5. `spi_ms_wires` / `spi_ms_cs_probe` / `spi_ms_miso_probe` 是排查接线的三件套，不用示波器。

## 待确认 / 风险

| 项 | 状态 | 验证方法 |
|---|---|---|
| 20 MHz 首字节 1 bit 误码的根因 | **已定位（2026-11-02）** | 28 组 CS guard + 4 组 SPD 全无效；从机 DMA 与从机轮询都错在首字节第 2 bit（0xA5→0xE5）→ 从机首帧采样相位 |
| 40 MHz 是否可用 | **已实测（2026-11-02）** | 能配（TIMING.SCLK_DIV=0x00 → 40 MHz），但从机在传输窗口内收不完 → 超时；80 MHz 源时钟只能配 80/(2m) |
| 长时间（万次）传输误码率 | **已实测（2026-11-02）** | 10 MHz 连续 1 MB（1024 次 x 1024 B）双向 0 错误，873 KB/s；20 MHz 1 MB 每次传输 1 字节错 |

## R5 新增命令与实测（2026-11-02）

    spi_ms_sweep            频率扫描: 请求频率 / TIMING.SCLK_DIV 回读 / 实测速率
    spi_ms_ber [MB] [Hz]    连续误码: 反复 1024 B 双向传输, 统计错误字节与吞吐
    spi_ms_guard [Hz]       CS 建立/保持扫描: cs2sclk x csht 共 28 组
    spi_ms_spd [0..3] [Hz]  PAD 驱动强度扫描

一次跑完（约 10 s 的测试量）：

    bash scripts/hwrun.sh spi/spi1_spi3_master_slave --secs 30       --send "spi_ms_sweep;spi_ms_ber 1 10000000;spi_ms_ber 1 20000000;spi_ms_guard 20000000;spi_ms_spd 0;spi_ms_spd 1;spi_ms_spd 2;spi_ms_spd 3"

结果（日志 logs/20261102-121103-spi1_spi3_master_slave.log、logs/20261102-123349-spi1_spi3_master_slave.log）：

| 项 | 结果 |
|---|---|
| 源时钟 | SPI1 = SPI3 = 80 MHz（cpu 480 MHz） |
| 可配频率 | 只有 80/(2m)：40/20/13.3/10/8/5.71… MHz，以及 DIV=0xFF 的 80 MHz 直通 |
| 16/23/25/32/50/60/64 MHz | spi_master_timing_init() 返回失败（商为奇数或除不尽） |
| 10 MHz 连续误码 | 1 MB 双向 0 错误（16777216 bit），1200.51 ms，873 KB/s |
| 20 MHz 连续误码 | 每次传输固定 1 字节（首字节） |
| 首字节误码归因 | CS guard 28 组 + SPD 4 组全部无效；从机 DMA 与轮询都错在同一位置 → 从机首帧采样相位 |
| 40/80 MHz | 能配置（DIV=0x00/0xFF），互连直接超时，20 cm 杜邦线不可用 |
