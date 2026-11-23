#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/figs/timer_ch15_figs.py —— 第 15 章（GPTMR 定时、捕获与测量）插图

生成：
  docs/figures/15/fig-15-1-gptmr-block.png   GPTMR 通道结构（graphviz）
  docs/figures/15/fig-15-2-irq-jitter.png    实测中断周期抖动（matplotlib，数据来自 logs/）
  docs/figures/15/fig-15-3-capture-timing.png 输入捕获原理时序（matplotlib）

实测数据出处：logs/20261030-110408-gptmr_timer_irq.log（见第 15 章表 15-5）。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, render_dot, COLORS
import matplotlib.pyplot as plt
import numpy as np

DOT_BLOCK = r"""
digraph gptmr {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.3, ranksep=0.5, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  clk [label="模块时钟\nf_gptmr = 100 MHz", fillcolor="#E8F0FE"];
  subgraph cluster_ch {
    label="4 个通道（每通道 1 路计数器 + 2 个比较器）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    cnt  [label="32 位向上计数器\nCNT", fillcolor="#E6F4EA"];
    rld  [label="重载值\nRLD", fillcolor="#F6F8FA"];
    cmp0 [label="CMP0\n比较器 0"];
    cmp1 [label="CMP1\n比较器 1"];
    cap  [label="捕获单元\nCAPPOS / CAPNEG\nCAPPRD / CAPDTY"];
    syn  [label="SYNCI\n同步输入"];
  }
  mon  [label="输入监测\nMONITOR\n(周期/高电平越界)"];
  irq  [label="中断 / DMA\nRLD / CMP / CAP"];
  pin  [label="比较输出 -> 引脚", shape=box3d, fillcolor="#D2E3FC"];
  trgm [label="TRGM 捕获输入\nGPTMR0_CAPT_2/3", fillcolor="#FFF8E1"];

  clk -> cnt; rld -> cnt;
  cnt -> cmp0; cnt -> cmp1;
  cmp0 -> pin; cmp1 -> pin;
  cmp0 -> irq; cmp1 -> irq;
  cnt -> cap;
  trgm -> cap;
  cap -> irq;
  cap -> mon;
  syn -> cnt;
}
"""


def fig_block():
    render_dot(DOT_BLOCK, 15, "fig-15-1-gptmr-block")


def fig_jitter():
    # 实测值（logs/20261030-110408-gptmr_timer_irq.log，1 kHz，n=682）
    meas_mean_us = 1000.000
    meas_min_us = 999.520
    meas_max_us = 1000.530
    meas_std_us = 0.194
    tick_ns = 10.0                      # f_gptmr = 100 MHz

    fig, axes = plt.subplots(1, 2, figsize=(7.4, 3.4),
                             gridspec_kw={"width_ratios": [1.25, 1.0]})

    # 左：以 tick 为单位的实测区间
    ax = axes[0]
    lo = (meas_min_us - meas_mean_us) * 1000.0 / tick_ns
    hi = (meas_max_us - meas_mean_us) * 1000.0 / tick_ns
    ax.errorbar([0], [0], yerr=[[-lo], [hi]], fmt="o", color=COLORS["blue"],
                capsize=6, lw=1.6, ms=6, label="实测 min~max")
    ax.axhline(0, color=COLORS["green"], lw=1.0, ls="--", label="理想 1000.000 us")
    ax.axhspan(-meas_std_us * 1000 / tick_ns, meas_std_us * 1000 / tick_ns,
               color=COLORS["orange"], alpha=0.15, label="±1σ")
    ax.set_xlim(-0.6, 0.6)
    ax.set_xticks([])
    ax.set_ylabel("相对理想周期的偏差 / tick（10 ns）")
    ax.set_title("(a) 1 kHz 中断周期实测：峰峰值 101 tick", loc="left", fontsize=9.5,
                 fontweight="bold")
    ax.grid(True, axis="y", ls="--", lw=0.5, color="#D0D7DE")
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(frameon=False, fontsize=8, loc="upper right")
    ax.annotate("min 999.520 us\n(-0.48 us)", xy=(0, lo), xytext=(0.12, lo - 8),
                fontsize=8, color=COLORS["gray"],
                arrowprops=dict(arrowstyle="->", color=COLORS["gray"], lw=0.7))
    ax.annotate("max 1000.530 us\n(+0.53 us)", xy=(0, hi), xytext=(0.12, hi + 4),
                fontsize=8, color=COLORS["gray"],
                arrowprops=dict(arrowstyle="->", color=COLORS["gray"], lw=0.7))

    # 右：不同中断频率下抖动随负载变化的示意（标注为待实测项）
    ax = axes[1]
    rates = [100, 1000, 10000]
    ax.bar([str(r) for r in rates], [0.02, 0.194, 0.0],
           color=[COLORS["gray"], COLORS["blue"], "white"],
           edgecolor=[COLORS["gray"], COLORS["blue"], COLORS["red"]],
           width=0.55)
    ax.set_ylabel("周期标准差 σ / us")
    ax.set_xlabel("中断频率 / Hz")
    ax.set_title("(b) 抖动随频率变化（仅 1 kHz 已实测）", loc="left", fontsize=9.5,
                 fontweight="bold")
    ax.grid(True, axis="y", ls="--", lw=0.5, color="#D0D7DE")
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.text(2.0, 0.13, "待实测\n\\`grate 10000\\`", ha="center", fontsize=8,
            color=COLORS["red"])

    fig.tight_layout()
    save(fig, 15, "fig-15-2-irq-jitter")


def fig_capture():
    fig, ax = plt.subplots(figsize=(6.8, 3.4))
    T = 1.0
    t = np.linspace(0, 2.2 * T, 5000)
    duty = 0.35
    sig = np.where(np.mod(t, T) < duty * T, 1.0, 0.0)
    ax.plot(t, sig + 1.6, color=COLORS["blue"], lw=1.6, label="被测信号（PWM 输出/TRGO）")
    ax.axhline(1.6, color="#D0D7DE", lw=0.6)

    # 捕获时刻
    rises = [k * T for k in range(3)]
    falls = [k * T + duty * T for k in range(3)]
    for i, r in enumerate(rises):
        ax.plot([r], [1.6], marker="v", color=COLORS["red"], ms=7)
        ax.text(r, 1.35, "CAP$_{POS}$%d" % i, ha="center", fontsize=7.5, color=COLORS["red"])
    for i, f in enumerate(falls):
        ax.plot([f], [1.6], marker="^", color=COLORS["green"], ms=7)

    # 周期/脉宽标注
    ax.annotate("", xy=(rises[1], 2.55), xytext=(rises[0], 2.55),
                arrowprops=dict(arrowstyle="<->", color=COLORS["purple"], lw=1.0))
    ax.text((rises[0] + rises[1]) / 2, 2.62, "周期 = CAPPRD（相邻上升沿差）",
            ha="center", fontsize=8, color=COLORS["purple"])
    ax.annotate("", xy=(falls[1], 2.15), xytext=(rises[1], 2.15),
                arrowprops=dict(arrowstyle="<->", color=COLORS["orange"], lw=1.0))
    ax.text(rises[1] + duty * T / 2, 2.22, "高电平 = CAPDTY", ha="center", fontsize=8,
            color=COLORS["orange"])

    ax.set_ylim(1.1, 2.9)
    ax.set_xlim(0, 2.1 * T)
    ax.set_yticks([])
    ax.set_xlabel("时间 / T")
    ax.set_title("GPTMR 输入捕获：CAPPOS/CAPNEG 记时间戳，CAPPRD/CAPDTY 直接给周期与高电平",
                 loc="left", fontsize=9, fontweight="bold")
    ax.legend(loc="lower left", frameon=False, fontsize=8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    save(fig, 15, "fig-15-3-capture-timing")


if __name__ == "__main__":
    setup()
    fig_block()
    fig_jitter()
    fig_capture()