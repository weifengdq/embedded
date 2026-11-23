#!/usr/bin/env python3
"""tools/figs/app35_fig4_tp_host.py —— 图 35-4：SOME/IP-TP 4096 B 三段在主机侧的到达情况

数据：tools/figs/app35_tpdata.json（由 logs/*-host-tp-r5.log 与 tcpdump 抓包整理，
      R5 2026-11-02 实测：主机侧 5 次请求，统计每段是否在线路上出现）。
输出：docs/figures/35/fig-35-4-tp-host-recv.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


def main():
    d = json.loads((HERE / "app35_tpdata.json").read_text(encoding="utf-8"))
    runs = d["runs"]
    n = len(runs)
    setup(font_size=9.0)
    fig, ax = plt.subplots(figsize=(7.2, 0.75 * n + 1.5))
    for i, r in enumerate(runs):
        y = n - 1 - i
        for j, ok in enumerate(r["recv"]):
            ax.add_patch(Rectangle((j, y - 0.36), 0.92, 0.72,
                                   facecolor=(COLORS["green"] if ok else COLORS["red"]),
                                   edgecolor="white", linewidth=1.2, alpha=0.9))
            ax.text(j + 0.46, y, ("收到" if ok else "缺失"), ha="center", va="center",
                    color="white", fontsize=8.2, fontweight="bold")
        ax.text(-0.05, y, "第 %d 次" % r["run"], ha="right", va="center", fontsize=8.4)
    ax.set_xlim(-0.55, 3.02)
    ax.set_ylim(-0.8, n - 0.2 + 0.3)
    ax.set_xticks([0.46, 1.46, 2.46])
    ax.set_xticklabels(["段 1\noffset 0\n1392 B", "段 2\noffset 1392\n1392 B", "段 3\noffset 2784\n1312 B"],
                       fontsize=8.2)
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    ax.tick_params(length=0)
    ax.set_title("%d 次 4096 B 响应：段 1 从未上线，tcpdump 只见 %d/%d 段"
                 % (n, sum(sum(1 for x in r["recv"] if x) for r in runs), 3 * n), fontsize=9.2)
    fig.tight_layout()
    save(fig, 35, "fig-35-4-tp-host-recv")


if __name__ == "__main__":
    main()
