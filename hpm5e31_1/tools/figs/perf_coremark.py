#!/usr/bin/env python3
"""fig-23-1: CoreMark 变体对比柱状图（数据来源见脚本内注释）

数据（hpm5e31_1 / HPM5E31 @480MHz, GCC 11.1.0, XIP flash 取指, n>=2 次运行）:
  -O3 数据在 DLM(栈)        : 1898.11 CoreMark = 3.954 CoreMark/MHz
                              logs/20261030-110654-bench_coremark.log
  -O2 数据在 DLM(栈)        : 1808.02 = 3.767   logs/20261030-110853-bench_coremark.log
  -O3 数据在 AXI SRAM(.bss) : 1886.84 = 3.931   logs/20261030-111015-bench_coremark.log
输出: docs/figures/23/fig-23-1-coremark.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

labels = ["-O3 / DLM(stack)\n(default)", "-O2 / DLM(stack)", "-O3 / AXI SRAM(.bss)"]
coremark = [1898.11, 1808.02, 1886.84]
per_mhz = [3.954, 3.767, 3.931]

fig, ax1 = plt.subplots(figsize=(7.2, 4.2), dpi=300)
bars = ax1.bar(labels, coremark, color=["#2b6cb0", "#63b3ed", "#2f855a"], width=0.55)
ax1.set_ylabel("CoreMark score (iterations/s)")
ax1.set_ylim(0, 2200)
ax1.set_title("HPM5E31 CoreMark 1.0 @480MHz (XIP flash code, GCC 11.1.0)", fontsize=10)
for b, v in zip(bars, coremark):
    ax1.text(b.get_x() + b.get_width() / 2, v + 25, f"{v:.0f}", ha="center", fontsize=9)
ax2 = ax1.twinx()
ax2.plot(labels, per_mhz, "o--", color="#c53030", linewidth=1.4, markersize=6)
ax2.set_ylabel("CoreMark/MHz", color="#c53030")
ax2.set_ylim(3.4, 4.2)
for x, v in zip(range(len(per_mhz)), per_mhz):
    ax2.annotate(f"{v:.3f}", (x, v), textcoords="offset points", xytext=(0, 6),
                 ha="center", fontsize=8, color="#c53030")
ax1.grid(axis="y", linestyle=":", alpha=0.5)
fig.tight_layout()
out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "23", "fig-23-1-coremark.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("wrote", os.path.abspath(out))
