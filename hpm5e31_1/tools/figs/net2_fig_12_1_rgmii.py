#!/usr/bin/env python3
"""tools/figs/net2_fig_12_1_rgmii_timing.py —— 图 12-1：RGMII 的 DoD/DoS 延迟模式

数据来源：YT8011A_1.pdf V1.4 §2.1.2（p.25）、§5.2.4 RGMII Timing Delay Modes（p.87-89）
          Table 21/22（DoD 源端/目的端 skew ≤0.5/0.65 ns）、Table 23（DoS 源端 skew ≥1.2 ns）

说明：
  * DoD(Delay on Destination)：源端时钟与数据**边沿对齐**，由接收端把时钟延迟 ~2ns 后采样。
    本板默认即由 PHY 内部给 RXC 加 2ns（strap RXC/RXDLY_EN=1 + EXT 0x9001[8]=1）。
  * DoS(Delay on Source)：发送端输出**已延迟**的时钟（典型 1.8~2.0ns），接收端直接采样。
输出：docs/figures/12/fig-12-1-rgmii-timing.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
import numpy as np


def wave(ax, y, t0, t1, period=1.0, shift=0.0, label="", color=None, lw=1.8):
    """画一路梯形时钟/数据波形（占空比 50%）。"""
    t = np.linspace(t0, t1, 2000)
    ph = ((t - shift) % period) / period
    v = np.where(ph < 0.5, 1.0, 0.0)
    ax.plot(t, v + y, color=color or COLORS["blue"], lw=lw, label=label)


def data_wave(ax, y, t0, t1, bits, period=1.0):
    """在时钟周期中点画数据电平（每个周期 1 bit，示意用）。"""
    xs, ys = [], []
    for i, b in enumerate(bits):
        xs += [t0 + i * period, t0 + (i + 1) * period]
        ys += [b + y, b + y]
    ax.plot(xs, ys, color=COLORS["orange"], lw=1.8)


def main():
    setup(font_size=9.5)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9.2, 6.2))

    # ---------- (a) DoD：源端边沿对齐 ----------
    T = 1.0
    wave(ax1, 2.0, 0, 8, T, label="TXC (125 MHz)")
    data_wave(ax1, 0.4, 0, 8, [0, 1, 1, 0, 0, 1, 0, 1], T)
    for k in range(0, 9):
        ax1.axvline(k, color=COLORS["gray"], lw=0.6, ls=":")
    ax1.annotate("", xy=(0.02, 1.5), xytext=(0.0, 1.5),
                 arrowprops=dict(arrowstyle="<->", color=COLORS["red"], lw=1.2))
    ax1.text(0.12, 1.62, "源端时钟/数据边沿对齐（skew ≤ 0.5 ns）",
             color=COLORS["red"], fontsize=9)
    ax1.text(0.12, 0.98, "TXD[3:0]/TX_CTL", color=COLORS["orange"], fontsize=9)
    ax1.set_title("(a) DoD (Delay on Destination)：接收端给 RXC 加 2 ns 再采样",
                  loc="left", fontweight="bold", color=COLORS["dark"])
    ax1.set_xlim(0, 8)
    ax1.set_ylim(-0.3, 4.2)
    ax1.set_yticks([])
    ax1.set_xlabel("时间 (ns，1 个时钟周期 = 8 ns @125 MHz / 1000BASE-T1)")
    for s in ("top", "right", "left"):
        ax1.spines[s].set_visible(False)

    # ---------- (b) DoS：源端已延迟 ----------
    wave(ax2, 2.0, 0, 8, T, shift=-0.22, label="TXC（源端已延迟 ≈1.8 ns）",
         color=COLORS["green"])
    data_wave(ax2, 0.4, 0, 8, [1, 0, 0, 1, 1, 0, 1, 0], T)
    for k in range(0, 9):
        ax2.axvline(k, color=COLORS["gray"], lw=0.6, ls=":")
    ax2.annotate("", xy=(0.0, 1.45), xytext=(0.22, 1.45),
                 arrowprops=dict(arrowstyle="<->", color=COLORS["green"], lw=1.2))
    ax2.text(0.26, 1.58, "时钟相对数据延迟 1.2~2.0 ns（skew ≥ 1.2 ns）",
             color=COLORS["green"], fontsize=9)
    ax2.text(0.12, 0.98, "TXD[3:0]/TX_CTL", color=COLORS["orange"], fontsize=9)
    ax2.set_title("(b) DoS (Delay on Source)：发送端自带延迟，接收端直接采样",
                  loc="left", fontweight="bold", color=COLORS["dark"])
    ax2.set_xlim(0, 8)
    ax2.set_ylim(-0.3, 4.2)
    ax2.set_yticks([])
    ax2.set_xlabel("时间 (ns)")
    for s in ("top", "right", "left"):
        ax2.spines[s].set_visible(False)

    fig.tight_layout(h_pad=1.6)
    save(fig, 12, "fig-12-1-rgmii-timing")


if __name__ == "__main__":
    main()
