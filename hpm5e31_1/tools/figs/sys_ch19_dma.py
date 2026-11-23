#!/usr/bin/env python3
"""第 19 章 DMA 插图: DMAv2 总线拓扑 + 实测带宽 (数据来自 logs/20261030-110400/110512-dma_mem2mem.log)"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

def box(ax, x, y, w, h, t, fc="#e8f0fe"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02", fc=fc, ec="#33517a"))
    ax.text(x + w/2, y + h/2, t, ha="center", va="center", fontsize=8)

fig, ax = plt.subplots(figsize=(9, 4.2))
box(ax, 0.02, 0.62, 0.16, 0.22, "CPU0 (480 MHz)\n+ L1 D-cache")
box(ax, 0.02, 0.28, 0.16, 0.18, "AXI SRAM 256KB\n(0x0120_0000)")
box(ax, 0.02, 0.05, 0.16, 0.16, "AHB SRAM 32KB\n(0xF020_0000)")
box(ax, 0.30, 0.62, 0.20, 0.22, "AXI fabric", fc="#fff4e5")
box(ax, 0.30, 0.28, 0.20, 0.18, "AHB peripheral bus", fc="#fff4e5")
box(ax, 0.62, 0.72, 0.16, 0.16, "XDMA (16 ch)\n8B width, 1024T")
box(ax, 0.62, 0.44, 0.16, 0.16, "HDMA (16 ch)\n4B width, 128T")
box(ax, 0.62, 0.10, 0.16, 0.16, "DMAMUX\n64 outputs", fc="#e6f4ea")
box(ax, 0.86, 0.10, 0.12, 0.16, "UART/SPI/I2C\nADC/PWM ...", fc="#e6f4ea")
for p1, p2 in [((0.18,0.73),(0.30,0.73)),((0.18,0.37),(0.30,0.37)),((0.18,0.13),(0.30,0.37)),
               ((0.50,0.73),(0.62,0.80)),((0.50,0.37),(0.62,0.52)),((0.62,0.26),(0.78,0.18)),((0.78,0.18),(0.86,0.18))]:
    ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle="<|-|>", mutation_scale=8, color="#33517a"))
ax.set_xlim(0,1); ax.set_ylim(0,1); ax.axis("off")
ax.set_title("HPM5E31 DMAv2 topology: XDMA on AXI, HDMA on AHB, 64 DMAMUX outputs (RM 33 p.561)", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/19/fig-19-1-dmav2-topology.png", dpi=160); plt.close()

labels = ["XDMA->AXI\nburst 8KB", "XDMA->AXI\nburst 8B", "XDMA 8B width\n64T burst", "chain 3x4KB\n(descriptor)",
          "HDMA->AHB SRAM\nburst 512B", "HDMA->AXI SRAM\nburst 64B", "HDMA->AXI SRAM\nburst 128B"]
vals = [407.1, 51.0, 348.6, 485.1, 327.7, 117.7, 25.9]
colors = ["#2e7d32", "#c62828", "#2e7d32", "#1565c0", "#2e7d32", "#f9a825", "#c62828"]
fig, ax = plt.subplots(figsize=(9, 3.8))
bars = ax.bar(labels, vals, color=colors)
for b, v in zip(bars, vals):
    ax.text(b.get_x() + b.get_width()/2, v + 6, f"{v:.0f}", ha="center", fontsize=8)
ax.set_ylabel("MB/s"); ax.set_ylim(0, 540); ax.grid(axis="y", alpha=.3)
ax.set_title("DMAv2 memory-to-memory bandwidth (16 KB copy, non-cacheable buffers, DLM/ILM not tested)", fontsize=9)
plt.xticks(fontsize=7); plt.tight_layout()
plt.savefig("docs/figures/19/fig-19-2-dma-bandwidth.png", dpi=160); plt.close()
print("ch19 figs ok")
