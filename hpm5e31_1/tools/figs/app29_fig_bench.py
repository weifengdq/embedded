#!/usr/bin/env python3
"""tools/figs/app29_fig_bench.py —— 图 28-1：三种应用层测试台（bench A/B/C）

内容：MCU 双节点 / 上位机+CAN 网关（cannelloni）/ 以太网应用直连。
输出：docs/figures/28/fig-28-1-bench-arch.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch


def box(ax, x, y, w, h, text, fc="#FFFFFF", ec=None, fs=8.5, tc=None):
    ec = ec or COLORS["gray"]
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                                linewidth=1.0, edgecolor=ec, facecolor=fc, zorder=2))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs,
            color=tc or COLORS["dark"], zorder=3, linespacing=1.25)


def arrow(ax, p0, p1, text=None, color=None, style="<->", dy=0.02, fs=7.5):
    color = color or COLORS["blue"]
    ax.add_patch(FancyArrowPatch(p0, p1, arrowstyle=style, mutation_scale=9,
                                 linewidth=1.2, color=color, zorder=1,
                                 shrinkA=0, shrinkB=0))
    if text:
        ax.text((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2 + dy, text, ha="center",
                va="bottom", fontsize=fs, color=color)


def one_bench(ax, title, rows):
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    ax.set_title(title, loc="left", fontsize=9.5, fontweight="bold", color=COLORS["dark"])
    y = 0.78
    for label, color, fs in rows:
        if label is None:
            y -= 0.09
            continue
        box(ax, 0.06, y, 0.88, 0.14, label, fc=color, fs=fs)
        y -= 0.20


def main():
    setup(font_size=9)
    fig, axes = plt.subplots(1, 3, figsize=(11.0, 3.9))

    ax = axes[0]
    one_bench(ax, "(A) MCU 双节点自测（最快，无上位机）", [
        ("开发板 hpm5e31_1", COLORS["light"], 8.5),
        ("CAN0 = DUT            CAN1 = 测试节点", "#E8F0FE", 8.0),
        ("板内 CAN 总线（120 Ω 两端，物理相连）", "#FFF4E5", 8.0),
        ("结论直接打串口 / hwrun 日志", COLORS["light"], 8.0),
    ])
    arrow(ax, (0.5, 0.60), (0.5, 0.52), "收发 + 回显")
    arrow(ax, (0.5, 0.32), (0.5, 0.24), None)

    ax = axes[1]
    one_bench(ax, "(B) 上位机 + CAN 网关（CAN 类协议标准）", [
        ("主机：python-can / udsoncan / canopen", "#E8F0FE", 7.8),
        ("vcan0（tc tbf 限速 500 kbit）", "#FFF4E5", 7.8),
        ("cannelloni v2：CAN over UDP 4000", COLORS["light"], 7.8),
        ("ENET0 + lwIP → CAN1 网关（app_can_gw）", "#E8F0FE", 7.6),
        ("CAN0 = DUT（板内总线）", COLORS["light"], 7.8),
    ], )
    arrow(ax, (0.5, 0.70), (0.5, 0.62), "SocketCAN")
    arrow(ax, (0.5, 0.52), (0.5, 0.44), "UDP/以太网")
    arrow(ax, (0.5, 0.34), (0.5, 0.26), "CAN 帧")

    ax = axes[2]
    one_bench(ax, "(C) 以太网应用直连（SOME/IP、XRCE、PTP）", [
        ("主机应用（vsomeip / Agent / ptp4l）", "#E8F0FE", 7.8),
        ("enp6s0 192.168.2.1/24", "#FFF4E5", 7.8),
        ("千兆车载以太网盒（1000BASE-T1）", COLORS["light"], 7.8),
        ("板 192.168.2.100/24 · ENET0 + lwIP", "#E8F0FE", 7.6),
        ("应用协议栈（UDP/TCP 直连）", COLORS["light"], 7.8),
    ])
    arrow(ax, (0.5, 0.70), (0.5, 0.62), "UDP/TCP")
    arrow(ax, (0.5, 0.52), (0.5, 0.44), "以太网")
    arrow(ax, (0.5, 0.34), (0.5, 0.26), "socket")

    fig.tight_layout()
    save(fig, 28, "fig-28-1-bench-arch")


if __name__ == "__main__":
    main()
