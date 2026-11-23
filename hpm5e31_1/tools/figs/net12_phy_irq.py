#!/usr/bin/env python3
"""tools/figs/net12_phy_irq.py —— 图 12-4 YT8011AN nINT 事件驱动链路（PB28）

数据来源：2026-11-02 实测（samples/enet/enet_phy_config，命令 irq/ints/mode）。
本图只用来说明事件顺序，不是示波器波形：GPIO 中断只负责"叫醒"，MDIO（约 26 us/次）
放到主循环里读，避免在 ISR 里做管理接口事务。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

setup()
fig, ax = plt.subplots(figsize=(7.2, 3.6))
ax.set_xlim(0, 10)
ax.set_ylim(0, 5)
ax.axis("off")

def box(x, y, w, h, text, color):
    b = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.06,rounding_size=0.18",
                       linewidth=1.2, edgecolor=color, facecolor=color + "22")
    ax.add_patch(b)
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=9.5, color=COLORS["dark"])

def arrow(x1, y1, x2, y2, text="", color=None):
    a = FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=12,
                        linewidth=1.2, color=color or COLORS["gray"])
    ax.add_patch(a)
    if text:
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + 0.18, text, ha="center", fontsize=8.2, color=COLORS["gray"])

box(0.2, 3.3, 2.0, 1.2, "YT8011AN\nnINT 拉低\n(链路/速率变化)", COLORS["red"])
box(2.7, 3.3, 2.2, 1.2, "PB28 GPIO\n下降沿中断\nISR: 清标志+置位", COLORS["orange"])
box(5.4, 3.3, 2.2, 1.2, "主循环\nphy_event_poll()\n(非中断上下文)", COLORS["blue"])
box(8.0, 3.3, 1.8, 1.2, "读 MII 0x13/0x17\n读清中断状态", COLORS["teal"])
box(5.4, 1.2, 2.2, 1.2, "解码事件\n链路/速率/角色", COLORS["green"])
box(8.0, 1.2, 1.8, 1.2, "netif_set_link_\nup()/down()", COLORS["purple"])
box(0.2, 1.2, 2.6, 1.2, "ISR 内不做 MDIO\n(一次约 26 us，会阻塞主循环)", COLORS["gray"])

arrow(2.2, 3.9, 2.7, 3.9)
arrow(4.9, 3.9, 5.4, 3.9, "flag")
arrow(7.6, 3.9, 8.0, 3.9, "读寄存器")
arrow(8.9, 3.3, 7.6, 2.4)
arrow(7.6, 1.8, 8.0, 1.8)
arrow(6.5, 3.3, 6.5, 2.4, "事件处理")

save(fig, 12, "fig-12-4-phy-irq")
