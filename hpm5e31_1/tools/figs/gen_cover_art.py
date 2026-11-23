#!/usr/bin/env python3
"""tools/figs/gen_cover_art.py —— 生成封面用的 hpm5e31_1 板卡示意图

输出：docs/figures/_common/cover-board.png（300 dpi，白底）
说明：示意图不是照片，用于封面装饰；引脚/接口位置与实际板卡布局无关。
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS, new_fig  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, FancyArrowPatch


def box(ax, x, y, w, h, label, sub="", color=COLORS["blue"], fs=9):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                                linewidth=1.0, edgecolor=color, facecolor="white", zorder=3))
    ax.text(x + w / 2, y + h / 2 + (0.012 if sub else 0), label, ha="center", va="center",
            fontsize=fs, color=COLORS["dark"], zorder=4)
    if sub:
        ax.text(x + w / 2, y + h / 2 - 0.030, sub, ha="center", va="center",
                fontsize=fs - 3, color=COLORS["gray"], zorder=4)


def main():
    setup()
    fig, ax = new_fig(9.0, 4.6)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    # PCB 底板
    ax.add_patch(FancyBboxPatch((0.02, 0.05), 0.96, 0.90, boxstyle="round,pad=0.01,rounding_size=0.02",
                                linewidth=1.4, edgecolor="#57606A", facecolor="#F6F8FA", zorder=1))
    ax.text(0.05, 0.92, "hpm5e31_1  ·  HPM5E31IPB1 最小系统板", fontsize=11,
            color=COLORS["dark"], fontweight="bold", zorder=5)

    # 中心 MCU
    box(ax, 0.36, 0.38, 0.28, 0.28, "HPM5E31IPB1", "RV32-IMAFDCBP @480MHz", color=COLORS["blue"], fs=10)

    # 外设/接口
    box(ax, 0.05, 0.66, 0.24, 0.15, "4x CAN FD", "TCAN1044AV", color=COLORS["orange"])
    box(ax, 0.05, 0.44, 0.24, 0.15, "10BASE-T1S", "LAN8651 (SPI2)", color=COLORS["green"])
    box(ax, 0.05, 0.22, 0.24, 0.15, "GbE PHY", "YT8011AN (RGMII)", color=COLORS["teal"])
    box(ax, 0.71, 0.66, 0.24, 0.15, "USB0", "Pin67/68", color=COLORS["purple"])
    box(ax, 0.71, 0.44, 0.24, 0.15, "J-Link JTAG", "调试", color=COLORS["gray"])
    box(ax, 0.71, 0.22, 0.24, 0.15, "UART0 控制台", "921600 8N1", color=COLORS["red"])
    box(ax, 0.36, 0.14, 0.28, 0.11, "片内 Flash 1MB", "合封 NOR，XPI0 映射 0x80000000", color=COLORS["gray"], fs=9)
    box(ax, 0.36, 0.75, 0.28, 0.10, "24MHz 晶振 / 电源", "", color=COLORS["gray"], fs=9)

    # 连线
    for (x1, y1, x2, y2) in [
        (0.29, 0.735, 0.36, 0.60), (0.29, 0.515, 0.36, 0.55), (0.29, 0.295, 0.36, 0.45),
        (0.71, 0.735, 0.64, 0.62), (0.71, 0.515, 0.64, 0.55), (0.71, 0.295, 0.64, 0.45),
        (0.50, 0.38, 0.50, 0.25), (0.50, 0.66, 0.50, 0.75),
    ]:
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-", linewidth=1.0,
                                     color="#8C959F", zorder=2))
    save(fig, "_common", "cover-board")


if __name__ == "__main__":
    main()
