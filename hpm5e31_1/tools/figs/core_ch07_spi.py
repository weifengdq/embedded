#!/usr/bin/env python3
"""第 7 章 SPI 插图：图 7-1 四种模式时序、图 7-2 实测 SCLK 与理论带宽。

数据来源：logs/20261030-112819-spi_loopback_poll.log、logs/20261030-113457-spi_dma_bw.log
运行：cd hpm5e31_1 && python3 tools/figs/core_ch07_spi.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import render_dot, setup, save, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt


def fig_7_1():
    dot = r"""
digraph spi_modes {
  graph [rankdir=TB, fontname="Noto Sans CJK SC", fontsize=12, nodesep=0.25, ranksep=0.30, bgcolor="white", pad=0.2];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10.5, color="#57606A", fillcolor="#F6F8FA"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  m0 [label="Mode 0: CPOL=0 CPHA=0\nSCLK 空闲低, 第 1 个(上升)沿采样", fillcolor="#DBEAFE"];
  m1 [label="Mode 1: CPOL=0 CPHA=1\nSCLK 空闲低, 第 2 个(下降)沿采样", fillcolor="#BBF7D0"];
  m2 [label="Mode 2: CPOL=1 CPHA=0\nSCLK 空闲高, 第 1 个(下降)沿采样", fillcolor="#FDE68A"];
  m3 [label="Mode 3: CPOL=1 CPHA=1\nSCLK 空闲高, 第 2 个(上升)沿采样", fillcolor="#FCA5A5"];
  note [label="实测: 4 种模式传输同一组数据耗时相同\n(1024 B @20 MHz: 411~412 us, 见日志)", fillcolor="#F6F8FA"];

  m0 -> note; m1 -> note; m2 -> note; m3 -> note;
}
"""
    render_dot(dot, "07-spi", "fig-07-1-spi-modes")


def fig_7_2():
    # 实测: spi_sweep (Mbit/s) 与 sdma_bench (KB/s)
    req = [1, 5, 10, 20, 40]
    meas = [0.9986, 4.9889, 9.9659, 19.8851, 23.1663]
    bw = [124, 624, 1247, 2490, 3466]
    theory = [r / 8 * 1000 for r in req]  # KB/s = Mbit/s / 8 * 1000

    fig, ax = new_fig(7.2, 4.2)
    ax.plot(req, theory, "o--", color=COLORS["gray"], label="理论 = SCLK/8")
    ax.plot(req, bw, "s-", color=COLORS["blue"], label="DMA 实测吞吐 (KB/s)")
    for x, y in zip(req, bw):
        ax.text(x, y + 90, "%d" % y, ha="center", fontsize=8, color=COLORS["blue"])
    ax.plot([20, 40], [2490, 3466], "x", color=COLORS["red"], markersize=9,
            label="40 MHz 请求仅得 ~23 MHz 有效 SCLK")
    ax.set_xscale("log")
    ax.set_xticks(req)
    ax.set_xticklabels(["1", "5", "10", "20", "40"])
    style_axes(ax, xlabel="请求 SCLK (MHz, 对数轴)", ylabel="吞吐 (KB/s)", axis="y")
    ax.legend(frameon=False, loc="upper left", fontsize=8.5)
    save(fig, "07-spi", "fig-07-2-spi-throughput")


if __name__ == "__main__":
    setup()
    fig_7_1()
    fig_7_2()
