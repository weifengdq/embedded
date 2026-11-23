#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_09_data.py - 第 9 章数据图：单向吞吐 + 位时序矩阵

数据来源（全部实测，日志在仓库 logs/ 下）：
  吞吐: logs/20261030-114225-mcan_throughput.log
  位时序: logs/20261030-115604-mcan_fd_timing.log
"""
import os
import matplotlib
matplotlib.use("Agg")
from matplotlib import font_manager
import matplotlib.pyplot as plt
import numpy as np

font_manager.fontManager.addfont("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
plt.rcParams["font.sans-serif"] = ["Noto Sans CJK JP", "Noto Sans CJK SC"]
plt.rcParams["axes.unicode_minus"] = False
OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "09"))
os.makedirs(OUT, exist_ok=True)

# ---------- 图 9-2 吞吐 ----------
cases = [
    ("经典\n500k/8B", 4313, 0.28, "#4a6fa5"),
    ("经典\n1M/8B", 8615, 0.55, "#4a6fa5"),
    ("FD\n500k+2M/64B", 2632, 1.35, "#d07a35"),
    ("FD\n1M+2M/64B", 2843, 1.46, "#d07a35"),
    ("FD\n1M+5M/64B", 6302, 3.23, "#d07a35"),
    ("FD\n1M+8M/64B", 9058, 4.64, "#d07a35"),
]
fig, ax1 = plt.subplots(figsize=(10, 5.2), dpi=300)
x = np.arange(len(cases))
b = ax1.bar(x, [c[1] for c in cases], color=[c[3] for c in cases], width=0.6)
ax1.set_ylabel("单向帧率（frame/s）")
ax1.set_xticks(x)
ax1.set_xticklabels([c[0] for c in cases], fontsize=8.5)
ax1.set_ylim(0, 11000)
for i, c in enumerate(cases):
    ax1.text(i, c[1] + 120, str(c[1]), ha="center", fontsize=8)
ax2 = ax1.twinx()
ax2.plot(x, [c[2] for c in cases], "o-", color="#c0392b", linewidth=1.6, markersize=5, label="payload Mbit/s")
ax2.set_ylabel("有效载荷吞吐（Mbit/s）", color="#c0392b")
ax2.set_ylim(0, 6)
for i, c in enumerate(cases):
    ax2.text(i, c[2] + 0.15, f"{c[2]:.2f}", ha="center", color="#c0392b", fontsize=7.5)
ax1.set_title("CAN0 -> CAN1 单向吞吐实测（每组 2000 帧，0 丢帧；日志 logs/20261030-114225-mcan_throughput.log）", fontsize=10)
ax1.grid(axis="y", linestyle=":", alpha=0.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig-09-2-throughput.png"), dpi=300, facecolor="white")
print("fig-09-2 ok")

# ---------- 图 9-3 位时序矩阵 ----------
# (数据段 Mbit/s, 仲裁500k fps 三档, 仲裁1M fps 三档) 数据取日志中三档的中位/稳定值
data = [
    (2, 2842, 2842, 1.46),
    (4, 4565, 5239, 2.68),
    (5, 5352, 6303, 3.23),
    (8, 7218, 9059, 4.64),
]
fig, ax = plt.subplots(figsize=(9.5, 5.0), dpi=300)
x = np.arange(len(data))
w = 0.35
ax.bar(x - w / 2, [d[1] for d in data], w, color="#4a6fa5", label="仲裁段 500 kbit/s")
ax.bar(x + w / 2, [d[2] for d in data], w, color="#d07a35", label="仲裁段 1 Mbit/s")
for i, d in enumerate(data):
    ax.text(i - w / 2, d[1] + 120, str(d[1]), ha="center", fontsize=8)
    ax.text(i + w / 2, d[2] + 120, str(d[2]), ha="center", fontsize=8)
ax.set_xticks(x)
ax.set_xticklabels([f"{d[0]} Mbit/s\n64B FD" for d in data])
ax.set_ylabel("帧率（frame/s）")
ax.set_ylim(0, 10500)
ax.set_title("CAN FD 数据段速率 vs 帧率（每组 300 帧，24 组矩阵全部 0 丢帧；日志 logs/20261030-115604-mcan_fd_timing.log）", fontsize=10)
ax.legend(fontsize=9)
ax.grid(axis="y", linestyle=":", alpha=0.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig-09-3-timing-matrix.png"), dpi=300, facecolor="white")
print("fig-09-3 ok")
