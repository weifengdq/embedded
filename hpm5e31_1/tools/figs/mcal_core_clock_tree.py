#!/usr/bin/env python3
"""tools/figs/mcal_core_clock_tree.py —— 第 44 章 图 44-1/43-3（Mcu 时钟树 + 主频实测）

产物：
  docs/figures/44-mcal-core/fig-44-1-clock-tree.png    HPM5E31 时钟树（XTAL -> PLL0 -> 后分频 -> 节点）
  docs/figures/44-mcal-core/fig-44-3-clock-measure.png 480/240 MHz 实测 vs 配置（含 ppm 误差）

数据来源（均为上板实测）：
  logs/20261102-135140-mcu_clock_init.log
  logs/20261102-134845-mcu_clock_init.log
运行：python3 tools/figs/mcal_core_clock_tree.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch  # noqa: E402


def box(ax, x, y, w, h, text, color, fs=9.0, sub=None):
    p = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                       linewidth=1.1, edgecolor=color, facecolor="white", zorder=2)
    ax.add_patch(p)
    ax.text(x + w / 2, y + h / 2 + (0.012 if sub else 0.0), text, ha="center", va="center",
            fontsize=fs, color=COLORS["dark"], zorder=3)
    if sub:
        ax.text(x + w / 2, y + h / 2 - 0.028, sub, ha="center", va="center", fontsize=fs - 2.0,
                color=COLORS["gray"], zorder=3)


def arrow(ax, p1, p2, color=None):
    ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=9,
                                 linewidth=1.0, color=color or COLORS["blue"], zorder=1))


def fig_clock_tree():
    fig, ax = plt.subplots(figsize=(9.2, 5.4))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    # 第一列：晶振
    box(ax, 0.02, 0.42, 0.15, 0.16, "XTAL 24 MHz", COLORS["teal"],
        sub="PLLCTLV2.XTAL (REFSEL=0)")
    # 第二列：PLL0
    box(ax, 0.23, 0.40, 0.20, 0.20, "PLL0 960 MHz", COLORS["orange"],
        sub="MFI=40 MFN=0 MFD=240M")
    # 第三列：后分频
    box(ax, 0.49, 0.60, 0.20, 0.16, "POSTDIV0 = /2.0", COLORS["green"], sub="PLL0.DIV[0]=5 -> 480 MHz")
    box(ax, 0.49, 0.36, 0.20, 0.16, "POSTDIV1 = /2.4", COLORS["green"], sub="PLL0.DIV[1]=7 -> 400 MHz")
    # 第四列：SYSCTL 节点
    box(ax, 0.74, 0.62, 0.23, 0.14, "CPU0 节点 /1 -> 480 MHz", COLORS["blue"], sub="SYSCTL.CLOCK[0]")
    box(ax, 0.74, 0.42, 0.23, 0.14, "SYSCTL 其它节点", COLORS["blue"], sub="ahb0 /2 -> 200 MHz")
    box(ax, 0.74, 0.22, 0.23, 0.14, "XPI0 节点", COLORS["blue"], sub="100 MHz（Flash 访问）")
    # 直接来自 OSC 的 mchtmr
    box(ax, 0.28, 0.06, 0.24, 0.13, "MCHTMR0 24 MHz", COLORS["purple"],
        sub="osc24m/1，实测时基")

    arrow(ax, (0.17, 0.50), (0.23, 0.50))
    arrow(ax, (0.43, 0.53), (0.49, 0.68))
    arrow(ax, (0.43, 0.47), (0.49, 0.44))
    arrow(ax, (0.69, 0.68), (0.74, 0.69))
    arrow(ax, (0.69, 0.44), (0.74, 0.49), COLORS["green"])
    arrow(ax, (0.69, 0.42), (0.74, 0.29), COLORS["green"])
    arrow(ax, (0.12, 0.42), (0.12, 0.12))
    arrow(ax, (0.12, 0.12), (0.28, 0.12), COLORS["purple"])

    ax.text(0.02, 0.95, "HPM5E31 时钟树（Mcu_InitClock 写入路径）",
            fontsize=12, fontweight="bold", color=COLORS["dark"])
    ax.text(0.02, 0.885, "实测：cpu0=480,000,000 Hz（mchtmr+rdcycle 均值 479,997,500 Hz，-5 ppm）；"
                        "ahb0=200 MHz；xpi0=100 MHz；mchtmr0=24 MHz",
            fontsize=8.6, color=COLORS["gray"])
    save(fig, "44-mcal-core", "fig-44-1-clock-tree")


def fig_measure():
    fig, ax = plt.subplots(figsize=(7.2, 3.8))
    labels = ["480 MHz 配置\n(MCU_CLOCK_SETTING_0)", "240 MHz 配置\n(MCU_CLOCK_SETTING_1)"]
    cfg = [480.0, 240.0]
    meas = [479.9975, 239.99772]
    x = [0, 1]
    width = 0.32
    ax.bar([i - width / 2 for i in x], cfg, width, label="配置值（Mcu_GetClockNodeHz）",
           color=COLORS["gray"], edgecolor="none")
    ax.bar([i + width / 2 for i in x], meas, width, label="实测值（mchtmr 10 ms 窗口 + rdcycle）",
           color=COLORS["blue"], edgecolor="none")
    for i in x:
        ax.text(i + width / 2, meas[i] + 6, "%.3f MHz\n%.0f ppm" % (meas[i], (meas[i] - cfg[i]) / cfg[i] * 1e6),
                ha="center", va="bottom", fontsize=8.4, color=COLORS["dark"])
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=9)
    ax.set_ylim(0, 560)
    ax.set_ylabel("CPU0 主频 (MHz)")
    ax.legend(loc="upper right", frameon=False)
    ax.grid(True, axis="y", linestyle="--", color="#D0D7DE", linewidth=0.6)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.set_title("主频实测与误差（5 次采样取均值，logs/20261102-135140）",
                 loc="left", fontsize=10.5, fontweight="bold", color=COLORS["dark"])
    save(fig, "44-mcal-core", "fig-44-3-clock-measure")


if __name__ == "__main__":
    setup()
    fig_clock_tree()
    fig_measure()
