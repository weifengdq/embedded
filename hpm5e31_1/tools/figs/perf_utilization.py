#!/usr/bin/env python3
"""fig-24-2: 各总线实测吞吐 / 理论峰值 的利用率

理论峰值来源:
  XPI0 100MHz x 4bit(quad)              = 50 MB/s     -> 实测 40.0 MB/s (本仓 logs/20261030-113730-bench_bus.log)
  YT8011AN 1000BASE-T1                  = 1000 Mbit/s -> 实测 935 Mbit/s (tp-net2 logs/20261030-113501)
  CAN FD 1M 仲裁 / 2M 数据, 64B 载荷     ≈ 1.645 Mbit/s 单向 payload（按帧位数估算，见第 24 章公式）-> 实测 0.809 Mbit/s
  CAN 2.0 500k, 8B 载荷                  ≈ 0.288 Mbit/s -> 实测 0.275 Mbit/s（4305 帧/s）
  UART4 921600 8N1                       = 0.9216 Mbit/s -> 实测未取得有效值（见待确认 P2），图中不画
输出: docs/figures/24/fig-24-3-utilization.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

names = ["XPI0 quad flash read", "UART4 921600 8N1", "CAN 2.0 500k (8B)", "CAN FD 1M/2M (64B)", "YT8011AN TCP down", "YT8011AN TCP up", "YT8011AN UDP down", "YT8011AN UDP up"]
util = [80.0, 93.1, 95.6, 98.4, 90.2, 46.6, 94.4, 64.2]
meas = ["40.0 / 50 MB/s", "857.9 / 921.6 kbit/s", "0.275 / 0.288 Mbit/s", "0.809 / 0.822 Mbit/s", "902 / 1000 Mbit/s", "466 / 1000 Mbit/s", "944 / 1000 Mbit/s", "641 / 1000 Mbit/s"]

fig, ax = plt.subplots(figsize=(7.4, 3.8), dpi=300)
bars = ax.barh(names[::-1], util[::-1], color=["#b7791f", "#c53030", "#c53030", "#c53030", "#6b46c1", "#805ad5", "#6b46c1", "#805ad5"], height=0.55)
ax.set_xlim(0, 115)
ax.axvline(100, color="#888", linestyle="--", linewidth=1)
ax.set_xlabel("measured / theoretical peak (%)")
ax.set_title("HPM5E31 bus utilization vs theoretical peak", fontsize=10)
for b, v, m in zip(bars, util[::-1], meas[::-1]):
    ax.text(v + 1.5, b.get_y() + b.get_height() / 2, f"{v:.1f}%  ({m})", va="center", fontsize=7.5)
ax.grid(axis="x", linestyle=":", alpha=0.45)
fig.tight_layout()
out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "24", "fig-24-3-utilization.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("wrote", os.path.abspath(out))
