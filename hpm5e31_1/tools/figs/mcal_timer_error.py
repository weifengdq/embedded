#!/usr/bin/env python3
"""第 45 章 图 45-2：Pwm 占空比/频率实测误差（Icu 在杜邦线对端硬件捕获）。

数据来源（真实上板日志）：
  logs/20261102-141059-pwm_basic.log
    duty 扫描 @10 kHz：25% -> 24.170%，50% -> 49.170%，75% -> 74.163%，100% -> 99.880%
    （0% 恒低电平，无沿，测宽模式不可测）
  频率扫描：1 kHz -> 1000 Hz，10 kHz -> 9996..10000 Hz，50 kHz -> 49925..50000 Hz
误差棒取同一次运行的多次心跳复打结果（period 量化 ±1 GPTMR tick）。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS

import matplotlib.pyplot as plt

setup()

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8.2, 3.6))

# ---- (a) 占空比 ----
set_duty = [25.0, 50.0, 75.0, 100.0]
meas_duty = [24.170, 49.170, 74.163, 99.880]
ax1.plot([0, 105], [0, 105], color=COLORS["gray"], linewidth=0.9, linestyle="--", label="理想 1:1")
ax1.plot(set_duty, meas_duty, "o-", color=COLORS["blue"], linewidth=1.4, markersize=4.5, label="实测 (Icu 捕获)")
for x, y in zip(set_duty, meas_duty):
    ax1.annotate("%.2f%%" % y, xy=(x, y), xytext=(x - 3.5, y - 4.5), fontsize=6.8, color=COLORS["blue"])
ax1.annotate("固定高电平缺 ~0.83 us\n(捕获/输出链路延迟，见章内易错点 45-3)",
             xy=(50, 49.17), xytext=(12, 78), fontsize=6.8, color=COLORS["orange"],
             arrowprops=dict(arrowstyle="->", color=COLORS["orange"], linewidth=0.8))
ax1.set_xlabel("设定占空比 (%)")
ax1.set_ylabel("Icu 实测占空比 (%)")
ax1.set_xlim(0, 105)
ax1.set_ylim(0, 105)
ax1.grid(alpha=0.25)
ax1.legend(loc="lower right", fontsize=7.0, frameon=False)
ax1.set_title("(a) 占空比：10 kHz，0/25/50/75/100%", fontsize=8.4)

# ---- (b) 频率误差 ----
freq = [1000, 10000, 50000]
err_lo = [-10.0, -400.0, -1500.0]    # 由实测 period 最大值换算（1k:100001 tick, 10k:10004, 50k:2003）
err_hi = [0.0, 0.0, 0.0]              # 由实测 period 最小值换算
mid = [(a + b) / 2.0 for a, b in zip(err_lo, err_hi)]
yerr = [[m - a for m, a in zip(mid, err_lo)], [b - m for m, b in zip(mid, err_hi)]]
ax2.errorbar(freq, mid, yerr=yerr, fmt="s", color=COLORS["green"], markersize=5,
             capsize=4, linewidth=1.2, label="实测误差范围 (period ±1 tick)")
ax2.axhline(0, color=COLORS["gray"], linewidth=0.8)
ax2.set_xscale("log")
ax2.set_xlabel("目标频率 (Hz)")
ax2.set_ylabel("频率误差 (ppm)")
ax2.grid(alpha=0.25, which="both")
ax2.legend(loc="lower left", fontsize=7.0, frameon=False)
ax2.set_title("(b) 频率：1/10/50 kHz（量化极限 ±1/T）", fontsize=8.4)

fig.tight_layout()
save(fig, "45-mcal-timer", "fig-45-2-pwm-icu-error-curve")