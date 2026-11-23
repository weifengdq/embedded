# spi_dma_bw —— SPI1 + HDMA 带宽实测

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

SPI1 主机用 DMA 搬 TX/RX 数据（无外部从器件，MISO 数据无意义，但 SCLK 与总线搬运是真实的）：

- TX：内存 → `SPI1.DATA`（DMA 请求来自 TX FIFO）
- RX：`SPI1.DATA` → 内存
- `spi_transfer()` 打开 `tx_dma_enable/rx_dma_enable` 后会自动等待总线空闲，用 mcycle 包住即可测总时间。
- 输出实测 KB/s、理论值（SCLK/8）与效率。

## 硬件

SPI1 SCLK=PC10 / MISO=PC12 / MOSI=PC13 / CS=PC11；DMA 用片上 `HPM_HDMA` 的 0/1 通道。

## 编译与运行

```bash
bash scripts/build.sh spi/spi_dma_bw
bash scripts/hwrun.sh spi/spi_dma_bw --secs 8 --send "sdma_info;sdma_test 2048 20000000;sdma_bench"
```

## 命令

| 命令 | 作用 |
|---|---|
| `sdma_info` | 通道/DMAMUX/时钟 |
| `sdma_test [bytes] [sclk]` | 单次 DMA 传输，输出带宽与效率 |
| `sdma_bench` | 1~60 MHz 带宽表 |

## 实测现象

实测（`logs/20261030-113457-spi_dma_bw.log`）：

```text
sdma_test n=2048 sclk=20000000 rc=0 time=822 us bw=2490 KB/s theory=2500 KB/s eff=99%
sdma_bench: 1M->124 KB/s, 5M->624, 10M->1247, 20M->2490 (均 99%), 40M->3466 (69%), 60M 配置失败
```

## 关键点 / 坑

1. 缓冲区必须 `ATTR_PLACE_AT_NONCACHEABLE`；若用 cacheable 内存要自己做 writeback/invalidate。
2. TX DMA 前必须先 `spi_disable_tx_dma()`，否则上一次的 DMA 请求可能让 SPI 提前启动（SDK 样例注释里专门提到）。
3. **没有从器件时 RX 数据无效**，所以本样例只统计时间和带宽，不校验内容；要校验必须外部短接 MISO-MOSI。
4. `spi_transfer()` 内部会等 `spi_wait_for_idle_status()`，所以 DMA 传输时间包含末尾的 idle 等待，比纯 SCLK 时间略长（效率列可见）。
5. 60 MHz 时 PAD 抖动/上升沿会明显影响外部器件可用性，真实外设请降额使用（LAN8651 用 15 MHz）。