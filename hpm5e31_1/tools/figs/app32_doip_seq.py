#!/usr/bin/env python3
"""图 31-4：一次完整 DoIP 诊断的时序（车辆发现 -> 路由激活 -> UDS -> 慢服务 0x78）

箭头上的时间是第 31 章表 31-7/31-8 的实测值（logs/*-host-doip-*.log）。
生成：python3 tools/figs/app32_doip_seq.py
产物：docs/figures/31/fig-31-4-sequence.png
"""
import sys
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import COLORS, save, setup  # noqa: E402

# (发送方, 接收方, 文本, 时间标注, 颜色, 线型)
EVENTS = [
    (0, 1, "UDP 0x0001 车辆识别请求", "T+0", COLORS["blue"], "--"),
    (1, 0, "UDP 0x0004 VIN/EID/GID/0x1000", "0.13 ms", COLORS["green"], "--"),
    (0, 1, "TCP SYN", "", COLORS["gray"], "-"),
    (1, 0, "TCP SYN+ACK", "", COLORS["gray"], "-"),
    (0, 1, "0x0005 路由激活请求 SA=0x0E00 AT=0x00", "", COLORS["orange"], "-"),
    (1, 0, "0x0006 响应 0x10 成功", "1.22 ms", COLORS["green"], "-"),
    (0, 1, "0x8001 [0x0E00|0x1000|10 03]", "", COLORS["purple"], "-"),
    (1, 0, "0x8002 ACK 0x00", "0.167 ms", COLORS["teal"], "-"),
    (1, 0, "0x8001 [0x1000|0x0E00|50 03 ...]", "0.28 ms", COLORS["green"], "-"),
    (0, 1, "0x8001 [31 01 0301] 慢例程", "", COLORS["purple"], "-"),
    (1, 0, "0x8002 ACK 0x00（回调只入队）", "0.167 ms", COLORS["teal"], "-"),
    (1, 0, "0x8001 [7F 31 78] NRC pending", "+150 ms 内", COLORS["red"], ":"),
    (1, 0, "0x8001 [71 01 0301 00 01] 最终响应", "150.6 ms", COLORS["green"], "-"),
    (0, 1, "（静默 300 s / 测试改 3 s）", "", COLORS["gray"], ":"),
    (1, 0, "TCP FIN（general inactivity 超时）", "2998.7 ms", COLORS["red"], ":"),
]


def main():
    setup()
    fig, ax = plt.subplots(figsize=(9.6, 6.6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, len(EVENTS) + 2.2)
    ax.axis("off")

    x_t, x_e = 2.2, 7.8
    top = len(EVENTS) + 1.2
    ax.plot([x_t, x_t], [0.4, top], color=COLORS["dark"], lw=1.2)
    ax.plot([x_e, x_e], [0.4, top], color=COLORS["dark"], lw=1.2)
    ax.text(x_t, top + 0.35, "测试仪 192.168.2.1\n(逻辑地址 0x0E00)", ha="center", va="bottom",
            fontsize=10, fontweight="bold", color=COLORS["blue"])
    ax.text(x_e, top + 0.35, "DoIP 实体 192.168.2.100\nHPM5E31 逻辑地址 0x1000", ha="center",
            va="bottom", fontsize=10, fontweight="bold", color=COLORS["green"])

    for i, (a, b, text, dt, color, ls) in enumerate(EVENTS):
        y = top - 0.55 - i
        x0, x1 = (x_t, x_e) if a == 0 else (x_e, x_t)
        arrow = FancyArrowPatch((x0, y), (x1, y), arrowstyle="-|>", mutation_scale=12,
                                color=color, lw=1.2, linestyle=ls, shrinkA=0, shrinkB=0)
        ax.add_patch(arrow)
        ax.text((x0 + x1) / 2, y + 0.12, text, ha="center", va="bottom", fontsize=8.2,
                color=COLORS["dark"])
        if dt:
            ax.text(x1 - 0.12 if x1 > x0 else x1 + 0.12, y + 0.06, dt,
                    ha="right" if x1 > x0 else "left", va="bottom", fontsize=7.4,
                    color=color, style="italic")
    save(fig, 31, "fig-31-4-sequence")


if __name__ == "__main__":
    main()
