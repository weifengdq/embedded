#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_08_quad_topology.py - 4 路 MCAN 与两组双节点总线 + 32 KB 消息 RAM 分配

数据来源：boards/hpm5e31_1/ext/board_can.h（引脚/端子）、RM §51.1（4 个 MCAN）、
hpm_mcan_soc.h（MCAN_MSG_BUF_SIZE_IN_WORDS=640 -> 2560 B/实例，AHB SRAM 共 32 KB）、
TI TCAN1044A-Q1 数据手册（5 Mbit/s 上限）。
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle

setup()
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.4, 4.3), gridspec_kw={"width_ratios": [1.5, 1.0]})

# ---- 左：拓扑 ----
ax1.set_xlim(0, 10)
ax1.set_ylim(0, 10)
ax1.axis("off")
ax1.add_patch(FancyBboxPatch((0.2, 0.5), 3.0, 9.0, boxstyle="round,pad=0.12",
                             facecolor="#F6F8FA", edgecolor="#333333"))
ax1.text(1.7, 9.15, "HPM5E31（4 x MCAN）", ha="center", fontsize=10)
nodes = [("MCAN0", "PD00/PD01"), ("MCAN1", "PD05/PD04"),
         ("MCAN2", "PC08/PA09"), ("MCAN3", "PC15/PC14")]
for i, (n, pin) in enumerate(nodes):
    y = 7.6 - i * 1.9
    ax1.add_patch(Rectangle((0.55, y - 0.42), 1.5, 0.84, facecolor=COLORS["blue"], alpha=0.18,
                            edgecolor=COLORS["blue"]))
    ax1.text(1.3, y + 0.12, n, ha="center", fontsize=8.5)
    ax1.text(1.3, y - 0.24, pin, ha="center", fontsize=6.8, color="#444444")
    ax1.add_patch(Rectangle((2.35, y - 0.3), 0.7, 0.6, facecolor="#FFFFFF", edgecolor="#333333"))
    ax1.text(2.7, y, "TCAN\n1044AV", ha="center", va="center", fontsize=5.6)
    ax1.plot([2.15, 2.35], [y, y], color="#333333", lw=0.8)
for g, (y0, y1, cols) in enumerate([(6.7, 4.1, ("CAN0", "CAN1")), (2.9, 0.7, ("CAN2", "CAN3"))]):
    xb = 6.6
    ax1.plot([3.05, xb], [y0, y0], color=COLORS["dark"], lw=2.2)
    ax1.plot([3.05, xb], [y1, y1], color=COLORS["dark"], lw=2.2)
    ax1.plot([xb, xb], [y1, y0], color=COLORS["dark"], lw=2.2)
    ax1.plot([3.05, 3.05], [y1, y0], color=COLORS["dark"], lw=2.2)
    for y in (y0, y1):
        ax1.annotate("", xy=(xb, y), xytext=(6.25, y),
                     arrowprops=dict(arrowstyle="-|>", color=COLORS["red"], lw=1.0))
    ax1.text(3.3, y1 - 0.45, f"{cols[0]} <-> {cols[1]}", fontsize=8.5)
    ax1.text(3.3, y0 - 0.45, "同一段总线，两端各 120 Ω", fontsize=7.2, color="#444444")
ax1.text(6.45, 8.55, "两组独立双节点总线，可同时满负荷", fontsize=8.8, color=COLORS["dark"])
ax1.text(0.25, 0.02, "引脚与端子来自 board_can.h；收发器上限 5 Mbit/s（TCAN1044A-Q1 数据手册）",
         fontsize=6.8, color="#555555")

# ---- 右：消息 RAM ----
used = 4 * 2560
total = 32 * 1024
ax2.barh([1], [used / 1024.0], color=COLORS["green"], edgecolor="#333333")
ax2.barh([1], [(total - used) / 1024.0], left=[used / 1024.0], color="#E5E7EB", edgecolor="#333333")
for i in range(4):
    ax2.plot([i * 2.5, i * 2.5], [0.55, 1.45], color="#333333", lw=1.0)
    ax2.text(i * 2.5 + 1.25, 1.55, f"MCAN{i}\n2.5 KB", ha="center", fontsize=7)
ax2.text(used / 1024.0 / 2, 1.0, f"4 x 2560 B\n= 10 KB（31.25%）", ha="center", va="center", fontsize=8)
ax2.text(used / 1024.0 + (total - used) / 2048.0, 1.0, f"空闲 {(total - used) / 1024.0:.0f} KB",
         ha="center", va="center", fontsize=8)
ax2.set_xlim(0, 32)
ax2.set_ylim(0.4, 2.0)
ax2.set_yticks([])
ax2.set_xlabel("AHB SRAM 消息 RAM（KB，共 32 KB）")
ax2.set_title("4 实例共享 32 KB AHB SRAM（每实例按需 2560 B）", fontsize=10)
ax2.grid(axis="x", linestyle=":", alpha=0.5)
fig.tight_layout()
save(fig, 8, "fig-08-4-quad-topology")
