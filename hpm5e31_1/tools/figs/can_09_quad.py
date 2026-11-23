#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_09_quad.py - 4 路并发满载实测：每方向帧率、CPU 忙闲比、cyc/frame

数据来源：logs/20261102-122843-mcan_quad_load.log（Lead 独占窗口，2026-11-02 12:28，
n=2000/链路，64 B FD，BRS=1，bad=0、TEC/REC/LEC=0、RXFIFO 溢出 0）。
单链路参考值取第 9 章表 9-5（2026-10-30 日志）：
  FD 1M/2M 单向 2843 fps，FD 1M/5M 单向 6302 fps。
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
import numpy as np

# (标签, 1M/2M 每方向 fps, 1M/5M 每方向 fps)
runs = [
    ("单链路单向\n(2 个 MCAN)", 2843, 6302),
    ("一对总线双向\n(links=2)", 1425, 3169),
    ("两组总线双向\n(links=4)", 1425, 3166),
]

setup()
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.0, 4.4), gridspec_kw={"width_ratios": [1.35, 1.0]})

x = np.arange(len(runs))
w = 0.36
fps2 = [r[1] for r in runs]
fps5 = [r[2] for r in runs]
b1 = ax1.bar(x - w / 2, fps2, w, color=COLORS["blue"], label="数据段 2 Mbit/s", edgecolor="#333333")
b2 = ax1.bar(x + w / 2, fps5, w, color=COLORS["orange"], label="数据段 5 Mbit/s", edgecolor="#333333")
for bars in (b1, b2):
    for b in bars:
        ax1.text(b.get_x() + b.get_width() / 2, b.get_height() + 90, "%d" % int(b.get_height()),
                 ha="center", fontsize=7.5)
ax1.set_xticks(x)
ax1.set_xticklabels([r[0] for r in runs], fontsize=8)
ax1.set_ylabel("每方向帧率（frame/s）")
ax1.set_ylim(0, 7600)
ax1.set_title("64 B FD 每方向吞吐：并发时每路被仲裁减半，组数增加不再下降", fontsize=10)
ax1.legend(fontsize=8, frameon=False, loc="upper right")
ax1.grid(axis="y", linestyle=":", alpha=0.5)
ax1.text(0.30, 0.58,
         "两组双向 vs 一对双向：每方向帧率差 <0.1%\n（1425 vs 1425 / 3166 vs 3169）\n"
         "=> 消息 RAM 与 AHB 带宽不是 4 路并发的瓶颈",
         transform=ax1.transAxes, fontsize=7.4, va="top", color="#333333",
         bbox=dict(boxstyle="round,pad=0.35", facecolor="white", edgecolor="#CCCCCC", alpha=0.95))

x2 = np.arange(2)
cpu = [68.5, 81.3]
ax2.bar(x2, cpu, 0.5, color=[COLORS["green"], COLORS["red"]], edgecolor="#333333")
for i, v in enumerate(cpu):
    ax2.text(i, v + 1.5, "%.1f%%" % v, ha="center", fontsize=9)
ax2.set_xticks(x2)
ax2.set_xticklabels(["一对总线\nlinks=2", "两组总线\nlinks=4"], fontsize=8.5)
ax2.set_ylim(0, 105)
ax2.set_ylabel("主循环 CPU 忙闲比（%）")
ax2.set_title("并发开销：第二条总线只多花 12.8 个百分点", fontsize=10)
ax2.grid(axis="y", linestyle=":", alpha=0.5)
ax2.text(0.03, 0.97,
         "cyc/frame 1M/2M：115377 -> 68492\ncyc/frame 1M/5M：52506 -> 31117\n（4 路时每帧摊到的 CPU 周期更少）",
         transform=ax2.transAxes, fontsize=7.4, va="top",
         bbox=dict(boxstyle="round,pad=0.3", facecolor="white", edgecolor="#CCCCCC", alpha=0.95))

fig.tight_layout()
save(fig, 9, "fig-09-5-quad-load")
