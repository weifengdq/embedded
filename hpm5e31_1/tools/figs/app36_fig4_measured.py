#!/usr/bin/env python3
"""tools/figs/app36_fig4_measured.py —— 图 36-4：实测频率/抖动/命令往返时延

数据来源：tools/figs/app36_data.json（logs/20261030-172701-xrce_pubsub.log 与
host/xrce/test_ros2_bridge.sh 的 ros2 topic hz / ros2_adc_sub.py / ros2_cmd_pub.py 输出）。
输出：docs/figures/36/fig-36-4-measured.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS, new_fig, style_axes  # noqa: E402

import matplotlib.pyplot as plt


def main():
    d = json.loads((HERE / "app36_data.json").read_text(encoding="utf-8"))
    setup(font_size=9.0)
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.4))

    ax = axes[0]
    labels = ["ros2 topic hz\n窗口 1", "窗口 2", "窗口 3", "ros2_adc_sub\n(10 s)"]
    vals = list(d["ros2_hz_windows"]) + [d["ros2_sub_rate_hz"]]
    bars = ax.bar(labels, vals, color=[COLORS["blue"]] * 3 + [COLORS["green"]], width=0.6, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.6, "%.2f" % v, ha="center", fontsize=8.4,
                fontweight="bold", color=COLORS["dark"])
    ax.axhline(100, color=COLORS["red"], ls="--", lw=0.9, zorder=2)
    ax.text(0.02, 100.8, "板端配置 100 Hz", color=COLORS["red"], fontsize=7.8)
    ax.set_ylim(90, 103)
    style_axes(ax, ylabel="频率 (Hz)")

    ax = axes[1]
    rtt = d["rtt_ms"]
    names = ["最小", "中位", "最大", "单向上界\n(RTT/2)"]
    vals2 = [rtt["min"], rtt["median"], rtt["max"], d["one_way_bound_ms"]]
    bars = ax.bar(names, vals2, color=[COLORS["orange"]] * 3 + [COLORS["purple"]], width=0.6, zorder=3)
    for b, v in zip(bars, vals2):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.08, "%.2f" % v, ha="center", fontsize=8.4,
                fontweight="bold", color=COLORS["dark"])
    style_axes(ax, ylabel="时延 (ms)")
    ax.set_ylim(0, max(vals2) * 1.25)
    ax.text(0.5, max(vals2) * 1.12,
            "抖动 σ=%.2f ms，丢帧 %.2f%%（%d/%d）" % (d["arrival_jitter_ms"], d["lost_pct"],
                                                d["lost_frames"], d["ros2_sub_samples"]),
            ha="center", fontsize=7.8, color=COLORS["gray"])
    fig.tight_layout()
    save(fig, 36, "fig-36-4-measured")


if __name__ == "__main__":
    main()
