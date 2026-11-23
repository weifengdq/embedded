#!/usr/bin/env python3
"""第 21 章 XPI/Flash 插图: 镜像布局 + 擦写读速度 (数据来自 logs/20261030-110952/113129-flash_rw_erase.log)"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

fig, ax = plt.subplots(figsize=(9, 3.4))
regions = [
    (0x00000, 0x00400, "0x0000-0x03FF\n(reserved)", "#bdbdbd"),
    (0x00400, 0x00010, "0x0400\nnor_cfg_option", "#ef6c00"),
    (0x01000, 0x00090, "0x1000\nboot_header", "#6a1b9a"),
    (0x01090, 0x01F70, "unused", "#e0e0e0"),
    (0x03000, 0x0FA30, "0x3000: app image (fw[0], 62.5 KB)", "#1565c0"),
    (0x12A30, 0xDD5D0, "unused", "#e0e0e0"),
    (0xF0000, 0x10000, "0xF0000-0xFFFFF\nrw test safe window (64 KB)", "#2e7d32"),
]
for off, size, label, c in regions:
    ax.add_patch(Rectangle((off, 0), size, 1, color=c))
    ax.text(off + size/2, 0.5, label, ha="center", va="center", fontsize=7, color="white" if c not in ("#e0e0e0","#bdbdbd") else "black")
ax.set_xlim(0, 0x100000); ax.set_ylim(0, 1)
ax.set_yticks([]); ax.set_xlabel("flash offset from 0x8000_0000 (bytes)")
ax.set_xticks([0, 0x1000, 0x3000, 0x20000, 0x80000, 0xF0000, 0x100000])
ax.set_xticklabels(["0", "0x1000", "0x3000", "0x20000", "0x80000", "0xF0000", "0x100000"], fontsize=7)
ax.set_title("Internal 1 MB flash layout (measured: logs/20261030-120558 + demo.map)", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/21/fig-21-1-flash-layout.png", dpi=160); plt.close()

fig, ax = plt.subplots(figsize=(8, 3.6))
labels = ["sector erase\n(4 KB)", "page program\n(4 KB)", "ROM API read\n(4 KB)", "XIP read cold\n(16 KB)", "XIP read warm\n(16 KB)"]
vals = [2.13, 0.28, 30.0, 45.7, 88.2]
colors = ["#c62828", "#ef6c00", "#1565c0", "#2e7d32", "#66bb6a"]
bars = ax.bar(labels, vals, color=colors)
ax.set_yscale("log"); ax.set_ylabel("MB/s (log)")
for b, v in zip(bars, vals):
    ax.text(b.get_x() + b.get_width()/2, v*1.15, f"{v:g}", ha="center", fontsize=8)
ax.grid(axis="y", which="both", alpha=.3)
ax.set_title("Flash erase / program / read throughput (measured on hpm5e31_1)", fontsize=9)
plt.xticks(fontsize=7); plt.tight_layout()
plt.savefig("docs/figures/21/fig-21-2-flash-throughput.png", dpi=160); plt.close()
print("ch21 figs ok")
