#!/usr/bin/env python3
"""第 18 章 ADC16 插图: 采样通路框图 + 噪声/ENOB 实测 (数据来自 logs/20261030-111916-adc_basic_poll.log)"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

def box(ax, x, y, w, h, text, fc="#e8f0fe"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02", fc=fc, ec="#33517a"))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=8)

def arrow(ax, p1, p2, text=""):
    ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=10, color="#33517a"))
    if text:
        ax.text((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2 + 0.03, text, ha="center", fontsize=7)

fig, ax = plt.subplots(figsize=(9, 4.2))
box(ax, 0.02, 0.55, 0.16, 0.22, "analog input\nPF26 (ADC0.IN01)")
box(ax, 0.24, 0.55, 0.14, 0.22, "sample &\nhold (20..511 clk)")
box(ax, 0.44, 0.55, 0.14, 0.22, "16-bit SAR\nconv (21 clk)")
box(ax, 0.64, 0.70, 0.16, 0.16, "BUS_RESULT[ch]\n+ VALID")
box(ax, 0.64, 0.40, 0.16, 0.16, "SEQ/PMT FIFO\n-> DMA")
box(ax, 0.84, 0.70, 0.14, 0.16, "CPU poll\n(oneshot)")
box(ax, 0.84, 0.40, 0.14, 0.16, "DMA ->\nAXI SRAM")
arrow(ax, (0.18, 0.66), (0.24, 0.66))
arrow(ax, (0.38, 0.66), (0.44, 0.66), "50 MHz conv clk")
arrow(ax, (0.58, 0.68), (0.64, 0.76))
arrow(ax, (0.58, 0.62), (0.64, 0.48))
arrow(ax, (0.80, 0.78), (0.84, 0.78))
arrow(ax, (0.80, 0.48), (0.84, 0.48))
box(ax, 0.24, 0.10, 0.30, 0.14, "TRGM: PWM0_TRGO / GPTMR / SW trigger", fc="#fff4e5")
arrow(ax, (0.39, 0.24), (0.51, 0.55), "trigger")
box(ax, 0.60, 0.10, 0.28, 0.14, "ADC clock: ahb0 200 MHz / div(4..16)", fc="#fff4e5")
arrow(ax, (0.60, 0.17), (0.51, 0.55))
ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
ax.set_title("HPM5E31 ADC16 data path (oneshot / sequence-preemption + DMA)", fontsize=10)
plt.tight_layout(); plt.savefig("docs/figures/18/fig-18-1-adc16-datapath.png", dpi=160); plt.close()

# 实测数据: sample_cycle -> sd/ENOB, 5 组 conv clk 分频 (来自串口日志 CSV)
data = {
    50: [(5,104.96,7.64),(20,53.01,8.63),(100,40.62,8.91),(511,33.48,9.34)],
    40: [(5,78.04,7.95),(20,49.56,8.70),(100,37.35,8.99),(511,32.15,9.40)],
    25: [(5,58.52,8.52),(20,46.74,8.76),(100,38.01,8.97),(511,30.64,9.46)],
    20: [(5,60.48,8.48),(20,45.55,8.79),(100,41.49,8.88),(511,32.65,9.38)],
    12.5:[(5,73.62,8.17),(20,48.18,8.73),(100,47.61,8.75),(511,40.51,8.91)],
}
fig, axes = plt.subplots(1, 2, figsize=(9, 3.6))
for conv, rows in data.items():
    xs = [r[0] for r in rows]
    axes[0].plot(xs, [r[1] for r in rows], marker="o", label=f"conv={conv:g} MHz")
    axes[1].plot(xs, [r[2] for r in rows], marker="s", label=f"conv={conv:g} MHz")
for ax in axes:
    ax.set_xscale("log", base=2); ax.set_xlabel("sample & hold cycles"); ax.grid(alpha=.3)
    ax.legend(fontsize=7)
axes[0].set_ylabel("noise sigma (LSB, floating input)")
axes[1].set_ylabel("estimated ENOB (bit)")
axes[0].set_title("Noise vs sampling time (PF26 floating)", fontsize=9)
axes[1].set_title("ENOB vs sampling time", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/18/fig-18-2-adc-noise-enob.png", dpi=160); plt.close()
print("ch18 figs ok")
