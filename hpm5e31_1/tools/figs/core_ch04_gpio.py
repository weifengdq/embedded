#!/usr/bin/env python3
"""第 4 章 GPIO/IOC 插图：图 4-1 信号链、图 4-2 实测操作耗时。

数据来源：logs/20261030-110857-gpio_speed.log
运行：cd hpm5e31_1 && python3 tools/figs/core_ch04_gpio.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import render_dot, setup, save, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt


def fig_4_1():
    dot = r"""
digraph ioc_gpiom_gpio {
  graph [rankdir=LR, fontname="Noto Sans CJK SC", fontsize=12, nodesep=0.30, ranksep=0.50, bgcolor="white", pad=0.2];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=11, color="#57606A", fillcolor="#F6F8FA"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  pad  [label="物理 PAD (如 PA02)\nFUNC_CTL: ALT_SELECT / LOOP_BACK / ANALOG\nPAD_CTL: PE/PS/HYS/OD/SR/SPD/DS", fillcolor="#DBEAFE"];
  ioc  [label="IOC 控制器\n0xF00D0000 之外的 IOC 基址", fillcolor="#FDE68A"];
  gm   [label="GPIOM  ASSIGN[port].PIN[pin]\n0 = gpio0 / 2 = core0_fast", fillcolor="#BBF7D0"];
  g0   [label="GPIO0 (AHB)\n0xF00D0000\nDI/DO/OE/IF/IE", fillcolor="#FCA5A5"];
  fg   [label="FGPIO (核私有)\n0x300000\n同样的寄存器布局", fillcolor="#FCA5A5"];
  sw   [label="软件\ngpio_write_pin() / gpiom_set_pin_controller()", fillcolor="#F6F8FA"];

  sw -> ioc [label="配置复用/属性"];
  ioc -> pad [label="电平/方向"];
  sw -> g0 [label="普通路径"];
  sw -> gm [label="切换控制器"];
  gm -> fg [label="core0_fast 时"];
  g0 -> gm [label="可见性"];
  fg -> gm;
  gm -> ioc [label="DO/OE 作用下"];
  ioc -> gm [label="DI 回读", style=dashed];
}
"""
    render_dot(dot, "04-gpio-ioc", "fig-04-1-ioc-gpiom-gpio")


def fig_4_2():
    # 实测数据: logs/20261030-110857-gpio_speed.log (CPU 480 MHz)
    items = [
        ("gpio_toggle_pin()", 65.0),
        ("直接写 DO.TOGGLE", 65.0),
        ("SET+CLEAR (方波)", 130.0),
        ("端口整字写 DO[F]", 65.0),
        ("gpio_read_pin()", 115.0),
        ("端口整字读 DI[F]", 75.0),
        ("FGPIO 翻转", 14.6),
    ]
    names = [i[0] for i in items]
    vals = [i[1] for i in items]
    colors = [COLORS["blue"]] * 6 + [COLORS["green"]]

    fig, ax = new_fig(7.2, 4.0)
    bars = ax.barh(names, vals, color=colors, height=0.62, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(v + 2, b.get_y() + b.get_height() / 2, "%.1f ns" % v,
                va="center", fontsize=9, color=COLORS["dark"])
    ax.invert_yaxis()
    style_axes(ax, xlabel="单次操作耗时 (ns, mcycle 实测, 已扣空循环)", axis="x")
    ax.set_xlim(0, 150)
    save(fig, "04-gpio-ioc", "fig-04-2-gpio-bench")


if __name__ == "__main__":
    setup()
    fig_4_1()
    fig_4_2()
