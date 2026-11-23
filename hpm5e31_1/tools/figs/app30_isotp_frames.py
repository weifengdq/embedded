#!/usr/bin/env python3
"""图 29-1：ISO-TP(ISO 15765-2:2016) 四类 N_PCI 帧的逐字节格式（8 字节经典帧）

生成：python3 tools/figs/app30_isotp_frames.py
产物：docs/figures/29/fig-29-1-frames.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

plt.rcParams["font.sans-serif"] = ["Noto Sans CJK SC", "Noto Sans SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

D = "#dfe7ea"   # 数据
P = "#8ecae6"   # SF PCI
F = "#ffb703"   # FF PCI
C = "#90be6d"   # CF PCI
X = "#e76f51"   # FC PCI
Q = "#f4a261"   # FC 参数
Z = "#e9ecef"   # padding


def row(name, cells, note):
    return (name, cells, note)


ROWS = [
    row("SF 单帧\n(经典 DL 1..7)",
        [("0x0L", "N_PCI", P), ("D1", "数据", D), ("D2", "数据", D), ("D3", "数据", D),
         ("D4", "数据", D), ("D5", "数据", D), ("D6", "数据", D), ("D7", "数据", D)],
        "SF_DL=1..7 放低 4 位；CAN FD 下 DL=8..62 时首字节 0x00、第二字节为 DL，共 8~62 B 数据"),
    row("FF 首帧\n(DL 8..4095)",
        [("0x1H", "N_PCI", F), ("0xLL", "FF_DL 低 8 位", F), ("D1", "首段数据", D), ("D2", "首段数据", D),
         ("D3", "首段数据", D), ("D4", "首段数据", D), ("D5", "首段数据", D), ("D6", "首段数据", D)],
        "经典帧首帧带 6 B 数据；FF_DL=0 时改用后随 4 B 的 32 位长度（CAN FD 首帧带 62 B 数据）"),
    row("CF 连续帧",
        [("0x2N", "N_PCI", C), ("D1", "数据", D), ("D2", "数据", D), ("D3", "数据", D),
         ("D4", "数据", D), ("D5", "数据", D), ("D6", "数据", D), ("D7", "数据", D)],
        "SN 从 1 递增到 15 后回 1（不出现 0）；经典帧每帧 7 B 有效载荷，CAN FD 每帧 63 B"),
    row("FC 流控帧",
        [("0x3F", "N_PCI", X), ("BS", "BlockSize", Q), ("STmin", "最小间隔", Q), ("0xCC", "padding", Z),
         ("0xCC", "padding", Z), ("0xCC", "padding", Z), ("0xCC", "padding", Z), ("0xCC", "padding", Z)],
        "FS: 0=CTS 1=WAIT 2=OVFLW；BS=0 表示不再分块；STmin 单位 ms（0xF1~0xF9 为 100~900 us）"),
]

fig, ax = plt.subplots(figsize=(11.2, 5.6), dpi=300)
ax.set_xlim(-3.9, 8.25)
ax.set_ylim(-0.85, 4.35)
ax.axis("off")

for r, (name, cells, note) in enumerate(ROWS):
    y = 3.15 - r * 1.02
    ax.text(-0.12, y + 0.30, name, ha="right", va="center", fontsize=9.5, weight="bold")
    for col, (label, sub, color) in enumerate(cells):
        ax.add_patch(Rectangle((col, y), 1.0, 0.64, facecolor=color, edgecolor="#333", lw=0.8))
        ax.text(col + 0.5, y + 0.42, label, ha="center", va="center", fontsize=8.6, weight="bold")
        ax.text(col + 0.5, y + 0.16, sub, ha="center", va="center", fontsize=6.9, color="#222")
    ax.text(0.0, y - 0.20, note, ha="left", va="center", fontsize=7.4, color="#444")

for c in range(8):
    ax.text(c + 0.5, 3.86, "byte %d" % c, ha="center", va="center", fontsize=8, color="#555")
ax.text(4.0, 3.93, "ISO-TP(ISO 15765-2:2016) 四类帧的逐字节格式（经典 8 字节帧）",
        ha="center", va="bottom", fontsize=10.5, weight="bold")
plt.tight_layout()
os.makedirs("docs/figures/30", exist_ok=True)
plt.savefig("docs/figures/29/fig-29-1-frames.png", bbox_inches="tight", facecolor="white")
print("ok docs/figures/29/fig-29-1-frames.png")
