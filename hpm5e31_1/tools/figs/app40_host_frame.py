#!/usr/bin/env python3
"""图 41-2：gs_host_frame 布局与 4 种合法尺寸

生成：python3 tools/figs/app40_host_frame.py
产物：docs/figures/41/fig-41-2-host-frame.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

plt.rcParams["font.sans-serif"] = ["Noto Sans CJK SC", "Noto Sans SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

HDR = "#8ecae6"   # 12 B 头
DAT = "#dfe7ea"   # 数据区
TS  = "#ffb703"   # 时间戳
RES = "#e9ecef"

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11.2, 6.0), dpi=300,
                               gridspec_kw={"height_ratios": [1.0, 1.0]})

# --- 上：12 字节头逐字段 ---
ax1.set_xlim(-2.2, 12.3)
ax1.set_ylim(-0.9, 2.2)
ax1.axis("off")
ax1.text(6.0, 1.95, "gs_host_frame 头（12 B，小端 packed）", ha="center", fontsize=10.5, weight="bold")
fields = [(0, 4, "echo_id\nu32", HDR), (4, 4, "can_id\nu32（含 EFF/RTR/ERR 标志）", HDR),
          (8, 1, "can_dlc\nu8（DLC 编码）", "#ffd9a0"), (9, 1, "channel\nu8 (0..3)", "#ffd9a0"),
          (10, 1, "flags\nu8", "#90be6d"), (11, 1, "rsv", RES)]
for (x, w, label, color) in fields:
    ax1.add_patch(Rectangle((x, 0.55), w, 0.7, facecolor=color, edgecolor="#333", lw=0.8))
    ax1.text(x + w / 2, 0.9, label, ha="center", va="center", fontsize=7.6)
for c in range(12):
    ax1.text(c + 0.5, 0.30, str(c), ha="center", va="center", fontsize=7, color="#555")
ax1.text(-0.1, 0.30, "byte", ha="right", va="center", fontsize=7.5, color="#555")
ax1.text(0.0, -0.35, "flags: bit0 OVERFLOW / bit1 FD / bit2 BRS / bit3 ESI；echo_id=0xFFFFFFFF 表示 RX 帧，"
                     "否则是 TX 回显序号（< GS_MAX_TX_URBS=10）",
         ha="left", va="center", fontsize=7.8, color="#444")

# --- 下：4 种尺寸 ---
ax2.set_xlim(-2.2, 86)
ax2.set_ylim(-0.9, 4.6)
ax2.axis("off")
ax2.text(42, 4.35, "内核要求的 4 种帧尺寸（少了任一字节都会被丢弃/报错）", ha="center",
         fontsize=10.5, weight="bold")
rows = [
    ("经典 TX\n主机→设备", 12, 8, 0, 20, "20 B"),
    ("经典 RX\n设备→主机(带时间戳)", 12, 8, 4, 24, "24 B"),
    ("FD TX\n主机→设备", 12, 64, 0, 76, "76 B"),
    ("FD RX\n设备→主机(带时间戳)", 12, 64, 4, 80, "80 B"),
]
for i, (name, h, d, t, total, tag) in enumerate(rows):
    y = 3.35 - i * 0.95
    ax2.text(-0.4, y + 0.25, name, ha="right", va="center", fontsize=8.5, weight="bold")
    ax2.add_patch(Rectangle((0, y), h, 0.5, facecolor=HDR, edgecolor="#333", lw=0.8))
    ax2.text(h / 2, y + 0.25, "12", ha="center", va="center", fontsize=7.5)
    if d:
        ax2.add_patch(Rectangle((h, y), d, 0.5, facecolor=DAT, edgecolor="#333", lw=0.8))
        ax2.text(h + d / 2, y + 0.25, "data[%d]" % d, ha="center", va="center", fontsize=7.2)
    if t:
        ax2.add_patch(Rectangle((h + d, y), t, 0.5, facecolor=TS, edgecolor="#333", lw=0.8))
        ax2.text(h + d + t / 2, y + 0.25, "ts", ha="center", va="center", fontsize=7.2)
    ax2.text(total + 1.5, y + 0.25, "= %s" % tag, ha="left", va="center", fontsize=8.2, weight="bold")
ax2.annotate("", xy=(12, -0.25), xytext=(0, -0.25), arrowprops=dict(arrowstyle="<->", color="#c62828"))
ax2.text(6, -0.55, "12 B 头", ha="center", va="center", fontsize=8, color="#c62828")
plt.tight_layout()
os.makedirs("docs/figures/40", exist_ok=True)
plt.savefig("docs/figures/41/fig-41-2-host-frame.png", bbox_inches="tight", facecolor="white")
print("ok docs/figures/41/fig-41-2-host-frame.png")
