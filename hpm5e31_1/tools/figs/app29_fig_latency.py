#!/usr/bin/env python3
"""tools/figs/app29_fig_latency.py —— 图 28-3：CAN<->UDP 往返延迟分布（100 次实测）

数据来源：tools/figs/app29_data/bench_t1000.json
          由 host/can_bridge/bench_gateway.py --json-out 在 2026-10-30 17:39 实测生成
          （对应板端日志 logs/20261030-173827-can_gateway.log、主机 /tmp/can_bridge_accept/bench.log）
输出：docs/figures/28/fig-28-3-latency.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS, new_fig, style_axes  # noqa: E402

import matplotlib.pyplot as plt


def main():
    data = json.loads((HERE / "app29_data" / "bench_t1000.json").read_text(encoding="utf-8"))
    samples = data["latency_samples"]
    lat = data["latency"]

    setup(font_size=9.5)
    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.6),
                                  gridspec_kw={"width_ratios": [2.1, 1.0]})

    # ---- 左：直方图 ----
    n, bins, patches = ax.hist(samples, bins=18, color=COLORS["blue"], alpha=0.75,
                               edgecolor="white", linewidth=0.6, zorder=3,
                               label="100 次往返（%s）" % data.get("tag", ""))
    for b, p in zip(bins, patches):
        if b >= lat["lat_p99"]:
            p.set_facecolor(COLORS["red"])
            p.set_alpha(0.85)
    ax.axvline(lat["lat_p50"], color=COLORS["green"], ls="--", lw=1.3,
               label="p50 = %.2f ms" % lat["lat_p50"])
    ax.axvline(lat["lat_p99"], color=COLORS["red"], ls="--", lw=1.3,
               label="p99 = %.2f ms" % lat["lat_p99"])
    ax.axvline(lat["lat_avg"], color=COLORS["orange"], ls=":", lw=1.3,
               label="avg = %.2f ms" % lat["lat_avg"])
    ax.set_xlabel("往返延迟 (ms)：cansend vcan0 -> 板 CAN0 回显 -> candump")
    ax.set_ylabel("样本数")
    ax.set_title("(a) -t 1000 us：往返延迟分布", loc="left", fontweight="bold",
                 color=COLORS["dark"], fontsize=9.5)
    ax.grid(True, axis="y", zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.legend(fontsize=8, loc="upper right", frameon=False)

    # ---- 右：分位数柱状 ----
    labels = ["min", "avg", "p50", "p95", "p99", "max"]
    vals = [lat["lat_min"], lat["lat_avg"], lat["lat_p50"],
            lat["lat_p95"], lat["lat_p99"], lat["lat_max"]]
    colors = [COLORS["gray"], COLORS["orange"], COLORS["green"],
              COLORS["teal"], COLORS["red"], COLORS["purple"]]
    bars = ax2.bar(range(len(vals)), vals, color=colors, width=0.62, zorder=3)
    for i, v in enumerate(vals):
        ax2.text(i, v + 0.06, "%.2f" % v, ha="center", va="bottom", fontsize=8,
                 color=COLORS["dark"])
    ax2.set_xticks(range(len(labels)))
    ax2.set_xticklabels(labels, fontsize=8.5)
    ax2.set_ylim(0, max(vals) * 1.25)
    ax2.set_ylabel("延迟 (ms)")
    ax2.set_title("(b) 分位数（ms）", loc="left", fontweight="bold",
                  color=COLORS["dark"], fontsize=9.5)
    ax2.grid(True, axis="y", zorder=0)
    ax2.set_axisbelow(True)
    for side in ("top", "right"):
        ax2.spines[side].set_visible(False)
    ax2.text(0.02, 0.95, "n=%d, lost=%d\n300 fps 丢包 %.2f%%"
             % (lat["lat_n"], lat["lat_lost"], data["loss"]["pct"]),
             transform=ax2.transAxes, fontsize=7.8, va="top", color=COLORS["gray"])

    fig.tight_layout()
    save(fig, 28, "fig-28-3-latency")


if __name__ == "__main__":
    main()
