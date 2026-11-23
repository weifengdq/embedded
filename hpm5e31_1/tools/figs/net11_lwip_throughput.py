#!/usr/bin/env python3
"""tools/figs/net11_lwip_throughput.py —— 图 11-3 LAN8651 数据通路能力与 lwIP 实测吞吐

数据来源：/tmp/tp-net-fig11.json（由实测日志生成，见第 11 章 11.9/11.12）；
左图是各层能力（线速率 / TC6 / lwIP），右图是首次上板时 TC6 收发统计与丢帧。
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt

d = json.load(open(sys.argv[1] if len(sys.argv) > 1 else "/tmp/tp-net-fig11.json"))
setup()

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.8, 3.9), gridspec_kw={"width_ratios": [1.25, 1]})

labels = d["layers"]
vals = d["mbps"]
colors = [COLORS["gray"], COLORS["blue"], COLORS["teal"], COLORS["green"], COLORS["orange"]][: len(vals)]
bars = ax1.bar(labels, vals, color=colors, width=0.62)
for b, v in zip(bars, vals):
    ax1.text(b.get_x() + b.get_width() / 2, v + 0.15, f"{v:g}", ha="center", fontsize=9)
ax1.set_ylabel("Mbit/s（有效载荷）")
ax1.set_ylim(0, 11.5)
ax1.axhline(10, color=COLORS["gray"], linestyle="--", linewidth=0.9)
ax1.text(0.02, 10.15, "10BASE-T1S 线速率 10 Mbit/s", fontsize=8, color=COLORS["gray"])
ax1.tick_params(axis="x", labelsize=8.5)
ax1.grid(axis="y", linestyle=":", alpha=0.5)
ax1.set_title("各层实测/理论上限", fontsize=9.5)

st = d["stats"]
names = ["TX 帧", "RX 帧", "RX 丢弃"]
vals2 = [st["tx_frames"], st["rx_frames"], st["rx_dropped"]]
b2 = ax2.bar(names, vals2, color=[COLORS["blue"], COLORS["green"], COLORS["red"]], width=0.55)
for b, v in zip(b2, vals2):
    ax2.text(b.get_x() + b.get_width() / 2, v + 0.3, str(v), ha="center", fontsize=9)
ax2.set_ylabel("帧数（首次上板会话）")
ax2.set_title("TC6 收发统计（修复后 RX 丢帧为 0）", fontsize=9.5)
ax2.grid(axis="y", linestyle=":", alpha=0.5)

fig.tight_layout()
save(fig, 11, "fig-11-3-lwip-throughput")
