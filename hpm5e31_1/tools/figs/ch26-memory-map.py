#!/usr/bin/env python3
"""tools/figs/ch26-memory-map.py —— 图 26-1：HPM5E31 外设基地址分布图

数据来源：HPM SDK v1.13.0 的 soc/HPM5E00/HPM5E31/hpm_soc_ip.h（由 tools/gen_regmap.py 解析）。
输出：docs/figures/26/fig-26-1-memory-map.png
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
from _common import setup, save, COLORS, new_fig, style_axes  # noqa: E402

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import gen_regmap  # noqa: E402


def main():
    setup(font_size=9.0)
    ip_h, irq_h = gen_regmap.sdk_paths(gen_regmap.DEFAULT_SDK)
    bases = gen_regmap.parse_bases(ip_h)
    groups = gen_regmap.group_by_ip(bases)

    periph = {}
    for ip, inst in groups.items():
        addrs = [x["base"] for x in inst]
        if any(0xF0000000 <= a <= 0xF5FFFFFF for a in addrs):
            periph[ip] = sorted(addrs)
    order = sorted(periph, key=lambda k: min(periph[k]))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.0, 8.8),
                                   gridspec_kw={"width_ratios": [2.35, 1.0]})

    for y, ip in enumerate(order):
        addrs = periph[ip]
        lo, hi = addrs[0], addrs[-1]
        color = COLORS["blue"] if len(addrs) == 1 else COLORS["orange"]
        ax1.plot([lo, hi], [y, y], linewidth=5, solid_capstyle="butt", color=color,
                 alpha=0.85, zorder=3)
        if len(addrs) > 1:
            ax1.scatter(addrs, [y] * len(addrs), s=7, color="white", edgecolor=color,
                        linewidth=0.6, zorder=4)
        ax1.text(hi + 0x00060000, y, "%d" % len(addrs), va="center", fontsize=7,
                 color=COLORS["gray"])
    ax1.set_yticks(range(len(order)))
    ax1.set_yticklabels(order, fontsize=7.2)
    ax1.set_ylim(-0.8, len(order) - 0.2)
    ax1.set_xlim(0xEFE00000, 0xF6300000)
    ax1.set_xticks([0xF0000000, 0xF1000000, 0xF2000000, 0xF3000000, 0xF4000000, 0xF5000000, 0xF6000000])
    ax1.xaxis.set_major_formatter(mticker.FuncFormatter(lambda v, p: "0x%08X" % int(v)))
    ax1.tick_params(axis="x", labelsize=7.5, rotation=15)
    style_axes(ax1, xlabel="基地址（例：UART0 = 0xF0040000、CAN0 = 0xF0280000）",
               grid=True, axis="x")
    ax1.set_title("外设总线：%d 类 IP / %d 个实例（行尾数字 = 实例数）"
                  % (len(order), sum(len(v) for v in periph.values())),
                  loc="left", fontsize=10, fontweight="bold")
    ax1.text(0.01, 0.005, "蓝 = 单实例，橙 = 多实例；数据来自 hpm_soc_ip.h（tools/gen_regmap.py）",
             transform=ax1.transAxes, fontsize=7, color=COLORS["gray"], va="bottom")

    regions = [
        ("0x00000000", "片上 ROM / ILM / DLM\n（FGPIO 0x00300000）", COLORS["gray"], 0.34),
        ("0x30000000", "AHB SRAM / DM（0x30000000）", COLORS["green"], 0.46),
        ("0x80000000", "片内 Flash 1MB（XPI0 映射 0x80000000）", COLORS["orange"], 0.58),
        ("0xE4000000", "PLIC / MCHTMR / 系统控制（0xE4000000）", COLORS["purple"], 0.70),
        ("0xF0000000", "外设总线（0xF0000000~0xF5FFFFFF）\n本图左侧明细", COLORS["blue"], 0.86),
    ]
    y = 0.0
    for name, label, color, w in regions:
        ax2.add_patch(plt.Rectangle((0.04, y), w, 0.78, color=color, alpha=0.18, zorder=2))
        ax2.text(0.07, y + 0.50, name, fontsize=8.5, fontweight="bold", color=color,
                 va="center", zorder=3)
        ax2.text(0.07, y + 0.22, label, fontsize=7.2, color=COLORS["dark"],
                 va="center", zorder=3)
        y += 1.0
    ax2.set_xlim(0, 1)
    ax2.set_ylim(-0.2, y)
    ax2.axis("off")
    ax2.set_title("全地址空间分区（宽度非等比，仅示意顺序）", loc="left",
                  fontsize=10, fontweight="bold")

    fig.tight_layout()
    save(fig, 26, "fig-26-1-memory-map")


if __name__ == "__main__":
    main()
