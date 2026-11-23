#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_09_bittiming.py - MCAN 位时间结构（同步段/段1/段2/采样点）

公式来自 RM §51.4.2 (p.866)：bit_time = (1 + seg1 + seg2) * TQ，
采样点 = (1 + seg1)/(1 + seg1 + seg2)。数值为第 8 章 2.3 节 80 MHz 配置。
"""
import os
import matplotlib
matplotlib.use("Agg")
from matplotlib import font_manager
import matplotlib.pyplot as plt
font_manager.fontManager.addfont("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
plt.rcParams["font.sans-serif"] = ["Noto Sans CJK JP", "Noto Sans CJK SC"]
plt.rcParams["axes.unicode_minus"] = False

# (标签, baud, prescaler, seg1, seg2, 采样点%)
CFG = [
    ("500 kbit/s\nSP 87.5%", 500e3, 1, 139, 20),
    ("1 Mbit/s\nSP 87.5%", 1000e3, 1, 69, 10),
    ("2 Mbit/s\nSP 80.0%", 2000e3, 1, 31, 8),
    ("5 Mbit/s\nSP 87.5%", 5000e3, 1, 13, 2),
    ("8 Mbit/s\nSP 80.0%", 8000e3, 1, 7, 2),
]

fig, ax = plt.subplots(figsize=(10, 5.0), dpi=300)
for i, (label, baud, presc, seg1, seg2) in enumerate(CFG):
    n = 1 + seg1 + seg2
    bit_ns = n * presc / 80e6 * 1e9
    sync = 1 * presc / 80e6 * 1e9
    s1 = seg1 * presc / 80e6 * 1e9
    s2 = seg2 * presc / 80e6 * 1e9
    sp = 100.0 * (1 + seg1) / n
    y = len(CFG) - i
    ax.barh(y, sync, color="#bcbddc", edgecolor="#333333", height=0.55)
    ax.barh(y, s1, left=sync, color="#a1d99b", edgecolor="#333333", height=0.55)
    ax.barh(y, s2, left=sync + s1, color="#fdae6b", edgecolor="#333333", height=0.55)
    ax.plot([bit_ns * sp / 100.0] * 2, [y - 0.35, y + 0.35], color="#d62728", linewidth=2)
    ax.text(bit_ns + 8, y, f"{bit_ns:.0f} ns, {n} TQ, SP={sp:.1f}%", va="center", fontsize=8)
    ax.text(sync + s1 / 2, y, f"seg1={seg1}", ha="center", va="center", fontsize=7)
    ax.text(sync + s1 + s2 / 2, y, f"seg2={seg2}", ha="center", va="center", fontsize=7, rotation=0)
ax.set_yticks(range(1, len(CFG) + 1))
ax.set_yticklabels([c[0] for c in reversed(CFG)], fontsize=9)
ax.set_xlabel("位时间（ns，80 MHz CAN 时钟）")
ax.set_xlim(0, 3300)
ax.axvline(0, color="#333333", linewidth=0.8)
ax.set_title("MCAN 位时间结构：同步段(紫) + seg1(绿) + seg2(橙)，红线为采样点", fontsize=11)
ax.text(0.99, 0.02, "同步段=1 TQ；采样点=(1+seg1)/(1+seg1+seg2)；来源 RM §51.4.2 (p.866)",
        transform=ax.transAxes, ha="right", fontsize=7.5, color="#555555")
ax.grid(axis="x", linestyle=":", alpha=0.5)
fig.tight_layout()
out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "09",
                                   "fig-09-1-bit-timing.png"))
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, dpi=300, facecolor="white")
print(out)
