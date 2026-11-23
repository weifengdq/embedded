#!/usr/bin/env python3
"""第 38 章 图 38-2：板端时间戳路径（PHY → MAC → DMA 描述符 → lwIP → PTPd → 时钟）。

纵向分层图。延迟量级与寄存器细节见第 38 章表 38-4 / 表 38-11 与 §2/§4。
运行：cd hpm5e31_1 && python3 tools/figs/app37_fig2_ts_path.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS, new_fig
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

ROWS = [
    ("YT8011AN 1000BASE-T1 PHY", "模拟前端收/发延迟数百 ns（也可由 PHY 自己打戳，本章未用）", COLORS["teal"]),
    ("HPM5E31 ENET MAC", "IEEE1588 时间戳单元：SYST_SEC/NSEC，SSINC=10 ns（100 MHz）", COLORS["blue"]),
    ("DMA 描述符", "RX：RDES6/RDES7；TX：TDES6/TDES7 + TTSE/TTSS", COLORS["purple"]),
    ("lwIP 移植层", "RX：enet_get_rx_timestamp()→pbuf->time_sec/nsec；TX：UDP319 帧置 TTSE", COLORS["green"]),
    ("PTPd 2.0.1", "取 p->time_sec/nsec 作 t1~t4；PI 伺服算 offset / meanPathDelay", COLORS["orange"]),
    ("ENET 硬件时钟", "TS_ADDEND 调频；SYST_*_UPD 调相", COLORS["red"]),
]


def main():
    setup()
    fig, ax = new_fig(9.8, 6.0)
    ax.set_xlim(0, 10.4)
    ax.set_ylim(0.0, 7.0)
    ax.axis("off")

    x0, w = 0.35, 9.70
    h, gap = 0.70, 0.24
    top = 6.45
    for i, (title, detail, color) in enumerate(ROWS):
        y = top - i * (h + gap) - h
        ax.add_patch(FancyBboxPatch((x0, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.09",
                                    linewidth=1.5, edgecolor=color, facecolor="white", zorder=2))
        ax.text(x0 + 0.18, y + h / 2, title, ha="left", va="center", fontsize=9.2,
                color=color, fontweight="bold", zorder=3)
        ax.text(x0 + 3.45, y + h / 2, detail, ha="left", va="center", fontsize=7.8,
                color=COLORS["dark"], zorder=3)
        if i:
            ax.add_patch(FancyArrowPatch((x0 + 0.60, y + h + gap - 0.02), (x0 + 0.60, y + h + 0.02),
                                         arrowstyle="-|>", mutation_scale=12, lw=1.4,
                                         color=COLORS["gray"], zorder=4))

    ax.text(0.35, 6.85, "MAC 打戳点之前的延迟是「线缆/PHY 固有延迟」，不进入 offset；"
                        "打戳点之后到协议栈的延迟必须由描述符时间戳旁路，否则引入 µs 级误差。",
            fontsize=8.2, color=COLORS["dark"])
    ax.text(0.35, 0.62, "硬件时间戳（本章）：t1~t4 全部来自描述符，绕开中断/轮询/协议栈抖动（ns 级）",
            fontsize=8.2, color=COLORS["blue"])
    ax.text(0.35, 0.26, "软件时间戳（若不用 LWIP_PTP）：在 netRecv() 里读当前时钟，抖动 = 轮询间隔 + 协议栈耗时（几十 µs）",
            fontsize=8.2, color=COLORS["red"])

    save(fig, 38, "fig-38-2-timestamp-path")


if __name__ == "__main__":
    main()