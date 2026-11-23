#!/usr/bin/env python3
"""fig-23-2: 内存拷贝/填充带宽 vs 传输大小（cache / DLM / 非cacheable）

数据来源: logs/20261030-111657-bench_memory.log（bench_memory 例程 table 1/table 2）
  单位 MB/s；cache = AXI SRAM(.bss) 走 L1 D-Cache；dlm = 0x00200000；nc = 非 cacheable AXI SRAM
输出: docs/figures/23/fig-23-2-memory-bw.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sizes = [1, 4, 16, 64, 256, 1024, 4096, 16384, 32768, 65536]
# table 1: cache 区域
libc_memcpy = [53.3, 80.0, 83.5, 92.5, 95.1, 95.8, 95.9, 96.0, 95.9, 95.9]
libc_memset = [68.6, 137.1, 170.7, 217.9, 234.1, 238.5, 239.6, 239.9, 239.9, 240.0]
copy_u32 = [60.0, 213.3, 426.6, 520.6, 605.3, 630.9, 637.7, 626.3, 622.5, 589.5]
copy_unroll16 = [34.3, 120.0, 349.1, 698.1, 859.2, 898.5, 922.5, 928.7, 892.8, 784.8]
memset_u32 = [68.6, 320.0, 548.5, 714.4, 883.9, 939.7, 954.8, 958.6, 959.3, 959.6]

fig, ax = plt.subplots(figsize=(7.4, 4.4), dpi=300)
ax.plot(sizes, libc_memcpy, "o-", label="libc memcpy (newlib-nano, byte-wise)", color="#c53030")
ax.plot(sizes, libc_memset, "s-", label="libc memset", color="#dd6b20")
ax.plot(sizes, copy_u32, "^-", label="hand copy_u32", color="#2b6cb0")
ax.plot(sizes, copy_unroll16, "v-", label="hand copy 16-word unroll", color="#2f855a")
ax.plot(sizes, memset_u32, "D-", label="hand memset_u32", color="#6b46c1")
ax.axhline(960, color="#888", linestyle="--", linewidth=1)
ax.text(1.1, 975, "AXI(S) 240MHz x 32bit = 960 MB/s", fontsize=7.5, color="#555")
ax.set_xscale("log", base=2)
ax.set_xlabel("transfer size (byte, log2)")
ax.set_ylabel("throughput (MB/s)")
ax.set_title("HPM5E31 memory throughput vs size (D-Cache on, -O3)", fontsize=10)
ax.set_ylim(0, 1100)
ax.grid(True, which="both", linestyle=":", alpha=0.45)
ax.legend(fontsize=7.5, loc="lower right")
fig.tight_layout()
out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "23", "fig-23-2-memory-bw.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("wrote", os.path.abspath(out))
