#!/usr/bin/env python3
"""第 5 章 UART 插图：图 5-1 数据通路、图 5-2 波特率误差。

数据来源：logs/20261030-111055-uart_loopback.log
运行：cd hpm5e31_1 && python3 tools/figs/core_ch05_uart.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import render_dot, setup, save, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt


def fig_5_1():
    dot = r"""
digraph uart_paths {
  graph [rankdir=LR, fontname="Noto Sans CJK SC", fontsize=12, nodesep=0.30, ranksep=0.45, bgcolor="white", pad=0.2];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10.5, color="#57606A", fillcolor="#F6F8FA"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  txd  [label="TXD 引脚", fillcolor="#DBEAFE"];
  thr  [label="THR\n(写数据)"];
  txf  [label="TX FIFO 32B", fillcolor="#FDE68A"];
  sh   [label="发送移位寄存器"];
  rxd  [label="RX 引脚", fillcolor="#DBEAFE"];
  rxf  [label="RX FIFO 32B", fillcolor="#FDE68A"];
  rbr  [label="RBR\n(读数据)"];
  poll [label="① 轮询 LSR.DR", fillcolor="#BBF7D0"];
  irq  [label="② RX FIFO 触发中断\n+ 环形缓冲", fillcolor="#BBF7D0"];
  dma  [label="③ DMA\n(DMAMUX 请求)", fillcolor="#BBF7D0"];
  mcr  [label="MCR.LOOP=1\n内部回环(自测用)", fillcolor="#FCA5A5"];

  thr -> txf -> sh -> txd;
  txd -> mcr [style=dashed, label="内部回环"];
  mcr -> rxd [style=dashed];
  rxd -> rxf -> rbr;
  rbr -> poll;
  rxf -> irq;
  rxf -> dma;
}
"""
    render_dot(dot, "05-uart", "fig-05-1-uart-dataflow")


def fig_5_2():
    # 表 5-2 实测数据: 请求波特率 vs 实测误差(ppm), 源时钟 80 MHz
    baud = [9600, 38400, 115200, 460800, 921600, 1500000, 2000000, 3000000, 4000000, 5000000]
    err  = [-416, 1562, -2256, -13582, -13583, -12360, -19, 25606, -38, -46]

    fig, ax = new_fig(7.2, 4.0)
    xs = range(len(baud))
    colors = [COLORS["red"] if abs(e) > 10000 else COLORS["blue"] for e in err]
    ax.axhline(0, color=COLORS["gray"], linewidth=0.8)
    ax.axhline(30000, color=COLORS["orange"], linewidth=0.8, linestyle="--", label="驱动容差 +3%")
    ax.axhline(-30000, color=COLORS["orange"], linewidth=0.8, linestyle="--")
    ax.bar(xs, [e / 1000.0 for e in err], color=colors, width=0.6, zorder=3)
    for i, e in enumerate(err):
        ax.text(i, e / 1000.0 + (1.2 if e >= 0 else -2.4), "%.1f" % (e / 1000.0),
                ha="center", fontsize=7.5, color=COLORS["dark"])
    ax.set_xticks(list(xs))
    ax.set_xticklabels(["%.2g" % (b / 1000.0) if b < 1000000 else "%.1fM" % (b / 1e6) for b in baud], fontsize=8)
    style_axes(ax, xlabel="请求波特率", ylabel="误差 (‰, 千分之一)", axis="y")
    ax.set_ylim(-35, 35)
    ax.legend(loc="lower left", frameon=False)
    save(fig, "05-uart", "fig-05-2-baud-error")


if __name__ == "__main__":
    setup()
    fig_5_1()
    fig_5_2()
