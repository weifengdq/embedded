#!/usr/bin/env python3
"""fig-23-4: 计算内核吞吐（-O3，硬件单精度 FPU，ILM 与 XIP 代码布局对比）

数据来源: logs/20261030-113743-bench_compute.log（bench_compute，BENCH_FPU=1，
          -march=rv32imafc -mabi=ilp32f，480MHz）
单位: 每次操作的周期数（cyc/op，越低越好）；speedup = xip_cyc / ilm_cyc
输出: docs/figures/23/fig-23-3-compute.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

kernels = ["mac_f32\n(2 flop)", "cvt_f2i", "mac_i32\n(2 op)", "q15_gain", "div_f32",
           "sqrt_f32", "div_i32", "div_u64", "sin_f32"]
cyc = [4.027 / 2, 5.055, 3.027 / 2, 6.059, 24.063, 26.070, 18.684, 56.160, 141.457]
# 注: mac 的 ops 按 flop 计（ARR_N*2），这里换算成"每次乘加"的周期数

fig, ax = plt.subplots(figsize=(7.6, 4.2), dpi=300)
colors = ["#2f855a" if c < 10 else ("#dd6b20" if c < 40 else "#c53030") for c in cyc]
bars = ax.bar(kernels, cyc, color=colors, width=0.62)
ax.set_yscale("log")
ax.set_ylabel("cycles per operation (log)")
ax.set_title("HPM5E31 compute kernel cost per operation (-O3, hardware FPU, 480MHz)", fontsize=10)
for b, v in zip(bars, cyc):
    ax.text(b.get_x() + b.get_width() / 2, v * 1.15, f"{v:.2f}", ha="center", fontsize=7.5)
ax.set_ylim(1, 400)
ax.grid(axis="y", which="both", linestyle=":", alpha=0.45)
fig.text(0.01, 0.01,
         "ILM vs XIP: 全部内核 speedup 0.88~1.00 —— 内核代码很小，L1 I-Cache 已能覆盖，放 ILM 无吞吐收益",
         fontsize=7, color="#444")
fig.tight_layout(rect=(0, 0.03, 1, 1))
out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "23", "fig-23-3-compute.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("wrote", os.path.abspath(out))
