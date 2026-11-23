#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_09_tsu_path.py - MCAN TSU/PTPC 时间戳路径与读取入口

依据：RM §51.4.9 (p.870) 时间戳、hpm_mcan_drv.h 的 mcan_timestamping 文档、
hpm_mcan_soc.h（MCAN_TSU_EXT_TIMEBASE_SRC_TBSEL_0 / MCAN_TSU_TBSEL_PTPC0）、
hpm_ptpc_drv.h（ptpc_init / ptpc_set_timer_output）。
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

setup()
fig, ax = plt.subplots(figsize=(10.6, 4.8))
ax.set_xlim(0, 11)
ax.set_ylim(0, 6.4)
ax.axis("off")


def box(x, y, w, h, text, color, fs=8.2):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.06",
                                facecolor=color, edgecolor="#333333", alpha=0.9))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs)


def arrow(x1, y1, x2, y2, text="", color="#333333", fs=7.0):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=11,
                                 color=color, lw=1.2))
    if text:
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + 0.12, text, ha="center", fontsize=fs,
                color="#333333")


box(0.25, 4.55, 1.9, 0.9, "AHB 时钟\n(clock_ahb0)", "#E8F0FE")
box(0.25, 2.85, 1.9, 0.9, "PTPC0\n秒/纳秒计数器", "#FFF4E5")
box(2.75, 4.55, 1.5, 0.9, "TSU 预分频\nprescaler+1", "#E8F0FE")
box(2.75, 2.85, 1.5, 0.9, "MCAN_TBSEL\n(TBSEL_0)", "#FFF4E5")
box(4.85, 3.7, 1.75, 1.6, "MCAN TSU\n32 / 64 bit\n捕获计数器", "#E6F4EA", fs=8.0)
box(7.15, 4.55, 1.55, 0.9, "TX Event\nFIFO 元素", "#FCE8E6")
box(7.15, 2.85, 1.55, 0.9, "RX FIFO\n元素", "#FCE8E6")
box(9.2, 4.55, 1.6, 0.9, "mcan_get_ts_\nfrom_tx_event()", "#F1F3F4", fs=7.0)
box(9.2, 2.85, 1.6, 0.9, "mcan_get_ts_\nfrom_rx_msg()", "#F1F3F4", fs=7.0)

arrow(2.15, 5.0, 2.75, 5.0, "内部时基")
arrow(2.15, 3.3, 2.75, 3.3, "外部时基")
arrow(4.25, 5.0, 4.85, 4.9)
arrow(4.25, 3.3, 4.85, 4.1)
arrow(6.6, 5.0, 7.15, 5.0, "SOF 触发", fs=6.5)
arrow(6.6, 3.9, 7.15, 3.3, "SOF 触发", fs=6.5)
arrow(8.7, 5.0, 9.2, 5.0)
arrow(8.7, 3.3, 9.2, 3.3)

ax.text(0.25, 1.95,
        "内部 AHB 时基：tick = (prescaler+1)/AHB_clock，两个实例各自从使能时刻起算，"
        "计数器之间有固定未知偏移；\n"
        "PTPC 时基：4 个 MCAN 的 TSU 捕获同一个 PTPC 计数器，TX 与 RX 时间戳可直接相减。",
        fontsize=7.6, va="top")
ax.text(0.25, 0.95,
        "实测入口（samples/can/mcan_quad_load）：tsu 200 -> 内部 AHB 时基，比较同一节点相邻帧的 "
        "TX/RX 间隔；\nptp 200 -> PTPC0 公共时基，直接统计 RX 时间戳 - TX 时间戳。",
        fontsize=7.6, va="top", color="#333333")
ax.text(0.25, 0.15, "依据 RM §51.4.9 (p.870)；驱动 API 见 hpm_mcan_drv.h 的 timestamping 说明",
        fontsize=6.8, color="#666666")
fig.tight_layout()
save(fig, 9, "fig-09-4-tsu-path")
