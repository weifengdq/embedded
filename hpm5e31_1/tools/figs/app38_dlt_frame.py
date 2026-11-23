#!/usr/bin/env python3
"""tools/figs/app38_dlt_frame.py —— 图 39-1：DLT 报文结构（Standard Header + Extended Header）

输出：docs/figures/39/fig-39-1-dlt-frame.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt


def box(ax, x, y, w, h, text, color, fontsize=8.0):
    ax.add_patch(plt.Rectangle((x, y), w, h, facecolor=color, edgecolor=COLORS["gray"], lw=0.8))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fontsize, color=COLORS["dark"])


def main():
    setup(font_size=9.0)
    fig, ax = plt.subplots(figsize=(7.2, 3.6))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 60)
    ax.axis("off")

    y = 42
    ax.text(0, 57, "文件存储头（16 B：DLT\\x01 + sec + usec + ECU ID）+ Standard Header（4 B）", fontsize=9, color=COLORS["blue"])
    box(ax, 0, y, 12, 10, "HTYP\n(1 B)", COLORS["light"])
    box(ax, 12, y, 12, 10, "MCNT\n(1 B)", COLORS["light"])
    box(ax, 24, y, 26, 10, "LEN\n(2 B, 大端)", COLORS["light"])
    ax.text(26, y - 3, "含 header + payload 的总长", fontsize=7, color=COLORS["gray"])

    y2 = 22
    ax.text(0, 37, "Standard-extra（WTMS→TMSP 4 B）+ Extended Header（10 B：MSIN/NOAR/APID/CTID）", fontsize=9, color=COLORS["green"])
    box(ax, 0, y2, 10, 10, "MSIN\n(1 B)", COLORS["light"])
    box(ax, 10, y2, 8, 10, "NOAR\n(1 B)", COLORS["light"])
    box(ax, 18, y2, 12, 10, "APID\n(2 B)", COLORS["light"])
    box(ax, 30, y2, 12, 10, "CTID\n(2 B)", COLORS["light"])
    box(ax, 42, y2, 18, 10, "TMSP\n(4 B, 0.1 ms)", COLORS["light"])

    y3 = 6
    ax.text(0, 17, "Payload（verbose 模式：参数个数 + 每个参数的 (type, len, data)）", fontsize=9,
            color=COLORS["orange"])
    box(ax, 0, y3, 12, 9, "NOAR/参数数\n(1 B)", COLORS["light"])
    box(ax, 12, y3, 20, 9, "参数 1: type,len,data", COLORS["light"])
    box(ax, 32, y3, 20, 9, "参数 2: type,len,data", COLORS["light"])
    box(ax, 52, y3, 12, 9, "...", COLORS["light"])

    ax.annotate("", xy=(90, 25), xytext=(64, 25),
                arrowprops=dict(arrowstyle="-|>", color=COLORS["red"], lw=1.2))
    ax.text(66, 27, "UDP 3490\n(或 dlt-daemon Unix socket)", fontsize=7.5, color=COLORS["red"])

    ax.text(0, 1.5, "HTYP: UEH(bit0) WEID(bit1) WTMS(bit2) WTVS(bit3) WSID(bit4)  |  "
                    "MSIN: 0=verbose 1=log 2=control 3=buffer overflow",
            fontsize=6.8, color=COLORS["gray"])

    fig.tight_layout()
    save(fig, 39, "fig-39-1-dlt-frame")
    print("generated docs/figures/39/fig-39-1-dlt-frame.png")


if __name__ == "__main__":
    main()
