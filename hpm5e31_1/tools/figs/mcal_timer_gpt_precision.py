#!/usr/bin/env python3
"""第 45 章 图 45-3：Gpt 1 ms tick 精度与中断抖动（10 s 窗口实测）。

数据来源（真实上板日志）：
  logs/20261102-134707-gpt_periodic.log
    10 s 窗口：ticks=9999 / expect=10000 -> -100 ppm（1 个计数 = 量化极限）
    ISR 周期 mean=1000.000 us，抖动 pp=0.510 us，stddev=0.095 us（46 CPU cycles）
    ISR 入口延迟 0.68~0.75 us；GPTMR0 实测时钟 100000409 Hz（+4.1 ppm，mcycle 标定）
    API：Gpt_GetTimeElapsed 296 ns / Gpt_StartTimer 2973 ns
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS

import matplotlib.pyplot as plt

setup()

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.2, 3.5), gridspec_kw={"width_ratios": [1.15, 1.0]})

# ---- (a) 误差预算 ----
names = ["计数量化\n(1/10000)", "GPTMR 时钟\n(+4.1 ppm)", "实测总误差\n(9999/10000)"]
vals = [-100.0, 4.1, -100.0]
colors = [COLORS["gray"], COLORS["green"], COLORS["blue"]]
bars = ax1.bar(range(len(vals)), vals, color=colors, width=0.55)
for i, v in enumerate(vals):
    ax1.text(i, v + (6 if v >= 0 else -14), "%+.1f ppm" % v, ha="center", fontsize=7.6, color=COLORS["dark"])
ax1.axhline(0, color=COLORS["dark"], linewidth=0.8)
ax1.set_xticks(range(len(vals)))
ax1.set_xticklabels(names, fontsize=7.0)
ax1.set_ylabel("误差 (ppm)")
ax1.set_ylim(-135, 40)
ax1.grid(axis="y", alpha=0.25)
ax1.set_title("(a) 10 s 计数误差预算（-100 ppm 为量化极限）", fontsize=8.4)

# ---- (b) 抖动与延迟 ----
labels = ["ISR 周期抖动\n(pp)", "ISR 周期抖动\n(stddev)", "ISR 入口延迟\n(均值)", "API 调用\n(GetTimeElapsed)"]
vals_us = [0.510, 0.095, 0.753, 0.296]
colors2 = [COLORS["orange"], COLORS["orange"], COLORS["purple"], COLORS["teal"]]
ax2.barh(range(len(vals_us)), vals_us, color=colors2, height=0.55)
for i, v in enumerate(vals_us):
    ax2.text(v + 0.012, i, "%.3f us" % v, va="center", fontsize=7.4, color=COLORS["dark"])
ax2.set_yticks(range(len(vals_us)))
ax2.set_yticklabels(labels, fontsize=6.8)
ax2.set_xlabel("时间 (us)")
ax2.set_xlim(0, 0.95)
ax2.grid(axis="x", alpha=0.25)
ax2.set_title("(b) 1 ms tick 的软件可见时间开销（480 MHz）", fontsize=8.4)

fig.tight_layout()
save(fig, "45-mcal-timer", "fig-45-3-gpt-precision-budget")
