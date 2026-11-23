#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/figs/timer_ch17_figs.py —— 第 17 章（SDM / PLB / TRGM）插图

生成：
  docs/figures/17/fig-17-1-sdm-block.png   SDM 结构（graphviz）
  docs/figures/17/fig-17-2-sigma-delta.png ΣΔ 位流与 Sinc 滤波原理（matplotlib）
  docs/figures/17/fig-17-3-plb-block.png   PLB TYPE A/B 结构（graphviz）
  docs/figures/17/fig-17-4-trgm-route.png  TRGM 路由模型（graphviz）
  docs/figures/17/fig-17-5-trgm-probe.png  PLB->TRGM->GPTMR 捕获通路分段探针（graphviz）

依据：HPM5E00 UM Rev0.4 §43 SDM (p.723)、§42 PLB (p.707)、§38 TRGM (p.606)。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, render_dot, COLORS
import matplotlib.pyplot as plt
import numpy as np

DOT_SDM = r"""
digraph sdm {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.3, ranksep=0.5, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  sensor [label="外部 ΣΔ 调制器\n(如 AMC1306 / AD7401)\n输出 1 bit 位流", fillcolor="#FFF8E1"];
  mclk   [label="SDM0_CLK_x 输出\n(给调制器的时钟)", shape=box3d, fillcolor="#D2E3FC"];
  mdat   [label="SDM0_DAT_x 输入\n(位流)", shape=box3d, fillcolor="#D2E3FC"];

  subgraph cluster_f {
    label="通道滤波模块 ×4（每通道独立）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    cap  [label="输入捕获\n模式 0~5\n(上升沿/双沿/Manchester...)", fillcolor="#E6F4EA"];
    pcm  [label="第一滤波器 PCM_CIC\nSinc1/2/3/SincFast\nOSR 1~256\n-> 16/32 bit", fillcolor="#E6F4EA"];
    amp  [label="第二滤波器 AMP_CIC\nSinc1/2/3/SincFast\nOSR 1~32", fillcolor="#E6F4EA"];
    cmp  [label="3 个比较器\n高/低/越限", fillcolor="#FCE8E6"];
    fifo [label="FIFO 16×32 bit\n可配门限", fillcolor="#E6F4EA"];
    ts   [label="TIMESTAMP\n(参考时间戳)", fillcolor="#E6F4EA"];
  }
  trgm [label="TRGM PWM_EVT ×16\nSDSYNC 同步事件", fillcolor="#FFF8E1"];
  cpu  [label="CPU / DMA / IRQ", shape=box3d, fillcolor="#D2E3FC"];

  mclk -> sensor [label="调制时钟", style="dashed"];
  sensor -> mdat [label="位流", style="dashed"];
  mdat -> cap;
  trgm -> cap [label="同步采样"];
  cap -> pcm -> fifo -> cpu;
  cap -> amp -> cmp;
  cmp -> trgm [label="越限触发", style="dashed"];
  cap -> ts;
  fifo -> ts;
}
"""

DOT_PLB = r"""
digraph plb {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.28, ranksep=0.5, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  trgm_in [label="TRGM\nPLB_IN_00..31", fillcolor="#FFF8E1"];
  subgraph cluster_a {
    label="TYPE A ×16：LUT4 + 边沿检测/计数器";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    lut4 [label="4 输入查找表\nLOOKUP_TABLE[16]", fillcolor="#E6F4EA"];
    ff   [label="触发器 / 边沿检测\nSW_INJECT 可软件置位", fillcolor="#E6F4EA"];
    cnta [label="计数器模式", fillcolor="#E6F4EA"];
  }
  subgraph cluster_b {
    label="TYPE B ×8：LUT4 + 比较器";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    lutb [label="LUT[16] + CMP\nMODE 选择输出源", fillcolor="#E6F4EA"];
  }
  trgm_out [label="TRGM\nPLB_OUT00..", fillcolor="#FFF8E1"];
  tim [label="定时器/PWM 输入\n(可作 LUT 输入)", fillcolor="#FFF8E1"];

  trgm_in -> lut4;
  tim -> lut4;
  lut4 -> ff -> cnta;
  cnta -> trgm_out;
  trgm_in -> lutb -> trgm_out;
}
"""

DOT_TRGM = r"""
digraph trgm {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.25, ranksep=0.6, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  subgraph cluster_in {
    label="输入源（约 150 项）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    pwm  [label="PWM0/1 TRGO 0~7", fillcolor="#E8F0FE"];
    gtmr [label="GPTMR0~3 OUT2/3", fillcolor="#E8F0FE"];
    qei  [label="QEI0/1 触发/位置有效", fillcolor="#E8F0FE"];
    sdm  [label="SDM0 比较器/越限", fillcolor="#E8F0FE"];
    plb  [label="PLB_OUT00..", fillcolor="#E8F0FE"];
    pin  [label="TRGM_P00..P31\n(引脚)", fillcolor="#FFF8E1"];
    acmp [label="ACMP / ADC / DAC ...", fillcolor="#E8F0FE"];
  }
  mux [label="TRGM_OUT[n]\n(触发器: 同相/取反/边沿脉冲)", fillcolor="#E6F4EA", shape=box];
  subgraph cluster_out {
    label="输出目的";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    o_pwm  [label="PWMx_TRIG_IN 0~7", fillcolor="#E6F4EA"];
    o_cap  [label="GPTMRx_CAPT_2/3", fillcolor="#E6F4EA"];
    o_adc  [label="ADC 序列/抢占触发", fillcolor="#E6F4EA"];
    o_sdm  [label="SDM0 PWM_EVT (同步)", fillcolor="#E6F4EA"];
    o_qeo  [label="QEO/QEI 触发", fillcolor="#E6F4EA"];
    o_plb  [label="PLB_IN_00..31", fillcolor="#E6F4EA"];
    o_pin  [label="TRGM_Pnn 引脚", fillcolor="#FFF8E1"];
  }
  pwm -> mux; gtmr -> mux; qei -> mux; sdm -> mux; plb -> mux; pin -> mux; acmp -> mux;
  mux -> o_pwm; mux -> o_cap; mux -> o_adc; mux -> o_sdm; mux -> o_qeo; mux -> o_plb; mux -> o_pin;
}
"""
DOT_PROBE = r"""
digraph probe {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.34, ranksep=0.55, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.5, color="#57606A", arrowsize=0.7];

  subgraph cluster_s {
    label="被测链路（每段都能单独观测）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D0D7DE"; style="rounded";
    gen  [label="① GPTMR0 CH2\n比较输出 1 kHz\nCMPEN=1", fillcolor="#D2E3FC"];
    plb  [label="③ PLB TYPE A ch0\nLUT0=0xAAAA\n(直通 in0)", fillcolor="#E6F4EA"];
    pin  [label="⑤ TRGM_P15 引脚\n+ IOC LOOP_BACK", fillcolor="#FFF8E1"];
    cap  [label="④ GPTMR0 CH3\n捕获 CAPT_3\nCAPPOS/CAPF", fillcolor="#D2E3FC"];
  }
  t_in  [label="② TRGM 输入\nTRGM_IN[0x10]=OUT2\nTRGM_IN[0x44]=PLB_OUT00", fillcolor="#FCE8E6"];
  t_out [label="② TRGM 输出\nTRGM_OUT[0x38/0x39]\nTRGM_OUT[0x0F]=P15", fillcolor="#FCE8E6"];
  reg  [label="只读回读寄存器\nTRGM_IN[5] @+0x600\nTRGM_OUT[5] @+0x680\n软件轮询电平翻转", fillcolor="#F6F8FA", shape=note];

  gen -> t_in [label="OUT2"];
  t_in -> plb [label="PLB_IN_00"];
  plb -> t_out [label="PLB_OUT00"];
  t_out -> cap [label="CAPT_3"];
  plb -> pin [label="TRGM_P15 输出"];
  pin -> cap [label="TRGM0_P15 输入(回环)"];
  t_in -> reg [style="dashed", label="读电平"];
  t_out -> reg [style="dashed", label="读电平"];
}
"""



def fig_sdm_block():
    render_dot(DOT_SDM, 17, "fig-17-1-sdm-block")


def fig_sigma_delta():
    fig, axes = plt.subplots(2, 1, figsize=(6.8, 4.6),
                             gridspec_kw={"height_ratios": [1.0, 1.0]})
    rng = np.random.default_rng(7)
    n = 200
    # 三种占空比（=模拟输入幅度）对应的位流密度
    dens = [0.25, 0.5, 0.75]
    ax = axes[0]
    for k, d in enumerate(dens):
        bits = (rng.random(n) < d).astype(int)
        # 画成阶梯
        x = np.arange(n)
        ax.step(x, bits * 0.7 + k * 1.2, where="post",
                color=[COLORS["blue"], COLORS["orange"], COLORS["green"]][k], lw=0.8,
                label="density=%.0f%%" % (d * 100))
    ax.set_yticks([0, 1.2, 2.4]); ax.set_yticklabels(["25%", "50%", "75%"])
    ax.set_xlabel("位流样点")
    ax.set_title("(a) ΣΔ 调制器输出：1 bit 流的平均密度 = 模拟输入幅度", loc="left",
                 fontsize=9.5, fontweight="bold")
    ax.legend(loc="upper right", ncol=3, frameon=False, fontsize=8)
    ax.set_ylim(-0.3, 3.1)

    # Sinc3 滤波后恢复波形
    ax = axes[1]
    t = np.linspace(0, 1.0, 400)
    analog = 0.5 + 0.4 * np.sin(2 * np.pi * t)
    osr = 32
    bit_t = np.linspace(0, 1.0, 400 * osr)
    bit_analog = 0.5 + 0.4 * np.sin(2 * np.pi * bit_t)
    bits = (rng.random(len(bit_t)) < bit_analog).astype(float)
    # 简单的累加-抽取(Sinc1)近似, 仅用于示意
    dec = bits.reshape(-1, osr).mean(axis=1)
    dec_t = np.linspace(0, 1.0, len(dec))
    ax.plot(t, analog, color=COLORS["gray"], lw=1.2, ls="--", label="调制器输入 (模拟)")
    ax.step(dec_t, dec, where="mid", color=COLORS["purple"], lw=1.2,
            label="Sinc 滤波 + 抽取后 (%d 倍)" % osr)
    ax.set_xlabel("时间")
    ax.set_ylabel("归一化幅度")
    ax.set_title("(b) Sinc 滤波 + 抽取：高采样率 1 bit -> 低采样率多位", loc="left",
                 fontsize=9.5, fontweight="bold")
    ax.legend(frameon=False, fontsize=8, loc="lower right")
    ax.grid(True, ls="--", lw=0.5, color="#D0D7DE"); ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

    fig.tight_layout()
    save(fig, 17, "fig-17-2-sigma-delta")


def fig_plb():
    render_dot(DOT_PLB, 17, "fig-17-3-plb-block")


def fig_trgm():
    render_dot(DOT_TRGM, 17, "fig-17-4-trgm-route")


def fig_probe():
    render_dot(DOT_PROBE, 17, "fig-17-5-trgm-probe")


if __name__ == "__main__":
    setup()
    fig_sdm_block()
    fig_sigma_delta()
    fig_plb()
    fig_trgm()
    fig_probe()
