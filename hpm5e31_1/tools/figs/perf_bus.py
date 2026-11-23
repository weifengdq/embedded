#!/usr/bin/env python3
"""fig-24-1: 总线/存储/外设实测吞吐横向对比（对数坐标柱状图）

数据来源（全部为 hpm5e31_1 实测；MB/s 换算 Mbit/s 时按 1MB=8Mbit）:
  AXI SRAM memset_u32 (CPU)     : 959.6 MB/s  logs/20261030-111657-bench_memory.log
  AXI SRAM copy unroll16 (CPU)  : 922.5 MB/s  同上
  DLM copy unroll16 (CPU)       : 922.9 MB/s  同上
  XDMA mem2mem nc (8KB burst)   : 406.0 MB/s  logs/20261030-110400-dma_mem2mem.log (tp-sys)
  XDMA 链式 3x4KB               : 485.0 MB/s  logs/20261030-110512-dma_mem2mem.log (tp-sys)
  HDMA (AHB SRAM 8KB)           : 327.7 MB/s  tp-sys 实测
  libc memcpy (newlib-nano)     : 95.9 MB/s   logs/20261030-111657-bench_memory.log
  YT8011AN 1000BASE-T1（最终值，tp-net2）: TCP 下行 902.3 / TCP 上行 466.3 / UDP 下行 944 / UDP 上行 641.5 Mbit/s
    TCP下行 = 112.8 MB/s  UDP下行 = 118.0  UDP上行 = 80.2  TCP上行 = 58.3
    logs/20261030-153556 / -153645 / -153733-enet_lwip_iperf.log
  XPI flash 顺序读 cold         : 40.0 MB/s    logs/20261030-113730-bench_bus.log (本仓) / 46.0 (tp-sys)
  CAN FD 1M/2M 64B 单向 payload : 0.809 Mbit/s = 0.101 MB/s  logs/20261030-112832-mcan_two_node.log (tp-can)
  CAN 2.0 500k 8B 单向 payload  : 0.275 Mbit/s = 0.0344 MB/s 同上
输出: docs/figures/24/fig-24-2-bus-bw.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

rows = [
    ("AXI SRAM memset_u32 (CPU)", 959.6, "#2b6cb0"),
    ("AXI SRAM copy unroll16 (CPU)", 922.5, "#2b6cb0"),
    ("DLM copy unroll16 (CPU)", 922.9, "#2b6cb0"),
    ("XDMA chained 3x4KB", 485.0, "#2f855a"),
    ("XDMA mem2mem non-cacheable", 406.0, "#2f855a"),
    ("HDMA (AHB SRAM)", 327.7, "#2f855a"),
    ("YT8011AN UDP down (944M)", 118.0, "#b7791f"),
    ("YT8011AN TCP down (902M)", 112.8, "#b7791f"),
    ("YT8011AN UDP up (641M)", 80.2, "#d69e2e"),
    ("YT8011AN TCP up (466M)", 58.3, "#d69e2e"),
    ("libc memcpy (newlib-nano)", 95.9, "#dd6b20"),
    ("XPI flash sequential read", 40.0, "#6b46c1"),
    ("CAN FD 1M/2M 64B payload", 0.101, "#c53030"),
    ("CAN 2.0 500k 8B payload", 0.0344, "#c53030"),
]
names = [r[0] for r in rows][::-1]
vals = [r[1] for r in rows][::-1]
cols = [r[2] for r in rows][::-1]

fig, ax = plt.subplots(figsize=(8.2, 4.8), dpi=300)
bars = ax.barh(names, vals, color=cols, height=0.62)
ax.set_xscale("log")
ax.set_xlabel("measured effective throughput (MB/s, log scale)")
ax.set_title("HPM5E31 measured bus / memory / peripheral throughput", fontsize=10)
for b, v in zip(bars, vals):
    txt = f"{v:.4g}" if v < 1 else f"{v:.1f}"
    ax.text(v * 1.15, b.get_y() + b.get_height() / 2, txt, va="center", fontsize=7.5)
ax.set_xlim(0.01, 3000)
ax.grid(axis="x", which="both", linestyle=":", alpha=0.45)
fig.text(0.01, 0.01, "blue=on-chip RAM (CPU)  green=DMA  purple=XPI flash  orange=libc  red=serial bus", fontsize=7, color="#444")
fig.tight_layout(rect=(0, 0.03, 1, 1))
out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "24", "fig-24-2-bus-bw.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("wrote", os.path.abspath(out))
