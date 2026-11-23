#!/usr/bin/env python3
"""图 30-3：刷写时序与实测耗时（数据来自 logs/20261030-180524-uds_flash.log）

生成：python3 tools/figs/app31_flash_seq.py
产物：docs/figures/30/fig-30-3-flash-seq.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams["font.sans-serif"] = ["Noto Sans CJK SC", "Noto Sans SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

# 实测（32 KB 镜像，经典 500 kbit/s ISO-TP，256 B/块）
SEGMENTS = [
    ("10 02 编程会话", 0.0005, "#90caf9"),
    ("27 01/02 解锁", 0.0010, "#90caf9"),
    ("34 请求下载\n(擦除 32 KB, 14.7 ms)", 0.0161, "#ffb74d"),
    ("36 × 128 块\n(1.324 s, 24.2 KB/s)", 1.3245, "#a5d6a7"),
    ("37 退出", 0.0006, "#90caf9"),
    ("31 01 FF00\nCRC32", 0.0026, "#ce93d8"),
    ("31 01 FF01\n依赖检查", 0.0026, "#ce93d8"),
    ("11 01 复位", 0.0010, "#ef9a9a"),
]

fig, ax = plt.subplots(figsize=(11, 2.9), dpi=300)
left = 0.0
for name, dur, color in SEGMENTS:
    ax.barh(0, dur, left=left, height=0.55, color=color, edgecolor="#333", linewidth=0.6)
    ax.text(left + dur / 2, 0.42, name, ha="center", va="bottom", fontsize=7.4, rotation=0)
    left += dur
ax.set_xlim(0, left * 1.01)
ax.set_yticks([])
ax.set_xlabel("时间 (s)", fontsize=9)
ax.set_title("UDS 刷写 32 KB 镜像：总耗时 %.2f s，其中 0x36 数据传输占 %.0f%%"
             % (left, 1.3245 / left * 100), fontsize=10)
ax.grid(True, axis="x", ls=":", alpha=0.5)
ax.tick_params(labelsize=8)
plt.tight_layout()
os.makedirs("docs/figures/31", exist_ok=True)
plt.savefig("docs/figures/30/fig-30-3-flash-seq.png", bbox_inches="tight", facecolor="white")
print("ok docs/figures/30/fig-30-3-flash-seq.png  total=%.3f s" % left)
