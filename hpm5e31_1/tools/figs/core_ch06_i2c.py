#!/usr/bin/env python3
"""第 6 章 I2C 插图：图 6-1 帧结构、图 6-2 probe 延迟构成。

数据来源：logs/20261030-113737-i2c_scan.log
运行：cd hpm5e31_1 && python3 tools/figs/core_ch06_i2c.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import render_dot, setup, save, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt


def fig_6_1():
    dot = r"""
digraph i2c_frame {
  graph [rankdir=LR, fontname="Noto Sans CJK SC", fontsize=12, nodesep=0.22, ranksep=0.35, bgcolor="white", pad=0.2];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10.5, color="#57606A", fillcolor="#F6F8FA"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  s   [label="START\nSCL 高时\nSDA 下降", fillcolor="#DBEAFE"];
  a7  [label="地址 7 位\n(A6..A0)", fillcolor="#FDE68A"];
  rw  [label="R/W\n0=写 1=读", fillcolor="#FDE68A"];
  ack [label="ACK\n从机拉低 SDA", fillcolor="#BBF7D0"];
  d8  [label="数据 8 位\n(可多个)", fillcolor="#FDE68A"];
  ack2[label="ACK\n每字节一个", fillcolor="#BBF7D0"];
  p   [label="STOP\nSCL 高时\nSDA 上升", fillcolor="#FCA5A5"];

  s -> a7 -> rw -> ack -> d8 -> ack2 -> p;
}
"""
    render_dot(dot, "06-i2c", "fig-06-1-i2c-frame")


def fig_6_2():
    # logs/20261030-113737-i2c_scan.log: 三种模式 probe ~751 us (含 5000 次 NACK 重试)
    modes = ["100k\n(normal)", "400k\n(fast)", "1M\n(fast_plus)"]
    probe_us = [751.139, 751.136, 751.139]
    hw_us = [90.0, 22.5, 9.0]   # 9 个 SCL 周期的理论时间

    fig, ax = new_fig(7.0, 4.0)
    x = range(len(modes))
    w = 0.56
    ax.bar(x, probe_us, w, color=COLORS["gray"], label="probe 总耗时（含驱动 NACK 重试）", zorder=3)
    ax.bar(x, hw_us, 0.18, color=COLORS["blue"], label="其中硬件传输（9 个 SCL 周期）", zorder=4)
    for i, v in enumerate(probe_us):
        ax.text(i, v + 12, "%.1f us" % v, ha="center", fontsize=8.5, color=COLORS["dark"])
    ax.set_xticks(list(x))
    ax.set_xticklabels(modes)
    style_axes(ax, xlabel="I2C 模式", ylabel="单次地址 probe 耗时 (us)", axis="y")
    ax.legend(frameon=False, fontsize=8.5, loc="upper right")
    ax.set_ylim(0, 900)
    save(fig, "06-i2c", "fig-06-2-i2c-scan")


if __name__ == "__main__":
    setup()
    fig_6_1()
    fig_6_2()
