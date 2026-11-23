#!/usr/bin/env python3
"""tools/figs/net13_tx_bottleneck.py —— 图 13-6 TCP 上行发送路径的周期预算与优化效果

数据来源：第 13 章表 13-15/13-16 与 2026-11-02 复测（logs/ 下 enet_lwip_iperf 日志）。
左图把 1460 B 段的 CPU 周期拆成三段（驱动/D-cache 回写/协议栈）；
右图是 2026-10-30 八组优化实验里三个关键节点的上行吞吐，以及本轮双向打流结果。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt

setup()

parts = [
    ("DMA 描述符填充 + 启动", 756, COLORS["blue"]),
    ("TX D-cache 回写 (46 行)", 5000, COLORS["orange"]),
    ("tcp_write/tcp_output/pbuf", 6200, COLORS["red"]),
]
labels = [p[0] for p in parts]
cycles = [p[1] for p in parts]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.8, 3.9), gridspec_kw={"width_ratios": [1.2, 1]})
y = range(len(parts))
ax1.barh(list(y), cycles, color=[p[2] for p in parts], height=0.55)
ax1.set_yticks(list(y))
ax1.set_yticklabels(labels, fontsize=9)
ax1.invert_yaxis()
ax1.set_xlabel("每 1460 B 段 CPU 周期（480 MHz）")
total = sum(cycles)
for i, c in enumerate(cycles):
    ax1.text(c + 150, i, f"{c} cyc ({c / total * 100:.0f}%)", va="center", fontsize=8.5, color=COLORS["dark"])
ax1.set_xlim(0, total * 1.3)
ax1.grid(axis="x", linestyle=":", alpha=0.5)
ax1.set_title(f"合计 ≈{total} 周期/段 → 理论上限 ≈{480e6 / total * 1460 * 8 / 1e6:.0f} Mbit/s", fontsize=9.5)

stages = ["基线\nR4 首测", "+跳过 TX 回写\nR4 最终", "本轮复测\n2026-11-02"]
tcp = [415, 466, 462]
ax2.bar(range(len(stages)), tcp, color=[COLORS["gray"], COLORS["blue"], COLORS["teal"]], width=0.55)
for i, v in enumerate(tcp):
    ax2.text(i, v + 10, str(v), ha="center", fontsize=9)
ax2.set_xticks(range(len(stages)))
ax2.set_xticklabels(stages, fontsize=8)
ax2.set_ylabel("TCP 上行 Mbit/s（板端统计）")
ax2.set_ylim(0, 560)
ax2.grid(axis="y", linestyle=":", alpha=0.5)
ax2.set_title("上行吞吐（本轮未再优化）", fontsize=9.5)

fig.tight_layout()
save(fig, 13, "fig-13-6-tcp-bottleneck")
