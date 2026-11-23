#!/usr/bin/env python3
"""tools/figs/app29_fig_proto.py —— 图 28-2：cannelloni v2 UDP 包格式（逐字段）

数据来源：ref2/cannelloni/{cannelloni.h,parser.cpp,decoder.cpp}
输出：docs/figures/28/fig-28-2-cannelloni-format.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


def main():
    setup(font_size=9)
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(10.5, 4.6),
                                  gridspec_kw={"height_ratios": [1.0, 1.0]})

    # ---------- 上：UDP 载荷 = 5B 头 + N 帧 ----------
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 10)
    ax.axis("off")
    ax.text(0, 9.0, "UDP 载荷（全部多字节字段为大端/网络字节序）", fontsize=9.5,
            fontweight="bold", color=COLORS["dark"])

    fields = [(0, 8, "version\n= 2", COLORS["blue"]),
              (8, 8, "op_code\n0=DATA", COLORS["teal"]),
              (16, 8, "seq_no\n自增", COLORS["orange"]),
              (24, 14, "count (u16)\n本包帧数", COLORS["green"])]
    for x, w, txt, c in fields:
        ax.add_patch(Rectangle((x, 6.2), w, 2.0, facecolor=c, alpha=0.18, edgecolor=c, lw=1.2))
        ax.text(x + w / 2, 7.2, txt, ha="center", va="center", fontsize=8.2, color=COLORS["dark"])
        ax.text(x + w / 2, 5.85, "%d B" % 1 if w == 8 else "", ha="center", fontsize=7.5, color=c)
    ax.text(38, 7.2, "帧 0 .. 帧 count-1（每帧 5~6 B 头 + 数据）", fontsize=8.5, va="center",
            color=COLORS["dark"])
    for i, x in enumerate((52, 62, 72, 82, 92)):
        ax.add_patch(Rectangle((x, 6.4), 8, 1.6, facecolor=COLORS["light"],
                               edgecolor=COLORS["gray"], lw=0.8))
        ax.text(x + 4, 7.2, "帧%d" % i, ha="center", va="center", fontsize=7.5)

    # 帧结构
    ax.text(0, 4.4, "单帧序列化（parser.cpp encodeFrame）", fontsize=9.5, fontweight="bold",
            color=COLORS["dark"])
    fr = [(2, 26, "can_id (u32, htonl)\nbit31 EFF / bit30 RTR", COLORS["purple"]),
          (28, 16, "len (u8)\nbit7=1 表示 CAN FD", COLORS["red"]),
          (44, 14, "flags (u8)\n仅 FD：BRS/ESI", COLORS["orange"]),
          (58, 40, "data[len & 0x7F]（0..64 B，RTR 帧无数据段）", COLORS["teal"])]
    for x, w, txt, c in fr:
        ax.add_patch(Rectangle((x, 1.5), w, 2.0, facecolor=c, alpha=0.16, edgecolor=c, lw=1.2))
        ax.text(x + w / 2, 2.5, txt, ha="center", va="center", fontsize=8.0, color=COLORS["dark"])
    ax.text(2, 0.6, "canfd_len(f) = f->len & ~0x80 ：0x88 -> 真实 8 字节 FD 帧；0x08 -> 经典 8 字节帧",
            fontsize=8.0, color=COLORS["red"])

    # ---------- 下：单包时间轴（攒包）----------
    ax2.set_xlim(0, 12)
    ax2.set_ylim(0, 4.2)
    ax2.set_yticks([])
    ax2.set_xlabel("时间 (ms)")
    ax2.set_title("cannelloni 攒包时序：-t 100000（默认）vs -t 1000（低延迟推荐）",
                  loc="left", fontsize=9.5, fontweight="bold", color=COLORS["dark"])
    ax2.grid(True, axis="x", zorder=0)
    for y, label, color in ((3.0, "-t = 100 ms（默认）", COLORS["red"]),
                            (1.2, "-t = 1 ms（run_tunnel.sh 默认）", COLORS["green"])):
        ax2.plot([0, 10], [y, y], color=colorsafe(color), lw=0.8, alpha=0.5)
        ax2.text(-0.1, y, label, ha="right", va="center", fontsize=8.2, color=color)
    for t in range(0, 11, 1):
        ax2.add_patch(plt.Rectangle((t + 0.15, 2.85), 0.5, 0.3, facecolor=COLORS["red"], alpha=0.55))
    for t in range(0, 11, 1):
        ax2.add_patch(plt.Rectangle((t + 0.15, 1.05), 0.15, 0.3, facecolor=COLORS["green"], alpha=0.7))
    ax2.annotate("每 100 ms 才发出 1 个 UDP 包\n→ 时延下限 ~100 ms", xy=(5.4, 3.0),
                 xytext=(6.4, 3.6), fontsize=8.0, color=COLORS["red"],
                 arrowprops=dict(arrowstyle="->", color=COLORS["red"], lw=1.0))
    ax2.annotate("1 ms 内攒到的帧立即发\n→ 时延下限 ~1 ms", xy=(2.2, 1.2), xytext=(6.4, 1.8),
                 fontsize=8.0, color=COLORS["green"],
                 arrowprops=dict(arrowstyle="->", color=COLORS["green"], lw=1.0))
    ax2.set_ylim(0.5, 4.0)

    fig.tight_layout()
    save(fig, 28, "fig-28-2-cannelloni-format")


def colorsafe(c):
    return c


if __name__ == "__main__":
    main()
