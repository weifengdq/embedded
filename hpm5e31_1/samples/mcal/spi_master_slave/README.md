# spi_master_slave —— AUTOSAR MCAL Spi 例程（SPI1 主 ↔ SPI3 从）

同一颗 MCU 上两个 SPI 实例做主从互测：SPI1 作主机发时钟，SPI3 作从机用 DMA 收发，
一次 `Spi_SyncTransmit` 完成双向数据交换并逐字节比对。用来验证 MCAL Spi 驱动的
Job/Sequence 状态机、EB/IB 缓冲、8/16/32 bit 数据宽度与 1/10 MHz 时钟切换。

## 硬件要求与接线

| 信号 | SPI1（主） | SPI3（从） | 说明 |
|---|---|---|---|
| SCLK | PC10 | PA26 | 杜邦线直连；主机 PC10 需 IOC LOOP_BACK（板级 pinmux 已配） |
| nCS | PC11 | PA27 | 片选，主机模式 0 低有效 |
| MISO | PC12 | PA28 | 从机输出 → 主机输入 |
| MOSI | PC13 | PA29 | 主机输出 → 从机输入 |

SPI2 留给板载 LAN8651（10BASE-T1S），本模块不初始化。DMA：主机异步 TX/RX = HDMA ch2/3，
从机 TX/RX = HDMA ch4/5。

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/spi_master_slave
bash scripts/hwrun.sh mcal/spi_master_slave --secs 50 \
  --send 'spi bench;spi init 10m;spi ber 1 1024;spi init 1m;spi ber 1 1024;spi det;spi ib'
```

| 命令 | 说明 |
|---|---|
| `spi info` | 版本、配置集、SCLK、状态、Job/Seq 回调计数 |
| `spi init 1m` / `spi init 10m` | 切换 post-build 配置集（Spi_DeInit + Spi_Init） |
| `spi xfer [n]` | 8 bit 双向 n 字节，逐字节比对 |
| `spi width 8|16|32 [n]` | 指定数据宽度双向传输 |
| `spi bench` | 1 MHz 与 10 MHz 各跑 64/256/1024 B |
| `spi ber [MB] [chunk]` | 连续 MB 级传输误码与吞吐（分块） |
| `spi ib` | IB 内部缓冲通道（WriteIB/ReadIB） |
| `spi det` | 10 条非法参数调用，验证 Det 计数 |
| `spi raw` | 诊断：同一硬件用 SDK 直调路径跑一次，与 MCAL 路径对照 |

## 实测现象与数据

串口日志：`logs/20261102-142330-spi_master_slave.log`（PASS，2 次重复一致）。

```text
  1MHz    n=64    w=1  PASS err=0    t=534.477 us 双向=239 KB/s
  1MHz    n=256   w=1  PASS err=0    t=2064.681 us 双向=247 KB/s
  1MHz    n=1024  w=1  PASS err=0    t=8208.629 us 双向=249 KB/s
  10MHz   n=64    w=1  PASS err=0    t=63.727 us 双向=2008 KB/s
  10MHz   n=256   w=1  PASS err=0    t=217.250 us 双向=2356 KB/s
  10MHz   n=1024  w=1  PASS err=0    t=831.602 us 双向=2462 KB/s
ber: 1 MB @ 10MHz chunk=1024 xfers=1024 fail=0 err_bytes=0, 1201.66 ms
     单向吞吐=872 KB/s (0.85 MB/s)，双向=1744 KB/s，误码=0/16777216 bit => PASS (0 误码)
ber: 1 MB @ 1MHz chunk=1024 xfers=1024 fail=0 err_bytes=0, 8755.53 ms
     单向吞吐=119 KB/s (0.11 MB/s)，双向=238 KB/s，误码=0/16777216 bit => PASS (0 误码)
  ib   1MHz  n=64  w=1  PASS err=0  t=535.204 us 双向=239 KB/s
  width 1MHz n=64   w=2  PASS err=0  t=1058.208 us
  width 1MHz n=32   w=4  PASS err=0  t=1051.316 us
det: after inject count=10 (期望 +10)
```

关键数据：1 MHz 单次 1024 B 双向 249 KB/s（理论 250 KB/s，99.6%）；10 MHz 单次 2462 KB/s
（理论 2500 KB/s，98.5%）；1 MB 连续零误码（双向共 16777216 bit）。8/16/32 bit 全部 PASS，
IB 通道 PASS。Det 注入 10 条非法调用计数 +10。

## 关键点

1. **从机 Job 武装后立即推进序列**：片内主从互测时从机时钟来自同序列的下一个主机 Job，
   等从机完成会死锁。驱动实现为"武装即推进、主机完成后回收 DMA TC"。
2. **从机 TX 多发 1 个元素**（PBcfg 的 ExtraTxElements=1）：HPM SPI 从机 TX FIFO 在首个
   SCLK 前会欠载，只发 n 字节时数据整体错位 1 字节。这是 HPM IP 的行为，SWS 无对应配置项。
3. **EB 缓冲必须放 non-cacheable RAM**（例程用 ATTR_PLACE_AT_NONCACHEABLE）：DMA 访问
   ILM/DLM 会得到全 0。
4. 1 MB 连续传输按 1024 B 分块，单块一次 SyncTransmit；块间软件开销使 10 MHz 效率从
   98.5% 降到 69.8%，这是当前实现的性能边界。

## 待确认项

- 异步中断模式（DMA + END 中断）未通过实测，见 spi_async_job 的 `spi_job hint`；复跑命令
  `bash scripts/hwrun.sh mcal/spi_async_job --secs 20 --send 'spi_job hint 64'`。
- 10 MHz 只在 20 cm 杜邦线、室温下验证过，未做温度与电压边界扫描。
