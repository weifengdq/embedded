#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""app29_latency_hist.py - CAN<->UDP 往返延迟分布：-t 1000 与 -t 100000 对比

数据（各 100 次往返，python-can 20 ms 间隔，板端 can_gateway + vcan0 + cannelloni）：
  tools/figs/app29_data/bench_t1000.json     BUF_US=1000
  tools/figs/app29_data/bench_t100000.json   BUF_US=100000（cannelloni 默认）
复现：BUF_US=1000|100000 bash host/can_bridge/acceptance.sh
"""
import json
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt

here = os.path.dirname(os.path.abspath(__file__))
runs = [("-t 1000 us", "bench_t1000.json", COLORS["blue"]),
        ("-t 100000 us（默认）", "bench_t100000.json", COLORS["orange"])]
data = []
for label, fn, color in runs:
    with open(os.path.join(here, "app29_data", fn), encoding="utf-8") as f:
        doc = json.load(f)
    s = sorted(doc["latency_samples"])
    lat = doc["latency"]
    data.append((label, s, lat["lat_avg"], lat["lat_p99"], lat["lat_max"], color))
    print("%-22s n=%d avg=%.2f ms p99=%.2f max=%.2f min=%.2f"
          % (label, len(s), lat["lat_avg"], lat["lat_p99"], lat["lat_max"], lat["lat_min"]))

setup()
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.2, 4.0), gridspec_kw={"width_ratios": [1.15, 1.0]})

for label, s, avg, p99, mx, color in data:
    ax1.hist(s, bins=18, color=color, alpha=0.55, edgecolor="#333333", linewidth=0.5,
             label="%s  avg %.2f ms" % (label, avg))
    ax1.axvline(avg, color=color, lw=1.6, ls="--")
ax1.set_xlabel("往返延迟（ms）")
ax1.set_ylabel("次数")
ax1.set_title("100 次往返延迟直方图", fontsize=10)
ax1.legend(fontsize=8, frameon=False)
ax1.grid(axis="y", linestyle=":", alpha=0.5)

for label, s, avg, p99, mx, color in data:
    n = len(s)
    ys = [100.0 * (i + 1) / n for i in range(n)]
    ax2.plot(s, ys, marker=".", ms=3.5, color=color, lw=1.1, label=label)
ax2.axhline(99, color=COLORS["gray"], lw=0.9, ls=":")
ax2.set_xlabel("往返延迟（ms）")
ax2.set_ylabel("累计概率（%）")
ax2.set_ylim(0, 103)
ax2.set_title("延迟 CDF（-t 决定主机攒包下限）", fontsize=10)
ax2.legend(fontsize=8, frameon=False, loc="lower right")
ax2.grid(linestyle=":", alpha=0.5)

fig.tight_layout()
save(fig, 28, "fig-28-5-latency-hist")
