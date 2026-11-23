#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_08_board.py - hpm5e31_1 4 路 MCAN 接线拓扑与状态灯"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

from matplotlib import font_manager
font_manager.fontManager.addfont("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
plt.rcParams["font.sans-serif"] = ["Noto Sans CJK JP", "Noto Sans CJK SC"]
plt.rcParams["axes.unicode_minus"] = False

fig, ax = plt.subplots(figsize=(12, 6.4), dpi=300)
ax.set_xlim(0, 12)
ax.set_ylim(0, 6.4)
ax.axis("off")

mcu = FancyBboxPatch((0.2, 1.0), 3.3, 4.6, boxstyle="round,pad=0.05", edgecolor="#4a6fa5", facecolor="#eef4fb")
ax.add_patch(mcu)
ax.text(1.85, 5.35, "HPM5E31IPB1\n4x MCAN", ha="center", va="center", fontsize=11)

pins = [
    ("MCAN0", "PD00(TXD) / PD01(RXD)", 4.6),
    ("MCAN1", "PD05(TXD) / PD04(RXD)", 3.6),
    ("MCAN2", "PC08(TXD) / PA09(RXD)", 2.6),
    ("MCAN3", "PC15(TXD) / PC14(RXD)", 1.6),
]
for name, pin, y in pins:
    ax.text(0.35, y + 0.18, name, fontsize=9, color="#1a3d6d")
    ax.text(0.35, y - 0.12, pin, fontsize=7, color="#555555")
    phy = FancyBboxPatch((4.3, y - 0.3), 2.0, 0.62, boxstyle="round,pad=0.04",
                         edgecolor="#a5824a", facecolor="#f6f0e6")
    ax.add_patch(phy)
    ax.text(5.3, y + 0.02, "TCAN1044AV", ha="center", va="center", fontsize=8)
    ax.add_patch(FancyArrowPatch((3.5, y), (4.3, y), arrowstyle="-|>", mutation_scale=10, color="#4a6fa5"))

# 总线：CAN0-CAN1 一段，CAN2-CAN3 一段
def bus(y_top, y_bot, label):
    ax.plot([6.6, 10.4], [y_top, y_top], color="#333333", linewidth=1.6)
    ax.plot([6.6, 10.4], [y_bot, y_bot], color="#333333", linewidth=1.6)
    ax.plot([6.6, 6.6], [y_bot, y_top], color="#333333", linewidth=1.2)
    ax.plot([10.4, 10.4], [y_bot, y_top], color="#333333", linewidth=1.2)
    ax.text(8.5, (y_top + y_bot) / 2, label, ha="center", va="center", fontsize=9)
    # 终端电阻
    for x in (6.75, 10.25):
        ax.add_patch(plt.Rectangle((x - 0.09, (y_top + y_bot) / 2 - 0.22), 0.18, 0.44,
                                   facecolor="#ffffff", edgecolor="#111111", linewidth=0.8))
        ax.text(x, (y_top + y_bot) / 2 - 0.43, "120Ω", ha="center", fontsize=6.5)

bus(4.45, 3.75, "CAN0 / CAN1 同一段总线")
bus(2.45, 1.75, "CAN2 / CAN3 同一段总线")
ax.annotate("", xy=(6.6, 4.45), xytext=(6.3, 4.6), arrowprops=dict(arrowstyle="-|>", color="#4a6fa5"))
ax.annotate("", xy=(6.6, 3.75), xytext=(6.3, 3.6), arrowprops=dict(arrowstyle="-|>", color="#4a6fa5"))
ax.annotate("", xy=(6.6, 2.45), xytext=(6.3, 2.6), arrowprops=dict(arrowstyle="-|>", color="#4a6fa5"))
ax.annotate("", xy=(6.6, 1.75), xytext=(6.3, 1.6), arrowprops=dict(arrowstyle="-|>", color="#4a6fa5"))

ax.text(0.35, 0.45, "状态灯(高电平亮): CAN0 红PF26/白PF18  CAN1 红PF20/白PF16  "
        "CAN2 红PF22/白PF21  CAN3 红PF23/白PF19", fontsize=8, color="#333333")
ax.text(0.35, 0.15, "4 颗收发器 STB 统一由 PE03 控制（0=normal, 1=standby）", fontsize=8, color="#333333")
ax.set_title("hpm5e31_1 四路 CAN FD 接线拓扑（CAN0↔CAN1、CAN2↔CAN3 各构成一段两节点总线）",
             fontsize=12)
fig.tight_layout()
out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "08",
                                   "fig-08-3-board-wiring.png"))
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, dpi=300, facecolor="white")
print(out)
