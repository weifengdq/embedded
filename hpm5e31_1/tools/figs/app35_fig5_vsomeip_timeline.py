#!/usr/bin/env python3
"""tools/figs/app35_fig5_vsomeip_timeline.py —— 图 35-5：vsomeip 收到板端 SD 后的状态时间线

数据：tools/figs/app35_vsomeip_timeline.json（/tmp/vsomeip_dbg.out，2026-11-02 实测）。
输出：docs/figures/35/fig-35-5-vsomeip-timeline.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt


def main():
    d = json.loads((HERE / "app35_vsomeip_timeline.json").read_text(encoding="utf-8"))
    setup(font_size=9.0)
    fig, ax = plt.subplots(figsize=(7.2, 2.9))
    color = {"info": COLORS["blue"], "recv": COLORS["green"], "fail": COLORS["red"]}
    ev = d["events"]
    y = 0.0
    for e in ev:
        t = e["t_ms"] / 1000.0
        ax.plot([t], [y], "o", ms=7, color=color[e["kind"]], zorder=3)
        ax.vlines(t, y - 0.12, y + 0.12, color=color[e["kind"]], lw=1.2)
        ax.annotate(e["label"], (t, y), xytext=(0, 14), textcoords="offset points",
                    ha="left", va="bottom", fontsize=8.2, color=COLORS["dark"])
    ax.set_xlim(-0.6, 30.5)
    ax.set_ylim(-0.6, 0.9)
    ax.set_yticks([])
    ax.set_xlabel("相对时间 (s，t0=%s)" % d["t0"][11:23])
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.axhline(0, color=COLORS["gray"], lw=0.8)
    ax.text(0.99, 0.05, "服务状态：%s" % d["service_state"], transform=ax.transAxes,
            ha="right", fontsize=8.6, color=COLORS["red"], fontweight="bold")
    fig.tight_layout()
    save(fig, 35, "fig-35-5-vsomeip-timeline")


if __name__ == "__main__":
    main()
