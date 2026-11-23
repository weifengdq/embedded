#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/figs/timer_ch16_figs.py —— 第 16 章（QEI/QEO 编码器接口）插图

生成：
  docs/figures/16/fig-16-1-qeo-qei-flow.png  QEO -> QEI 数据流（graphviz）
  docs/figures/16/fig-16-2-quadrature.png    正交编码 4 倍频解码时序（matplotlib）
  docs/figures/16/fig-16-3-pos-speed.png     位置/速度计算与量化误差（matplotlib）

依据：HPM5E00 UM Rev0.4 §40 QEIV2 (p.626)、§41 QEO (p.678)、§38 TRGM (p.606)。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, render_dot, COLORS
import matplotlib.pyplot as plt
import numpy as np

DOT_FLOW = r"""
digraph qeo_qei {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.3, ranksep=0.55, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  subgraph cluster_qei {
    label="QEIv2（HPM_QEI0，§40）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    pad  [label="A/B/Z 输入\n(IOC 复用 + 可选滤波)", fillcolor="#FFF8E1"];
    dec  [label="正交解码\nABZ / PD / UD / UVW / sincos", fillcolor="#E6F4EA"];
    ph   [label="位置计数 COUNT[PH]\nZ 相重置 COUNT[Z]", fillcolor="#E6F4EA"];
    spd  [label="测速 COUNT[SPD]\n(脉冲数法 / 时长法)", fillcolor="#E6F4EA"];
    wdg  [label="看门狗计数器", fillcolor="#E6F4EA"];
  }

  subgraph cluster_trgm {
    label="TRGM（§38）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    pos_matrix [label="POS_MATRIX_SEL0\n位置矩阵\ntrgm_pos_matrix_in_from_qei0", fillcolor="#E8F0FE"];
  }

  subgraph cluster_qeo {
    label="QEO（HPM_QEO0/1，§41）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    qeo_in [label="位置/角度输入\n(来自 TRGM 位置矩阵)", fillcolor="#FFF8E1"];
    qeo_out [label="A/B/Z 正交输出\nU/V/W 霍尔输出", fillcolor="#E6F4EA"];
    qeo_pwm [label="PWM/DAC 输出\n(角度->占空比)", fillcolor="#E6F4EA"];
  }

  pad -> dec -> ph;
  dec -> spd;
  dec -> wdg;
  ph -> pos_matrix;
  pos_matrix -> qeo_in;
  qeo_in -> qeo_out;
  qeo_in -> qeo_pwm;

  qeo_out -> pins  [label="P0~P7 引脚\n(需 3 根飞线接回 QEI)", style="dashed", color="#DA3633"];
  pins [label="QEI0 A/B/Z 引脚\n(PC07/PC06/PC05)", shape=box3d, fillcolor="#D2E3FC"];
  pins -> pad [style="dashed", color="#DA3633"];
}
"""


def fig_flow():
    render_dot(DOT_FLOW, 16, "fig-16-1-qeo-qei-flow")


def fig_quadrature():
    fig, axes = plt.subplots(2, 1, figsize=(6.8, 4.6), sharex=True,
                             gridspec_kw={"height_ratios": [1.35, 1.0]})
    # A/B 相差 90 度电角度
    step = 0.5
    t = np.linspace(0, 8.0, 4000)
    # 用相位累积构造方波: 每个周期 4 个边沿
    ph = (t / step).astype(int) % 4
    a = np.where((ph == 0) | (ph == 1), 1.0, 0.0)
    b = np.where((ph == 1) | (ph == 2), 1.0, 0.0)

    ax = axes[0]
    ax.plot(t, a + 2.2, color=COLORS["blue"], lw=1.6, label="A 相")
    ax.plot(t, b + 1.0, color=COLORS["orange"], lw=1.6, label="B 相")
    ax.axhline(2.2, color="#D0D7DE", lw=0.5)
    ax.axhline(1.0, color="#D0D7DE", lw=0.5)
    ax.set_yticks([1.0, 2.2]); ax.set_yticklabels(["B", "A"])
    ax.set_ylim(0.4, 3.4)
    ax.set_title("(a) A/B 相正交信号，每个电气周期 4 个边沿（4 倍频）", loc="left",
                 fontsize=9.5, fontweight="bold")
    ax.legend(loc="upper right", ncol=2, frameon=False, fontsize=8)

    ax = axes[1]
    cnt = np.zeros_like(t)
    for i in range(1, len(t)):
        cnt[i] = cnt[i - 1]
    # 每个边沿 +1（正向）
    edges = np.where(np.diff(np.concatenate(([0], a))))[0]
    cn = 0
    xs, ys = [], []
    for k in range(1, len(t)):
        if a[k] != a[k - 1] or b[k] != b[k - 1]:
            cn += 1
            xs.append(t[k]); ys.append(cn)
    ax.step(xs, ys, where="post", color=COLORS["green"], lw=1.6, label="位置计数（4 倍频）")
    ax.plot(xs, ys, "o", color=COLORS["green"], ms=2.5)
    ax.set_xlabel("时间")
    ax.set_ylabel("计数")
    ax.set_title("(b) 每个 A/B 边沿计数 ±1：4 倍频，方向由 A/B 相位关系判别", loc="left",
                 fontsize=9.5, fontweight="bold")
    ax.legend(loc="upper left", frameon=False, fontsize=8)
    ax.grid(True, axis="y", ls="--", lw=0.5, color="#D0D7DE")
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

    fig.tight_layout()
    save(fig, 16, "fig-16-2-quadrature")


def fig_pos_speed():
    fig, axes = plt.subplots(1, 2, figsize=(7.4, 3.2))
    # 理论位置：匀速转动；量化位置：4 倍频计数
    t = np.linspace(0, 1.0, 1000)
    lines_per_rev = 1000          # 编码器线数
    speed_rpm = 600.0             # 600 rpm = 10 rps
    pos_true = speed_rpm / 60.0 * t * 4 * lines_per_rev     # 4 倍频后的计数
    pos_quant = np.floor(pos_true)

    ax = axes[0]
    ax.plot(t, pos_true, color=COLORS["gray"], lw=1.2, ls="--", label="理想位置")
    ax.step(t, pos_quant, where="post", color=COLORS["blue"], lw=1.2, label="4 倍频量化位置")
    ax.set_xlabel("时间 / s")
    ax.set_ylabel("计数值")
    ax.set_title("(a) 位置量化：1000 线 × 4 = 4000 计数/转", loc="left", fontsize=9.5,
                 fontweight="bold")
    ax.legend(frameon=False, fontsize=8)
    ax.grid(True, ls="--", lw=0.5, color="#D0D7DE"); ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

    # 速度量化误差随采样窗口变化
    ax = axes[1]
    fs = np.array([1e3, 5e3, 10e3, 20e3])
    f_enc = speed_rpm / 60.0 * 4 * lines_per_rev      # 边沿频率 Hz
    # 固定采样窗口 T_win 内的边沿数 m, 速度误差 ≈ ±1/m
    T_win = 1e-3                                       # 1 ms 窗口
    m = f_enc * T_win
    err = 100.0 / np.maximum(m, 1.0)
    ax.bar(["%.0f kHz" % (f / 1000) for f in fs],
           [100.0 / max(f * T_win, 1.0) for f in fs],
           color=COLORS["orange"], width=0.55)
    ax.set_xlabel("编码器边沿频率")
    ax.set_ylabel("速度量化误差 / %")
    ax.set_title("(b) 脉冲数测速：1 ms 窗口误差 ≈ 1/m", loc="left", fontsize=9.5,
                 fontweight="bold")
    ax.grid(True, axis="y", ls="--", lw=0.5, color="#D0D7DE"); ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.text(0.02, 0.92, "m = f_enc × 1 ms", transform=ax.transAxes, fontsize=8,
            color=COLORS["gray"])

    fig.tight_layout()
    save(fig, 16, "fig-16-3-pos-speed")


if __name__ == "__main__":
    setup()
    fig_flow()
    fig_quadrature()
    fig_pos_speed()
