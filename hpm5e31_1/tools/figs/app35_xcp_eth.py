#!/usr/bin/env python3
"""app35_xcp_eth.py —— 第 34 章新增插图：XCPonEth 帧格式 / 吞吐对比 / 协议时序

数据来源（全部实测，禁止编造）：
  * CAN 500 kbit/s DAQ：logs/20261030-172706-xcp_can_daq.log（3097 frame/s，8 B DTO）；
  * XCPonEth 1 kHz/4 ODT：logs/20261102-155202-xcp_eth.log + logs/20261102-155202-host-xcp-eth-daq.log；
  * XCPonEth 10 kHz/8 ODT：logs/20261102-155848-xcp_eth.log + logs/20261102-155400-host-xcp-eth-stress.log。

用法：
  $HOME/pyenv/bin/python tools/figs/app35_xcp_eth.py
"""
import sys
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import COLORS, save, setup, style_axes  # noqa: E402

CHAPTER = "34"


def fig_header():
    """图 34-5：XCPonEth 4 B 头 + 单包多 ODT（一个 UDP 数据报）"""
    fig, ax = plt.subplots(figsize=(9.2, 4.9))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 58)
    ax.axis("off")

    def box(x, y, w, h, text, color, fs=8.4, tc="white"):
        ax.add_patch(Rectangle((x, y), w, h, facecolor=color, edgecolor="white", linewidth=1.2))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, color=tc)

    ax.text(1, 55, "一个 UDP 数据报（≤1472 B）= 多条 XCP 消息首尾相接（单包多 ODT）",
            fontsize=11, fontweight="bold", color=COLORS["dark"])

    y1 = 35
    x = 2
    # 消息 0：LEN/CTR/ODT0/填充
    box(x, y1, 5, 6, "LEN", COLORS["blue"], 7.4)
    box(x + 5, y1, 5, 6, "CTR", COLORS["teal"], 7.4)
    box(x + 10, y1, 13, 6, "ODT0", COLORS["green"], 8.2)
    box(x + 23, y1, 2, 6, "", COLORS["gray"], 7.0)
    ax.annotate("LEN = 载荷长度（不含 4 B 头）\n非最后一条含 4 B 对齐填充",
                xy=(x + 3, y1 + 6), xytext=(x + 1, y1 + 12), fontsize=8,
                arrowprops=dict(arrowstyle="->", color=COLORS["gray"], lw=0.8),
                color=COLORS["dark"])
    ax.annotate("CTR：每消息 +1，\n接收侧用它判丢包",
                xy=(x + 7.5, y1), xytext=(x + 4, y1 - 9), fontsize=8,
                arrowprops=dict(arrowstyle="->", color=COLORS["gray"], lw=0.8),
                color=COLORS["dark"])
    ax.text(x + 10.5, y1 + 7.5, "PID + 4 B 时间戳 + 数据（ODT0）", fontsize=7.8,
            color=COLORS["gray"])

    x += 25
    for i in (1, 2):
        box(x, y1, 5, 6, "LEN", COLORS["blue"], 7.4)
        box(x + 5, y1, 5, 6, "CTR", COLORS["teal"], 7.4)
        box(x + 10, y1, 13, 6, "ODT%d" % i, COLORS["green"], 8.2)
        box(x + 23, y1, 2, 6, "", COLORS["gray"], 7.0)
        x += 25
    # 最后一条：不填充
    box(x, y1, 5, 6, "LEN", COLORS["blue"], 7.4)
    box(x + 5, y1, 5, 6, "CTR", COLORS["teal"], 7.4)
    box(x + 10, y1, 13, 6, "ODT3", COLORS["green"], 8.2)
    ax.annotate("最后一条 LEN 精确、不填充", xy=(x + 11, y1), xytext=(x - 6, y1 - 9), fontsize=8,
                arrowprops=dict(arrowstyle="->", color=COLORS["gray"], lw=0.8),
                color=COLORS["dark"])

    # 说明行
    ax.text(2, 21.5, "实测（logs/20261102-155400-xcp_eth.log）：10 kHz 事件 × 8 ODT → 每个数据报 8 条消息，"
                     "9724 pkt/s、77791 msg/s、835.7 KB/s（6.85 Mbit/s 载荷）", fontsize=8.6,
            color=COLORS["dark"])
    ax.text(2, 18.0, "MAX_DTO=256 B（本板构型）：单条消息载荷上限；1472 B / 8 ≈ 92 条 9 B ODT 才是单包上限",
            fontsize=8.6, color=COLORS["gray"])
    ax.text(2, 14.5, "UDP/IP 层（Linux enp6s0 ↔ YT8011AN 1000BASE-T1）：无重传、无排序保证；XCP 用 CTR 统计丢包",
            fontsize=8.6, color=COLORS["gray"])
    ax.add_patch(FancyArrowPatch((2, 7.5), (96, 7.5), arrowstyle="<->", color=COLORS["gray"],
                                 lw=0.8, mutation_scale=8))
    ax.text(49, 9.0, "XCPonEth 消息流（LEN 小端 2 B + CTR 小端 2 B）", fontsize=8, ha="center",
            color=COLORS["gray"])
    save(fig, CHAPTER, "fig-34-5-eth-frame")


def fig_throughput():
    """图 34-6：CAN 500k vs XCPonEth 实测吞吐（载荷口径），附 1 GbE 线速参考"""
    labels = ["CAN 500k\n1 kHz/3 ODT", "XCPonEth 1 kHz\n4 ODT", "XCPonEth 10 kHz\n8 ODT"]
    msgs = [3097, 3960, 79124]
    mbit = [0.198, 0.42, 6.96]
    colors = [COLORS["gray"], COLORS["blue"], COLORS["green"]]

    fig, axes = plt.subplots(1, 2, figsize=(10.0, 4.2))
    ax = axes[0]
    bars = ax.bar(range(3), msgs, color=colors, width=0.55)
    ax.set_yscale("log")
    ax.set_xticks(range(3))
    ax.set_xticklabels(labels, fontsize=8.4)
    style_axes(ax, ylabel="DAQ 消息率（frame 或 msg / s，对数）")
    for b, v in zip(bars, msgs):
        ax.text(b.get_x() + b.get_width() / 2, v * 1.15, "%d" % v, ha="center", fontsize=8.4,
                color=COLORS["dark"])
    ax.set_ylim(1000, 300000)

    ax = axes[1]
    bars = ax.bar(range(3), mbit, color=colors, width=0.55)
    ax.set_yscale("log")
    ax.set_xticks(range(3))
    ax.set_xticklabels(labels, fontsize=8.4)
    style_axes(ax, ylabel="UDP/CAN 载荷吞吐（Mbit/s，对数）")
    for b, v in zip(bars, mbit):
        ax.text(b.get_x() + b.get_width() / 2, v * 1.18, "%.3g" % v, ha="center", fontsize=8.4,
                color=COLORS["dark"])
    ax.axhline(950, color=COLORS["red"], ls="--", lw=0.9)
    ax.text(0.05, 1100, "1 GbE 线速上限 ≈950 Mbit/s（1500 B 帧）", fontsize=8,
            color=COLORS["red"])
    ax.set_ylim(0.1, 2000)
    fig.text(0.01, 0.01, "数据：CAN logs/20261030-172706-xcp_can_daq.log；ETH logs/20261102-155848-*.log",
             fontsize=7.5, color=COLORS["gray"])
    fig.tight_layout()
    save(fig, CHAPTER, "fig-34-6-eth-throughput")


def fig_sequence():
    """图 34-7：主机 ↔ XCPonEth 从站命令时序与实测耗时"""
    fig, ax = plt.subplots(figsize=(9.4, 4.8))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 40)
    ax.axis("off")

    ax.text(12, 38, "主机（host/xcp/xcp_eth_client.py）", fontsize=9.5, color=COLORS["blue"],
            fontweight="bold")
    ax.text(70, 38, "从站（samples/app/xcp_eth）", fontsize=9.5, color=COLORS["green"],
            fontweight="bold")
    ax.plot([16, 16], [7, 36], color=COLORS["blue"], lw=1.0)
    ax.plot([76, 76], [7, 36], color=COLORS["green"], lw=1.0)

    steps = [
        ("CONNECT 0xFF", 34, "RES: resource/MAX_CTO/MAX_DTO"),
        ("GET_STATUS / GET_ID / GET_DAQ_CLOCK", 29, "每次 1 个 CTO + 1 个 RES"),
        ("DAQ 配置：FREE/ALLOC_DAQ/ALLOC_ODT/ALLOC_ODT_ENTRY/WRITE_DAQ", 24, "0xD6/0xD5/0xD4/0xD3/0xE2/0xE1"),
        ("SET_DAQ_LIST_MODE(0x10 时间戳) + START_STOP_SYNCH(1)", 19, "启动 DAQ"),
        ("DAQ DTO 数据流（从站 → 主机，单向）", 14, "1 个数据报 = N 条 ODT 消息"),
        ("START_STOP_SYNCH(0) / DISCONNECT", 8, "停止采集"),
    ]
    for text, y, note in steps:
        ax.annotate("", xy=(76, y), xytext=(16, y + 1.2),
                    arrowprops=dict(arrowstyle="->", color=COLORS["dark"], lw=1.1))
        ax.text(17, y + 1.7, text, fontsize=8.0, color=COLORS["dark"])
        ax.text(77.5, y + 0.4, note, fontsize=7.6, color=COLORS["gray"])

    ax.text(2, 4.0, "实测（本次台架，200 次 SHORT_UPLOAD）：RTT p50≈86~110 us，min≈78 us；"
                    "UDP 丢包时幂等命令自动重发", fontsize=8.0, color=COLORS["dark"])
    ax.text(2, 1.5, "DAQ 单向：1 kHz/4 ODT → 990 pkt/s；10 kHz/8 ODT → 9724 pkt/s、77791 msg/s",
            fontsize=8.0, color=COLORS["gray"])
    save(fig, CHAPTER, "fig-34-7-eth-sequence")


def main():
    setup()
    fig_header()
    fig_throughput()
    fig_sequence()
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
