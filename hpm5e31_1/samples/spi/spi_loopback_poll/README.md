# spi_loopback_poll —— SPI1 回环尝试 + SCLK 实测 + 模式/位宽/频率扫描

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

板上 SPI1 没有挂从器件，因此本例程做两件事：

1. **回环尝试**：分别在不打开 / 打开 MOSI PAD 的 `LOOP_BACK` 时各做一次 8 字节全双工传输，
   直接打印读到的字节，证明 **IOC 的 LOOP_BACK 不能替代 MOSI→MISO 外部短接**。
   【待接线】想做真正的数据回环，用一根杜邦线把 PC12(MISO) 与 PC13(MOSI) 短接即可（本次未接，README 与本章均按"待接线"标注）。
2. **控制器自证**：SPI 主机即使没有从器件也一定会输出 SCLK，所以可以用"一次 N 字节传输的耗时"
   反推实际 SCLK = N×8/t，并给出：
   - 配置频率 vs 实测频率（ppm 误差）；
   - CPOL/CPHA 四种模式传输耗时（应基本一致）；
   - 8/16/32 位宽的吞吐；
   - 100 kHz ~ 80 MHz 频率扫描，观察分频量化误差。

## 硬件 / 接线（【待接线】：PC12-PC13 短接一根线后可验证真回环）

| 信号 | 引脚 | 说明 |
|---|---|---|
| SCLK | PC10 | 复用自 `init_spi1_pins()`，带 LOOP_BACK |
| MISO | PC12 | 需要外部短接到 MOSI 才能回环 |
| MOSI | PC13 | 同上 |
| CS | PC11 | 硬件片选 |

## 编译与运行

```bash
bash scripts/build.sh spi/spi_loopback_poll
bash scripts/hwrun.sh spi/spi_loopback_poll --secs 8 --send "spi_info;spi_loop;spi_sclk 1024 20000000;spi_modes;spi_bits;spi_sweep"
```

## 命令

| 命令 | 作用 |
|---|---|
| `spi_info` | 引脚/时钟/配置 |
| `spi_loop` | 回环尝试（对比不打开/打开 PAD LOOP_BACK） |
| `spi_sclk [N] [hz]` | 实测 SCLK |
| `spi_sweep` | 100 kHz~80 MHz 扫描表 |
| `spi_modes` | 4 种 CPOL/CPHA 模式 |
| `spi_bits` | 8/16/32 位宽 |

## 实测现象

实测（`logs/20261030-112819-spi_loopback_poll.log`，源时钟 80 MHz）：

```text
no-loopback rx: 00 00 00 00 00 00 00 00
mosi-loopback rx: 00 00 00 00 00 00 00 00 -> MISMATCH(需外部短接 MISO-MOSI)
spi_sclk cfg=20000000 measured=19880780 err=-5961 ppm, n=1024 time=412.05 us
spi_modes: 4 种模式 411~412 us（一致）
spi_bits : 8/16/32 bit 都是 821 us（总比特数相同）
spi_sweep: 1M->998565, 5M->4988854, 10M->9965860, 20M->19885103,
           25M/50M/60M 配置失败, 40M->23166328, 80M->23030104
```

## 关键点 / 坑

1. **IOC 的 LOOP_BACK 是"本 PAD 输出→本 PAD 输入"**，用于时钟反馈；它不是 MOSI→MISO 的内部通道。
   很多资料误以为打开 LOOP_BACK 就能自环，实测不符（见日志里 `MISMATCH`）。
2. 没有从器件时 MISO 是悬空/上拉，读到的字节没有意义，但 **SCLK 一定在输出**，所以用时序测吞吐是可靠的。
3. `spi_master_timing_init()` 对无法整除的频率会取最近分频，实测误差可达百分之几（见扫描表），
   做高速外设（如 LAN8651、SPI Flash）时必须按实测反推真实 SCLK 再算时序。
4. `spi_transfer()` 是阻塞的，返回前会 `spi_wait_for_idle_status()`，所以直接用 mcycle 包住它就是"总线占用时间"。
5. 频率越高，PAD 的 SR/SPD 设置越关键（`init_spi1_pins()` 已把 SR=1、SPD=3=最大 200 MHz 档）。
