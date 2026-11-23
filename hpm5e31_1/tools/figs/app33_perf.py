#!/usr/bin/env python3
"""tools/figs/app33_perf.py —— 图 33-4/32-5：PDO 延迟与周期分布、SDO 分段吞吐对比

数据来源（板内 Bench A 实测，脚本内直接固化以便可重跑画图）：
  logs/20261030-173319-canopen_pdo.log      —— 500 个 SYNC/TPDO1
  logs/20261030-173345-canopen_sdo_seg.log  —— 1 KB / 4 KB 分段 SDO
输出：
  docs/figures/33/fig-33-4-pdo-latency.png
  docs/figures/33/fig-33-5-sdo-throughput.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS, new_fig, style_axes  # noqa: E402

import matplotlib.pyplot as plt

# ---- 实测数据（logs/20261030-173319-canopen_pdo.log）----
PDO = {
    "sync": 500,
    "tpdo": 500,
    "lost": 0,
    "lat": {"min": 320, "avg": 350, "p99": 389, "max": 398},   # us
    "period": {"min": 10000, "avg": 10013, "max": 10043},      # us
    "jit_std": 8.785,                                          # us
    "jit_maxdev": 43,                                          # us
}

# ---- 实测数据（logs/20261030-173345-canopen_sdo_seg.log）----
SDO = [
    ("0x2001 1 KB", "upload", 1024, 73.0, 14027),
    ("0x2002 4 KB", "upload", 4096, 291.6, 14045),
    ("0x2001 1 KB", "download", 1024, 72.9, 14048),
    ("0x2002 4 KB", "download", 4096, 289.2, 14161),
]
EXPEDITED_MS = 0.49
EXPEDITED_OPS = 2036


def fig_pdo():
    setup(font_size=9.0)
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.2, 3.2))

    names = ["min", "avg", "p99", "max"]
    vals = [PDO["lat"][k] for k in names]
    bars = ax1.bar(names, vals, color=[COLORS["green"], COLORS["blue"], COLORS["orange"], COLORS["red"]],
                   width=0.62)
    for b, v in zip(bars, vals):
        ax1.text(b.get_x() + b.get_width() / 2, v + 8, "%d" % v, ha="center", fontsize=8)
    style_axes(ax1, xlabel="统计量", ylabel="端到端延迟 (µs)")
    ax1.set_ylim(0, max(vals) * 1.25)
    ax1.set_title("TPDO1 端到端延迟（500 帧，0 丢帧）", fontsize=9)

    labels = ["周期 avg", "周期 min", "周期 max", "抖动 σ", "抖动 maxdev"]
    vals2 = [PDO["period"]["avg"] - 10000, PDO["period"]["min"] - 10000, PDO["period"]["max"] - 10000,
             PDO["jit_std"], PDO["jit_maxdev"]]
    colors = [COLORS["blue"], COLORS["blue"], COLORS["blue"], COLORS["purple"], COLORS["purple"]]
    bars2 = ax2.bar(labels, vals2, color=colors, width=0.62)
    for b, v in zip(bars2, vals2):
        ax2.text(b.get_x() + b.get_width() / 2, v + 0.8, "%.1f" % v, ha="center", fontsize=8)
    style_axes(ax2, xlabel="统计量", ylabel="相对 10 ms 的偏差 (µs)")
    ax2.set_ylim(0, max(vals2) * 1.3)
    ax2.set_title("TPDO1 周期与抖动", fontsize=9)
    ax2.tick_params(axis="x", labelsize=7.5)

    fig.tight_layout()
    save(fig, 33, "fig-33-4-pdo-latency")
    print("generated docs/figures/33/fig-33-4-pdo-latency.png")


def fig_sdo():
    setup(font_size=9.0)
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.2, 3.2))

    labels = ["%s\n%s" % (s[0], s[1]) for s in SDO]
    times = [s[3] for s in SDO]
    thr = [s[4] for s in SDO]
    bars = ax1.bar(labels, times, color=[COLORS["blue"], COLORS["teal"], COLORS["orange"], COLORS["red"]],
                   width=0.6)
    for b, v in zip(bars, times):
        ax1.text(b.get_x() + b.get_width() / 2, v + 6, "%.1f" % v, ha="center", fontsize=8)
    style_axes(ax1, xlabel="对象 / 方向", ylabel="传输耗时 (ms)")
    ax1.set_ylim(0, max(times) * 1.2)
    ax1.set_title("分段 SDO 总耗时（5 轮取最佳）", fontsize=9)
    ax1.tick_params(axis="x", labelsize=7)

    bars2 = ax2.bar(labels, thr, color=[COLORS["blue"], COLORS["teal"], COLORS["orange"], COLORS["red"]],
                    width=0.6)
    for b, v in zip(bars2, thr):
        ax2.text(b.get_x() + b.get_width() / 2, v + 200, "%d" % v, ha="center", fontsize=8)
    style_axes(ax2, xlabel="对象 / 方向", ylabel="吞吐 (B/s)")
    ax2.set_ylim(0, max(thr) * 1.25)
    ax2.set_title("分段 SDO 吞吐（500 kbit/s，总线占用≈100%）", fontsize=9)
    ax2.tick_params(axis="x", labelsize=7)

    fig.tight_layout()
    save(fig, 33, "fig-33-5-sdo-throughput")
    print("generated docs/figures/33/fig-33-5-sdo-throughput.png")


def main():
    fig_pdo()
    fig_sdo()


if __name__ == "__main__":
    main()
