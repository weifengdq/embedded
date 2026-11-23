#!/usr/bin/env python3
"""tools/figs/net2_fig_13_3_throughput.py —— 图 13-3：TCP/UDP 实测吞吐（1000BASE-T1）

数据来源：tools/figs/net2_iperf_data.json（由 tools/figs/net2_parse_logs.py 从实测日志生成，
          原始日志见 docs/chapters/13-lwip-iperf.md 第 5 节的"日志"列）
输出：docs/figures/13/fig-13-3-throughput.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS, new_fig, style_axes  # noqa: E402

import matplotlib.pyplot as plt


def main():
    data = json.loads((HERE / "net2_iperf_data.json").read_text(encoding="utf-8"))
    setup(font_size=9.5)
    fig, ax = new_fig(6.8, 3.8)

    pairs = [("TCP 下行\n(host→board)", data["tcp_down_mbps"], COLORS["blue"]),
             ("UDP 下行\n(host→board)", data["udp_down_mbps"], COLORS["orange"])]
    if data.get("tcp_up_mbps"):
        pairs.insert(1, ("TCP 上行\n(board→host)", data["tcp_up_mbps"], COLORS["green"]))
    labels = [p[0] for p in pairs]
    vals = [p[1] for p in pairs]
    colors = [p[2] for p in pairs]
    bars = ax.bar(labels, vals, color=colors, width=0.55, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + max(vals) * 0.02,
                "%.1f" % v, ha="center", va="bottom", fontsize=10, fontweight="bold",
                color=COLORS["dark"])
    ax.axhline(1000, color=COLORS["red"], ls="--", lw=1.0, zorder=2)
    ax.text(2.32, 1000, "线速 1000 Mbit/s", color=COLORS["red"], fontsize=8.5,
            va="bottom", ha="right")
    ax.set_ylim(0, max(vals + [1000]) * 1.15)
    style_axes(ax, ylabel="吞吐 (Mbit/s)")
    fig.tight_layout()
    save(fig, 13, "fig-13-3-throughput")


if __name__ == "__main__":
    main()
