# dma_mem2mem —— DMAv2 内存到内存搬移：突发/位宽扫描、带宽实测、链式描述符

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 用 DMAv2 在内存之间搬移数据，**扫描突发长度**与**传输位宽**，实测带宽（MB/s）并用 memcmp 校验数据；
- 对比 **XDMA**（AXI 系统总线，高带宽）与 **HDMA**（AHB 外设总线，低延迟）；
- 演示 **链式描述符**（linked descriptor）连续搬移多段；
- 源/目的缓冲都放在 **non-cacheable AXI SRAM**，排除 cache 一致性干扰；
- 额外给出 HDMA 在 **AHB SRAM(0xF0200000)** 内部的带宽（HDMA 的最佳路径）。

## 硬件要求 / 接线

无外部接线。HPM5E31 有 2 个 DMA 控制器：XDMA 与 HDMA，各 16 通道（DMA_SOC_CHANNEL_NUM=32），
DMAMUX 输出 0~31 接 HDMA、32~63 接 XDMA（RM §DMA 开头）。本示例只做内存搬移，不需要 DMAMUX 请求源。

## 编译与运行

```bash
bash scripts/build.sh dma/dma_mem2mem
bash scripts/hwrun.sh dma/dma_mem2mem --secs 10 --send "dmainfo;dmasweep;dmawidth 4096;dmahb;dmachain"
```

## 命令

| 命令 | 说明 |
|---|---|
| dmainfo | 打印 XDMA/HDMA 基址、最大位宽、最大突发、测试缓冲大小 |
| dmasweep [bytes] | 突发 8B→最大值扫描，两个控制器各一遍，打印 MB/s 并校验 |
| dmawidth [bytes] | XDMA 显式位宽(1/2/4/8B) × 突发个数 矩阵 |
| dmahb | HDMA 在 AHB SRAM 内部搬移的带宽（低延迟路径） |
| dmachain | 3 段链式描述符搬移 + 校验 |

## 实测现象（串口输出）

日志：`logs/20261030-110400-dma_mem2mem.log`（dmasweep/dmawidth）、`logs/20261030-110512-dma_mem2mem.log`（dmahb/dmachain）

```text
DMAv2 on HPM5E31: 2 controllers, 16 channels each
  XDMA @0xf00c4000: width<=8 B, burst<=1024 T
  HDMA @0xf00c8000: width<=4 B, burst<=128 T
  test buffer: 16384 B, non-cacheable AXI SRAM, mchtmr=24000000 Hz

XDMA sweep (size=16384 B):
  burst=    8 B      51.0 MB/s  (321.29 us, 50.99 MB/s per copy)
  burst=   32 B     168.9 MB/s  ( 97.00 us, 168.91 MB/s per copy)
  burst=  128 B     385.9 MB/s  ( 42.46 us, 385.88 MB/s per copy)
  burst=  512 B     402.1 MB/s  ( 40.75 us, 402.06 MB/s per copy)
  burst= 8192 B     406.2 MB/s  ( 40.33 us, 406.21 MB/s per copy)
HDMA sweep (size=16384 B):
  burst=    8 B      24.1 MB/s  (678.92 us, 24.13 MB/s per copy)
  burst=   64 B     117.7 MB/s  (139.21 us, 117.69 MB/s per copy)
  burst=  128 B      25.9 MB/s  (632.21 us, 25.92 MB/s per copy)   <-- 异常掉速
  burst=  512 B      26.0 MB/s  (630.88 us, 25.97 MB/s per copy)
done (all data verified by memcmp)

XDMA explicit width/burst matrix (size=4096 B):
  width=1 B burst=1T       7.2 MB/s ...
  width=4 B burst=32T     234.6 MB/s ...
  width=8 B burst=64T     348.6 MB/s  ( 11.75 us)
verify: match

HDMA AHB_SRAM(0xf0200000) -> AHB_SRAM(0xf0202000), 8192 B:
  burst=    8 B      98.7 MB/s  ( 82.96 us)
  burst=   64 B     305.3 MB/s  ( 26.83 us)
  burst=  512 B     327.7 MB/s  ( 25.00 us)

chained 3x4096 B done in 25.33 us (485.1 MB/s)
verify: match
```

实测结论（数据见第 19 章表与图）：

| 场景 | 峰值带宽 | 说明 |
|---|---|---|
| XDMA → AXI SRAM，burst ≥ 2 KB | **407 MB/s** | 受 AXI SRAM 带宽限制（读+写各占一半） |
| XDMA 显式 8B 位宽 + 64T 突发 | 349 MB/s | 4096B 小拷贝有启动开销 |
| HDMA → AXI SRAM，burst ≤ 64 B | 118 MB/s | 走 AHB→AXI 桥 |
| HDMA → AXI SRAM，burst ≥ 128 B | **26 MB/s（掉 4.5 倍）** | 见"坑 2" |
| HDMA → AHB SRAM，burst ≥ 64 B | **328 MB/s** | HDMA 的最佳路径 |
| 链式 3 段（每段 4 KB） | 485 MB/s | 描述符开销可忽略 |

## 关键点 / 坑

1. **cache 一致性**：DMA 搬移的缓冲必须放在 non-cacheable 区（ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT），否则 CPU 侧 memcmp 可能读到 cache 里的旧数据。
2. **HDMA 访问 AXI SRAM 时突发 ≥128 B 会掉速**：实测 117.7 MB/s(64B) → 25.9 MB/s(128B)。HDMA 是 AHB 主设备，AXI SRAM 在另一侧，突发跨过 AHB 桥能力后效率骤降。建议 HDMA 只用于"外设寄存器 ↔ AHB SRAM"，或把 AXI SRAM 访问的突发限制在 ≤64 B。
3. **dma_start_memcpy() 会自动选位宽**（按 burst/地址/长度对齐取最大），返回 status_invalid_argument(2) 常见原因：突发不是 2^n 字节、或突发超过 DMA_SOC_TRANSFER_WIDTH_MAX * TRANSFER_PER_BURST_MAX。本示例 dmahb 的 1024/2048/4096/8192 B 突发就因此被拒绝（HDMA 上限 128T×4B=512B）。
4. **链式搬移的每段长度必须能被突发整除**：size_in_byte 不是突发字节数的整数倍时 dma_config_linked_descriptor() 返回失败（本示例最初用 16384/3 就踩到了，改成 /4 后通过）。
5. mchtmr 是 24 MHz 自由运行计数器（clock_mchtmr0），用于 µs 级计时；不要用 bsp_uptime_ms() 做几十 µs 的测量（1 ms 分辨率不够）。
