# bench_bus —— 总线 / 存储吞吐（XPI flash 直读 + UART + 时钟树）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

1. **XPI flash 顺序读带宽**（MB/s）：
   - `cold`：每一轮拷贝前 `l1c_dc_invalidate()` 掉 flash 区间，强制每轮走 XPI 总线；
   - `warm`：不失效，走 L1 D-Cache（命中）；
   - 两种搬运内核：libc `memcpy`（newlib-nano，逐字节）与 16 字展开内核。
2. **UART 实际发送吞吐**（B/s、bit/s）vs 配置 921600；等 TEMT 真正发送完才停表。
3. **总线时钟树**（ahb/axi-s/axi-c/axi-f/xpi0/mchtmr），用于换算理论带宽。

## 接线

无。UART 用板载 UART4 921600（也是控制台）。

## 编译与运行

```bash
bash scripts/build.sh bench/bench_bus
bash scripts/hwrun.sh bench/bench_bus --secs 16
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `bus` | 时钟树 + XPI 读 + UART 全跑 |
| `xiprep` | 只测 XPI 读带宽 |
| `uartbw` | 只测 UART 发送吞吐 |
| `busclocks` | 只打印总线时钟 |
| `businfo` | 缓冲区地址/大小/优化选项 |

## 实测数据（`logs/20261030-113730-bench_bus.log`）

时钟树：CPU 480MHz、AHB/AXI-S/AXI-C/AXI-F 全部 200MHz、XPI0 100MHz、MCHTMR 24MHz。

XPI flash 顺序读（MB/s）：

| size | memcpy cold | memcpy warm | unroll16 cold | unroll16 warm |
|---|---|---|---|---|
| 1 KB | 37.94 | 159.17 | 38.71 | 869.85 |
| 4 KB | 39.40 | 159.79 | 39.65 | 894.79 |
| 16 KB | 39.46 | 159.95 | 39.85 | 901.25 |
| 32 KB | 39.90 | 148.54 | 39.91 | 561.65 |
| 64 KB | 39.89 | 43.42 | 39.90 | 43.43 |

**结论**：

- **XPI 顺序读 ≈ 40 MB/s（cold，与内核无关：12.03 周期/字节）**。XPI0 100MHz × Quad 4bit = 50MB/s 理论值，
  实测利用率 80%；与 tp-sys 独立测量的 45.7MB/s 同量级（测试条件不同，见第 24 章）；
- **warm（cache 命中）**：用展开内核可达 901MB/s；用 libc memcpy 只有 160MB/s —— 说明 libc memcpy 是 CPU 瓶颈；
- 64KB 时 warm 也掉到 43MB/s：源 64KB + 目的 64KB 超过 L1 D-Cache，cache 完全失效。

UART（`logs/20261030-115014-bench_bus.log`）：

| 方法 | 实测 | 占理论值(92160 B/s) |
|---|---|---|
| SDK 控制台路径：16KB 连续 printf + 等 TEMT | **85,789 B/s = 857.9 kbit/s** | **93.1%** |
| 裸写 THR 寄存器（逐字节等 THRE） | 6,351,441 B/s（假值） | 6.9 倍，不可能 |

裸写寄存器那次在串口抓包里连一个字母都没看到 —— 字节被丢弃。测 UART 吞吐请用 SDK 发送路径或 DMA。

## 坑与注意事项

1. **cold 必须在循环里 invalidate**：第一版只在拷贝循环外 invalidate 一次，结果 4KB/16KB 的"cold"
   其实是"第一次 cold + 后面全 warm"，测出 153MB/s 的偏高值。只有 64KB（超 cache）才自动每轮 cold。
2. **UART 吞吐测量坑（已定位）**：直接 `while (!THRE) write(THR)` 逐字节写寄存器只会填 FIFO / 丢字节，
   本地测出 6.3MB/s（理论上限 92KB/s 的 68 倍），明显是假值；而且抓包里连一串字母都没出现，
   说明大量字节被丢弃。正确做法：等 `uart_stat_transmitter_empty`（TEMT）再停表，
   或用 DMA 发送 + 统计完成字节。本仓该项列为"待确认"。
3. **flash 区间 invalidate 是安全的**（只读数据没有 dirty line）；但**绝不能对可写数据**做
   `l1c_dc_invalidate`（写回缓存会丢数据），可写数据要 `l1c_dc_flush`。
4. **`.rodata` 落在 FLASH**：本 SoC 链接脚本把 `*(.rodata)` 归进 `.text` 段（`> FLASH`），
   所以 const 大数组确实在 flash 里，可以直接当 XIP 读的源。
5. **串口抓包 0.3s 延迟**：本样本 `main()` 里先 `board_delay_ms(1500)` 再自动测量。

## 参考

- 链接脚本：`$HPM_SDK_BASE/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（FLASH/ILM/DLM/AXI_SRAM 映射）
- L1 Cache：`$HPM_SDK_BASE/arch/riscv/l1c/hpm_l1c_drv.h`（`l1c_dc_invalidate` / `l1c_dc_flush`）
- UART：`$HPM_SDK_BASE/drivers/inc/hpm_uart_drv.h`（`uart_stat_transmitter_empty` / `uart_stat_tx_slot_avail`）