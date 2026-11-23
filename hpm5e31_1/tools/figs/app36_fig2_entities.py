#!/usr/bin/env python3
"""tools/figs/app36_fig2_entities.py —— 图 36-2：XRCE 实体模型与对象 ID/流 ID 的对应关系

输出：docs/figures/36/fig-36-2-entities.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch


def node(ax, x, y, w, h, text, color, fs=8.4, bold=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.010,rounding_size=0.02",
                                linewidth=1.1, edgecolor=color, facecolor="white", zorder=3))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs,
            color=COLORS["dark"], fontweight="bold" if bold else "normal", zorder=4)


def link(ax, p1, p2, color=COLORS["gray"]):
    ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=color, lw=1.0, zorder=2)


def main():
    setup()
    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    node(ax, 0.36, 0.86, 0.28, 0.10, "Session（client_key=0x484D5035）", COLORS["red"], bold=True)
    node(ax, 0.34, 0.66, 0.32, 0.12, "Participant  oid=0x01/0x01\ndomain_id=0", COLORS["blue"], bold=True)
    link(ax, (0.50, 0.86), (0.50, 0.78), COLORS["red"])

    # 发布侧
    node(ax, 0.03, 0.40, 0.27, 0.13, "Topic  oid=0x01/0x02\nrt/hpm5e31/hello\nString_", COLORS["orange"])
    node(ax, 0.03, 0.18, 0.27, 0.13, "DataWriter  oid=0x01/0x05\nQoS RELIABLE, depth 8\n→ 可靠输出流 0x80", COLORS["orange"])
    node(ax, 0.03, 0.62, 0.27, 0.10, "Publisher  oid=0x01/0x03", COLORS["orange"])
    link(ax, (0.34, 0.72), (0.165, 0.72))
    link(ax, (0.165, 0.62), (0.165, 0.53))
    link(ax, (0.165, 0.40), (0.165, 0.31))

    # 订阅侧
    node(ax, 0.70, 0.40, 0.27, 0.13, "Topic  oid=0x03/0x02\nrt/hpm5e31/cmd\nUInt8_", COLORS["green"])
    node(ax, 0.70, 0.18, 0.27, 0.13, "DataReader  oid=0x03/0x06\nQoS BEST_EFFORT\n← 可靠输入流 0x80", COLORS["green"])
    node(ax, 0.70, 0.62, 0.27, 0.10, "Subscriber  oid=0x01/0x04", COLORS["green"])
    link(ax, (0.66, 0.72), (0.835, 0.72))
    link(ax, (0.835, 0.62), (0.835, 0.53))
    link(ax, (0.835, 0.40), (0.835, 0.31))

    # 第二组发布者（ADC）
    node(ax, 0.37, 0.30, 0.26, 0.20,
         "Topic  oid=0x02/0x02\nrt/hpm5e31/adc\nUInt16MultiArray_\n\nDataWriter oid=0x02/0x05\n→ BE 输出流 0x01",
         COLORS["purple"], fs=7.8)

    ax.text(0.50, 0.12, "对象 ID = 2 字节（ID 域 + 类型域）；流 ID = 1 字节（0x01~0x7F BE / 0x80~0xFF 可靠）",
            ha="center", fontsize=7.8, color=COLORS["gray"])
    ax.text(0.50, 0.04, "微控制器只维护 1 个 Session；Agent 侧每个实体一个 ProxyClient 对象",
            ha="center", fontsize=7.8, color=COLORS["gray"], style="italic")
    fig.tight_layout()
    save(fig, 36, "fig-36-2-entities")


if __name__ == "__main__":
    main()
