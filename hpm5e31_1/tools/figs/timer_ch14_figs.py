#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/figs/timer_ch14_figs.py —— 第 14 章（PWMv2 与 HRPWM）插图

生成：
  docs/figures/14/fig-14-1-pwmv2-block.png   PWMv2 结构框图（graphviz）
  docs/figures/14/fig-14-2-waveforms.png     边沿对齐/中心对齐/死区/移相时序（matplotlib）
  docs/figures/14/fig-14-3-freq-resolution.png  频率-分辨率关系（matplotlib）

数据来源：HPM5E00 UM Rev0.4 §37（PDF p.607~618）、HPM5E00 DS V0.4 p.12；
实测值见 docs/chapters/14-pwmv2-hrpwm.md 表 14-4。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, render_dot, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt
import numpy as np

DOT_BLOCK = r"""
digraph pwmv2 {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.28, ranksep=0.45, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  trgm_in  [label="TRGM 触发输入 0~7\n(同步故障/强制/启动)", fillcolor="#FFF8E1"];
  shadow   [label="影子寄存器\nSHADOW_VAL[0..27]", fillcolor="#E8F0FE"];
  calc     [label="通用计算单元 0~15\nD·pD + S + T·pT", fillcolor="#E8F0FE"];
  dac      [label="DAC 输入 0~3", fillcolor="#E8F0FE"];

  subgraph cluster_sm {
    label="PWM 生成子模块 0~3（每个含 1 个 32 位计数器：24 位整数 + 8 位小数）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";

    cnt0 [label="计数器 0\ncmp 0,1,2,3"];
    cnt1 [label="计数器 1\ncmp 4,5,6,7"];
    cnt2 [label="计数器 2\ncmp 8,9,10,11"];
    cnt3 [label="计数器 3\ncmp 12,13,14,15"];
    cmp_far [label="比较值 16~23\n(CMP_CFG 自由映射)", fillcolor="#FFF8E1"];
  }

  gen [label="PWM 生成\nX=f(a,b)  Y=f(c,d)", fillcolor="#E6F4EA"];
  dead [label="死区\nPWM_DEAD[n]\n最小 0x200", fillcolor="#E6F4EA"];
  hr  [label="HRPWM\n100 ps 延迟链\n(需 TRGM 校准)", fillcolor="#FCE8E6"];
  force [label="强制输出\nFORCE_MODE/TIME", fillcolor="#E6F4EA"];
  fault [label="故障保护\n同步(触发输入0~3)/异步(16 pad)", fillcolor="#FCE8E6"];
  inv [label="取反", fillcolor="#E6F4EA"];
  out [label="P0~P7\n(IOC 复用到引脚)", shape=box3d, fillcolor="#D2E3FC"];
  trgo [label="TRGM 触发输出 0~7\n(选任一 CMP 事件)", fillcolor="#FFF8E1"];

  shadow -> cnt0; shadow -> cnt1; shadow -> cnt2; shadow -> cnt3;
  calc -> cnt0; calc -> cnt1; calc -> cnt2; calc -> cnt3;
  dac -> calc;
  cnt0 -> gen; cnt1 -> gen; cnt2 -> gen; cnt3 -> gen;
  cmp_far -> gen;
  gen -> dead -> hr -> force -> fault -> inv -> out;
  trgm_in -> fault; trgm_in -> force;
  cnt0 -> trgo; cnt1 -> trgo; cnt2 -> trgo; cnt3 -> trgo;
}
"""


def fig_block():
    render_dot(DOT_BLOCK, 14, "fig-14-1-pwmv2-block")


def _saw(ax, T, ylim=(-0.1, 1.35), color=COLORS["gray"]):
    t = np.linspace(0, 2 * T, 4000)
    cnt = np.mod(t, T) / T
    ax.plot(t, cnt, color=color, lw=1.0, ls="--", label="CNT/T")
    ax.set_ylim(*ylim)
    ax.set_xlim(0, 2 * T)


def fig_waveforms():
    fig, axes = plt.subplots(3, 1, figsize=(6.8, 7.0))

    # (a) 边沿对齐 a=0, b=0.4T，P6/P7 互补 + 死区
    ax = axes[0]
    T = 1.0
    a, b, dead = 0.0, 0.40 * T, 0.03 * T
    t = np.linspace(0, 2 * T, 4000)
    cnt = np.mod(t, T)
    x = np.where((cnt >= a) & (cnt < b), 1.0, 0.0)
    # 互补 + 死区：Y 上升沿推后 dead，下降沿提前 dead
    y = 1.0 - x
    y = np.where(np.abs(cnt - b) < dead, 1.0, y)      # b 处 Y 晚关断(示意死区间隙)
    y = np.where(np.abs(cnt - a) < dead, 0.0, y)
    ax.plot(t, cnt, color=COLORS["gray"], lw=0.9, ls="--", label="CNT")
    ax.plot(t, x + 1.55, color=COLORS["blue"], lw=1.6, label="P6 (X)")
    ax.plot(t, y + 2.75, color=COLORS["orange"], lw=1.6, label="P7 (Y, 互补+死区)")
    ax.axhline(1.55, color="#D0D7DE", lw=0.6)
    ax.axhline(2.75, color="#D0D7DE", lw=0.6)
    for k in range(2):
        ax.annotate("", xy=(k * T + a, 1.35), xytext=(k * T + b, 1.35),
                    arrowprops=dict(arrowstyle="<->", color=COLORS["green"], lw=0.9))
        ax.text(k * T + (a + b) / 2, 1.18, "b-a 高电平", ha="center", fontsize=8,
                color=COLORS["green"])
    ax.set_yticks([]); ax.set_ylabel("电平")
    ax.set_title("(a) 边沿对齐：a=0, b=0.4T，P6/P7 互补并插入死区", loc="left", fontsize=9.5)
    ax.legend(loc="upper right", ncol=3, fontsize=7.5, frameon=False)
    ax.set_xlabel("时间 / T")

    # (b) 中心对齐：a=(T-duty)/2, b=(T+duty)/2
    ax = axes[1]
    duty = 0.4 * T
    a2, b2 = (T - duty) / 2, (T + duty) / 2
    x2 = np.where((cnt >= a2) & (cnt < b2), 1.0, 0.0)
    ax.plot(t, cnt, color=COLORS["gray"], lw=0.9, ls="--", label="CNT")
    ax.plot(t, x2 + 1.55, color=COLORS["teal"], lw=1.6, label="P6 中心对齐")
    ax.axhline(1.55, color="#D0D7DE", lw=0.6)
    ax.set_yticks([]); ax.set_ylabel("电平")
    ax.set_title("(b) 中心对齐：a=(T-duty)/2，b=(T+duty)/2", loc="left", fontsize=9.5)
    ax.legend(loc="upper right", ncol=2, fontsize=7.5, frameon=False)
    ax.set_xlabel("时间 / T")

    # (c) 两路移相 90°
    ax = axes[2]
    ph = 0.25 * T
    x3 = np.where((np.mod(t, T) >= 0) & (np.mod(t, T) < 0.3 * T), 1.0, 0.0)
    x4 = np.where((np.mod(t - ph, T) >= 0) & (np.mod(t - ph, T) < 0.3 * T), 1.0, 0.0)
    ax.plot(t, x3 + 1.55, color=COLORS["purple"], lw=1.6, label="计数器0 输出")
    ax.plot(t, x4 + 2.75, color=COLORS["red"], lw=1.6, label="计数器1 输出（移相 90°）")
    ax.axhline(1.55, color="#D0D7DE", lw=0.6)
    ax.axhline(2.75, color="#D0D7DE", lw=0.6)
    ax.annotate("", xy=(ph, 2.45), xytext=(0.0, 2.45),
                arrowprops=dict(arrowstyle="<->", color=COLORS["orange"], lw=0.9))
    ax.text(ph / 2, 2.25, "移相量", ha="center", fontsize=8, color=COLORS["orange"])
    ax.set_yticks([]); ax.set_ylabel("电平")
    ax.set_title("(c) 多计数器同步 + 移相（中心/边沿对齐均适用）", loc="left", fontsize=9.5)
    ax.legend(loc="upper right", ncol=2, fontsize=7.5, frameon=False)
    ax.set_xlabel("时间 / T")

    fig.tight_layout()
    save(fig, 14, "fig-14-2-waveforms")


def fig_freq_resolution():
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.4))

    f_clk = 200e6
    reload = np.logspace(1, 8, 400)          # 24 位计数 => 1..16.7M
    freq = f_clk / reload
    duty_res = 1.0 / reload

    ax = axes[0]
    ax.loglog(reload, freq, color=COLORS["blue"], lw=1.6)
    ax.axhline(20e3, color=COLORS["red"], lw=0.8, ls="--")
    ax.text(3e1, 2.6e4, "20 kHz（电机 FOC 常用）", color=COLORS["red"], fontsize=8)
    ax.axvline(20000, color=COLORS["green"], lw=0.8, ls=":")
    ax.text(2.2e4, 3.0, "reload=20000\n=> 10 kHz", color=COLORS["green"], fontsize=8)
    ax.set_xlabel("计数器重载值 T（= CNT_RELOAD_WORK）")
    ax.set_ylabel("PWM 频率 / Hz")
    ax.set_title("f_pwm = f_pwm_clk / T，f_pwm_clk = 200 MHz", loc="left", fontsize=9.5, fontweight="bold")
    ax.grid(True, which="both", ls="--", lw=0.4, color="#D0D7DE")
    ax.set_axisbelow(True)

    ax = axes[1]
    ax.loglog(reload, duty_res * 100, color=COLORS["orange"], lw=1.6, label="8 位整数占空比")
    ax.loglog(reload, duty_res * 100 / 256, color=COLORS["purple"], lw=1.6,
              label="+HRPWM（8 位小数，1/256）")
    ax.set_xlabel("计数器重载值 T")
    ax.set_ylabel("占空比最小步进 / %")
    ax.set_title("占空比分辨率：1/T（HRPWM 再细分 256 级）", loc="left", fontsize=9.5, fontweight="bold")
    ax.grid(True, which="both", ls="--", lw=0.4, color="#D0D7DE")
    ax.set_axisbelow(True)
    ax.legend(frameon=False, fontsize=8)

    fig.tight_layout()
    save(fig, 14, "fig-14-3-freq-resolution")


if __name__ == "__main__":
    setup()
    fig_block()
    fig_waveforms()
    fig_freq_resolution()
