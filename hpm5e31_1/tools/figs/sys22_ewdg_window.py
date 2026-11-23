#!/usr/bin/env python3
"""fig-22-3: EWDG 窗口看门狗时序与实测结果（2026-11-02）

数据来源: samples/power/ewdg_window 的 auto_early / auto_hold 两次上电
          logs/20261102-124007-ewdg_window-r5c.log
输出: docs/figures/22/fig-22-3-ewdg-window.png
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

fig, axes = plt.subplots(1, 2, figsize=(10.6, 3.9), dpi=300,
                         gridspec_kw={"width_ratios": [1.55, 1.0]})

ax = axes[0]
ax.add_patch(Rectangle((0.5, 0), 0.5, 1.05, color="#2EA043", alpha=0.15, lw=0))
ax.text(0.75, 1.09, "valid window (feed >= 0.5 T, before timeout)", ha="center", fontsize=8, color="#2EA043")
ax.plot([0, 1.0], [0, 1.0], color="#1F6FEB", lw=2, label="EWDG counter (no feed)")
ax.axhline(1.0, color="#DA3633", ls="--", lw=1)
ax.text(0.02, 1.03, "timeout T = 1000 ms -> reset", color="#DA3633", fontsize=8)
ax.plot([0.05], [0.05], marker="X", color="#DA3633", ms=10, ls="none")
ax.annotate("feed at 50 ms\n(outside window) -> RESET", xy=(0.05, 0.05), xytext=(0.10, 0.55),
            fontsize=8, color="#DA3633",
            arrowprops=dict(arrowstyle="->", color="#DA3633", lw=1))
for f in (0.60, 0.75, 0.90):
    ax.plot([f], [f], marker="^", color="#2EA043", ms=7, ls="none")
ax.annotate("feed every 50 ms after 0.5 T\n-> survived 2500 ms, 41 feeds", xy=(0.72, 0.72),
            xytext=(0.30, 0.30), fontsize=8, color="#2EA043",
            arrowprops=dict(arrowstyle="->", color="#2EA043", lw=1))
ax.set_xlim(0, 1.25)
ax.set_ylim(-0.05, 1.25)
ax.set_xlabel("time / timeout (T = 1000 ms)")
ax.set_ylabel("EWDG counter (normalized)")
ax.set_title("window watchdog: only feed after the lower limit", fontsize=9.5)
ax.grid(True, ls=":", alpha=0.4)
ax.legend(fontsize=7.5, loc="upper left")

ax = axes[1]
names = ["feed @50 ms\n(early)", "hold: feed\n>= 500 ms"]
colors = ["#DA3633", "#2EA043"]
ax.bar([0, 1], [1, 1], color=colors, alpha=0.22, width=0.7)
ax.text(0, 0.5, "WDG0 reset\nwithin ms", ha="center", va="center", fontsize=8.5, color="#DA3633")
ax.text(1, 0.5, "SURVIVED\n2500 ms, 41 feeds", ha="center", va="center", fontsize=8.5, color="#2EA043")
ax.set_xticks([0, 1])
ax.set_xticklabels(names, fontsize=8.5)
ax.set_yticks([])
ax.set_xlim(-0.6, 1.6)
ax.set_ylim(0, 1.0)
ax.set_title("measured trials (logs/20261102-124007)", fontsize=9.5)
for s in ("top", "right", "left"):
    ax.spines[s].set_visible(False)

fig.tight_layout()
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs", "figures", "22", "fig-22-3-ewdg-window.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("wrote", os.path.abspath(out))
