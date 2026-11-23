#!/usr/bin/env python3
"""tools/figs/app40_perf_latency.py —— 图 41-5：gs_usb 单帧延迟与经典帧吞吐

数据来源：tools/figs/app40_data.json 的 latency / throughput 段
（由 host/gs_usb/bench_latency.py 采集，日志在 logs/<ts>-gs-usb-bench.log）
输出：docs/figures/41/fig-41-5-latency-throughput.png

数据未就绪时不生成图片（避免把"待实测"当成结果提交）。
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt


def main():
    data = json.loads((HERE / "app40_data.json").read_text(encoding="utf-8"))
    lat = data.get("latency") or {}
    thr = data.get("throughput") or {}
    if lat.get("median_us") is None:
        print("数据未就绪：先跑 sudo -n python3 host/gs_usb/bench_latency.py --json，"
              "把结果写进 tools/figs/app40_data.json 的 latency/throughput 段")
        return 1

    setup(font_size=9.0)
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.2))

    ax = axes[0]
    labels = ["min", "中位", "p99", "max"]
    vals = [lat.get("min_us"), lat.get("median_us"), lat.get("p99_us"), lat.get("max_us")]
    vals = [v if v is not None else 0 for v in vals]
    bars = ax.bar(labels, vals, color=[COLORS["blue"], COLORS["green"], COLORS["orange"], COLORS["red"]],
                  width=0.55, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + max(vals) * 0.02, "%.0f" % v,
                ha="center", fontsize=8.6, fontweight="bold", color=COLORS["dark"])
    ax.set_ylabel("单帧延迟 (us)")
    ax.set_title("can0 -> can1 端到端延迟（%s 帧）" % lat.get("count", "?"), loc="left",
                 fontweight="bold", color=COLORS["dark"])
    ax.set_ylim(0, max(vals) * 1.2 if max(vals) else 1)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

    ax = axes[1]
    names = ["发送帧率", "接收帧率"]
    fps = [thr.get("tx_fps") or 0, thr.get("rx_fps") or 0]
    bars = ax.bar(names, fps, color=[COLORS["blue"], COLORS["green"]], width=0.5, zorder=3)
    for b, v in zip(bars, fps):
        ax.text(b.get_x() + b.get_width() / 2, v + max(fps) * 0.02, "%.1f" % v,
                ha="center", fontsize=8.6, fontweight="bold", color=COLORS["dark"])
    ax.set_ylabel("帧率 (帧/s)")
    ax.set_title("经典帧吞吐（cangen，%s s）" % thr.get("duration_s", "?"), loc="left",
                 fontweight="bold", color=COLORS["dark"])
    ax.set_ylim(0, max(fps) * 1.25 if max(fps) else 1)
    ax.text(0.5, (max(fps) * 1.12 if max(fps) else 0.5),
            "丢帧 %s %%   总线负载 %s %%" % (thr.get("loss_pct"), thr.get("bus_load_pct")),
            ha="center", fontsize=8.0, color=COLORS["gray"])
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

    fig.tight_layout()
    save(fig, 41, "fig-41-5-latency-throughput")
    return 0


if __name__ == "__main__":
    sys.exit(main())
