#!/usr/bin/env python3
"""tools/figs/mcal_core_port_loopback.py —— 第 44 章 图 44-2（Port/Dio 回环与寄存器路径）

产物：docs/figures/44-mcal-core/fig-44-2-port-loopback.png
内容：SPI1 <-> SPI3 杜邦线回环 + IOC/GPIO 寄存器路径 + 实测结果标注。
数据来源：logs/20261102-135603-port_pin_config.log、logs/20261102-135945-dio_led_button.log
运行：python3 tools/figs/mcal_core_port_loopback.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle  # noqa: E402


def box(ax, x, y, w, h, text, color, fs=9.0):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                                linewidth=1.1, edgecolor=color, facecolor="white", zorder=3))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs,
            color=COLORS["dark"], zorder=4)


def main():
    fig, ax = plt.subplots(figsize=(9.0, 5.0))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    # 左：SPI1 侧（输出）
    box(ax, 0.03, 0.58, 0.20, 0.30, "", COLORS["blue"])
    ax.text(0.13, 0.84, "SPI1 侧（输出）", ha="center", fontsize=10, color=COLORS["blue"])
    for i, (pin, name) in enumerate([("PC11", "nCS"), ("PC12", "MISO"), ("PC10", "SCLK")]):
        ax.text(0.13, 0.78 - i * 0.07, "%s  %s" % (pin, name), ha="center", fontsize=9,
                color=COLORS["dark"], family="monospace")

    # 右：SPI3 侧（输入）
    box(ax, 0.57, 0.58, 0.20, 0.30, "", COLORS["green"])
    ax.text(0.67, 0.84, "SPI3 侧（输入）", ha="center", fontsize=10, color=COLORS["green"])
    for i, (pin, name) in enumerate([("PA27", "nCS"), ("PA28", "MISO"), ("PA26", "SCLK")]):
        ax.text(0.67, 0.78 - i * 0.07, "%s  %s" % (pin, name), ha="center", fontsize=9,
                color=COLORS["dark"], family="monospace")

    # 杜邦线
    for i, (lvl, res) in enumerate([("GPIO 回环", "2002 组图案 0 误码"),
                                    ("开漏 + 上拉", "OD 释放=1 / 拉低=0"),
                                    ("GPIO 回环", "同 PC11 对，0 误码")]):
        y = 0.78 - i * 0.07
        ax.add_patch(FancyArrowPatch((0.24, y), (0.56, y), arrowstyle="<|-|>", mutation_scale=8,
                                     linewidth=1.1, color=COLORS["gray"], linestyle="-" if i != 1 else "--",
                                     zorder=2))
        ax.text(0.40, y + 0.018, res, ha="center", fontsize=7.8, color=COLORS["gray"])

    # 下方：寄存器路径
    box(ax, 0.03, 0.20, 0.20, 0.26, "IOC PADx\nFUNC_CTL\n(PAD_CTL)", COLORS["orange"])
    box(ax, 0.28, 0.20, 0.20, 0.26, "GPIO\nOE / DO / DI", COLORS["orange"])
    box(ax, 0.53, 0.20, 0.20, 0.26, "Dio\nRead/Write\nFlip/Group", COLORS["purple"])
    box(ax, 0.78, 0.20, 0.19, 0.26, "应用/shell\nploop 2000\nppull / pod", COLORS["teal"])
    for x in (0.23, 0.48, 0.73):
        ax.add_patch(FancyArrowPatch((x, 0.33), (x + 0.05, 0.33), arrowstyle="-|>", mutation_scale=9,
                                     linewidth=1.1, color=COLORS["blue"], zorder=2))
    ax.text(0.03, 0.51, "Port 配置 -> GPIO 方向/电平 -> Dio 读写（AUTOSAR 分工：Port 管方向，Dio 管电平）",
            fontsize=8.6, color=COLORS["dark"])

    ax.text(0.03, 0.95, "SPI1 <-> SPI3 杜邦线 GPIO 回环（Port/Dio 实测路径）",
            fontsize=12, fontweight="bold", color=COLORS["dark"])
    ax.text(0.03, 0.05, "易错点：IOC PADx.FUNC_CTL.LOOP_BACK=1 才能让 DI 回读真实引脚电平；"
                        "改内部上下拉后需 ~200 us 稳定再采样。",
            fontsize=8.2, color=COLORS["red"])
    save(fig, "44-mcal-core", "fig-44-2-port-loopback")


if __name__ == "__main__":
    setup()
    main()
