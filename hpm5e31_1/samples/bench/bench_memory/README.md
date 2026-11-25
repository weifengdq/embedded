# bench_memory —— 内存带宽基准（memcpy / memset / memcmp）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

测 **memcpy / memset / memcmp** 在不同传输大小（1B~64KB）、不同存储位置
（可缓存 AXI SRAM / 非 cacheable AXI SRAM / DLM）下的吞吐（MB/s），并对比：

- **libc 库函数**（newlib-nano）与**手写 32 位循环**、**16 字展开循环**；
- 跨区域拷贝（cache↔非cacheable、cache↔DLM）；
- 目标地址非对齐（偏移 0/1/2/3 字节）。

方法：mcycle 计时（480MHz，约 2ns 分辨率），每个测点重复 3 次取最好；通过函数指针调用避免内联。

## 接线

无。只用 UART4 921600。

## 编译与运行

```bash
bash scripts/build.sh bench/bench_memory            # 默认 -O3
bash scripts/hwrun.sh bench/bench_memory --secs 25
```

内存布局（靠 linker 的 section 名，无需改链接脚本）：

| 缓冲区 | section | 实际位置 |
|---|---|---|
| `s_cache_a/b`（64KB×2） | 默认 `.bss` | AXI SRAM 0x0120xxxx，**走 L1 D-Cache** |
| `s_nc_a/b`（16KB×2） | `.noncacheable.bss` | AXI_SRAM_NONCACHEABLE（64KB 区） |
| `s_dlm_a/b`（32KB×2） | `.fast_ram.bss` | **DLM 0x00200000** |

## 串口命令

| 命令 | 说明 |
|---|---|
| `membw` | 跑完整扫描（约 5s） |
| `membwinfo` | 打印缓冲区地址/大小与优化选项 |

## 实测数据（`logs/20261030-111657-bench_memory.log`）

table 1：可缓存 AXI SRAM（L1 D-Cache），单位 MB/s，-O3：

| size(B) | libc memcpy | libc memset | copy_u32 | unroll16 | memset_u32 |
|---|---|---|---|---|---|
| 1 | 53.3 | 68.6 | 60.0 | 34.3 | 68.6 |
| 4 | 80.0 | 137.1 | 213.3 | 120.0 | 320.0 |
| 16 | 83.5 | 170.7 | 426.6 | 349.1 | 548.5 |
| 64 | 92.5 | 217.9 | 520.6 | 698.1 | 714.4 |
| 256 | 95.1 | 234.1 | 605.3 | 859.2 | 883.9 |
| 1024 | 95.8 | 238.5 | 630.9 | 898.5 | 939.7 |
| 4096 | 95.9 | 239.6 | 637.7 | 922.5 | 954.8 |
| 16384 | 96.0 | 239.9 | 626.3 | 928.7 | 958.6 |
| 32768 | 95.9 | 239.9 | 622.5 | 892.8 | 959.3 |
| 65536 | 95.9 | 240.0 | 589.5 | 784.8 | 959.6 |

table 2：三种存储位置 @ 4096B，单位 MB/s：

| 内核 | cache | dlm | nc（非cacheable） |
|---|---|---|---|
| copy_u32 | 745.0 | 763.2 | 79.8 |
| copy_unroll16 | 923.0 | 922.9 | 80.0 |
| memset_u32 | 952.5 | 952.5 | 425.3 |

跨区域（4096B，copy_u32）：c2nc=79.7 / nc2c=425.5 / c2dlm=637.7 / dlm2c=637.7 MB/s。
对齐：dst 偏移 0/1/2/3 字节都是 95.77 MB/s（因为 libc memcpy 是逐字节实现，对齐无所谓）。

**结论**

1. **newlib-nano 的 memcpy 是逐字节实现**：480MHz 下只有 **95.9 MB/s（约 5 周期/字节）**，
   测到的是 CPU 指令吞吐，不是内存带宽 —— 用它测"内存带宽"会得到完全错误的结论。
2. 换成 16 字展开内核后 **922.9 MB/s**，memset_u32 达 **959.6 MB/s**，正好等于
   AXI(S) 240MHz × 32bit = 960 MB/s 的理论上限（`busclocks` 可验证 AXI 时钟）。
3. **DLM 与可缓存 AXI SRAM 吞吐几乎相同**（923 vs 922.9）：数据能进 L1 D-Cache 时，
   DLM 的优势不体现在"带宽"上（体现在确定性和中断响应上，见第 24 章）。
4. **非 cacheable AXI SRAM 慢 11.5 倍**（copy 80 vs 923 MB/s），因为每个字都是一次独立 AXI 事务；
   但**写**比读快很多（memset_u32 425 MB/s），写有缓冲、读是阻塞的。
5. 64KB 时 unroll16 掉到 784.8 MB/s（源+目的 128KB 超过 L1 D-Cache 容量，开始反复 miss）。

## 易错点与注意事项

1. **命令名冲突**：BSP 里已有 `mem`（hexdump），本样本注册的是 `membw`，否则链接期报
   `multiple definition of shellCommandmem`。
2. **`.bss` 不是"非 cacheable"**：本 SoC 上 `.bss` 默认落在 AXI SRAM 且**可缓存**；
   要非 cacheable 必须显式用 `.noncacheable.bss`（链接脚本 `.noncacheable` → AXI_SRAM_NONCACHEABLE）。
3. **DLM 是可写段 `.fast_ram.bss`，不是 `.fast`**：`.fast` 是 ILM 里的**代码**段，别混。
4. **C 语言参数求值顺序易错点**：第一版写成
   `report(op, region, size, reps, cyc, measure(...))`，`cyc` 可能在 `measure()` 之前被读取，
   导致日志里出现 `cyc_per_byte=35997024256.000` 这种乱码。**MB/s 值本身是对的**（那是 measure 的返回值），
   但 cyc_per_byte 不可信 —— 已改为先调用再传参。
5. **memcmp 必须内容相同**：第一版两个缓冲区填了不同值（0x5a / 0xa5），memcmp 第一个字节就返回，
   测出 47 GB/s 的假值。现在两个缓冲区同内容，才会扫完全部字节。
6. 小尺寸（1~16B）测的是**函数调用开销**，不是带宽：1B 时 libc memcpy 53 MB/s 约 9 周期/次。

## 参考

- 链接脚本：`$HPM_SDK_BASE/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`
- 缓存驱动：`$HPM_SDK_BASE/arch/riscv/l1c/hpm_l1c_drv.h`
