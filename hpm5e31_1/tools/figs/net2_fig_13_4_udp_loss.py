#!/usr/bin/env python3
"""tools/figs/net2_fig_13_4_udp_loss.py —— 图 13-4：UDP 发送带宽 vs 实际到达/丢包

数据来源：tools/figs/net2_iperf_data.json 的 udp_series（每条 = 一次 iperf -u -b 测试）
输出：docs/figures/13/fig-13-4-udp-loss.png
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
    series = data["udp_series"]
    offered = [s["offered_mbps"] for s in series]
    received = [s["received_mbps"] for s in series]
    loss = [s["loss_pct"] for s in series]

    setup(font_size=9.5)
    fig, ax1 = new_fig(6.4, 3.8)
    ax1.plot(offered, offered, ls="--", color=COLORS["gray"], lw=1.0, label="发送（需求）带宽")
    ax1.plot(offered, received, "o-", color=COLORS["blue"], lw=1.8, ms=5, label="实际到达带宽")
    style_axes(ax1, xlabel="iperf UDP 发送带宽 (Mbit/s)", ylabel="带宽 (Mbit/s)")
    ax1.legend(loc="upper left", frameon=False)

    ax2 = ax1.twinx()
    ax2.plot(offered, loss, "s--", color=COLORS["red"], lw=1.2, ms=4, label="丢包率")
    ax2.set_ylabel("丢包率 (%)", color=COLORS["red"])
    ax2.tick_params(axis="y", colors=COLORS["red"])
    ax2.set_ylim(0, max(loss + [1.0]) * 1.3)
    for side in ("top",):
        ax2.spines[side].set_visible(False)
    ax2.legend(loc="center right", frameon=False, fontsize=8)

    fig.tight_layout()
    save(fig, 13, "fig-13-4-udp-loss")


if __name__ == "__main__":
    main()
