#!/usr/bin/env python3
"""第 38 章 图 38-1：IEEE 1588 E2E + two-step 报文时序（主钟-从钟）。

示意图，时间轴按 1588-2019 子条款 9.3/11.3 绘制；实测数字来自
logs/20261030-170813-ptp_slave.log（E2E 单向路径延迟 13835 ns）。
运行：cd hpm5e31_1 && python3 tools/figs/app37_fig1_seq.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS, new_fig
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch

XL = 2.45   # 主钟轴
XR = 6.45   # 从钟轴


def main():
    setup()
    fig, ax = new_fig(9.2, 5.6)
    ax.set_xlim(0, 10.6)
    ax.set_ylim(-1.55, 7.9)
    ax.axis("off")

    ax.plot([XL, XL], [1.05, 7.55], color=COLORS["blue"], lw=2.4, zorder=1)
    ax.plot([XR, XR], [1.05, 7.55], color=COLORS["green"], lw=2.4, zorder=1)
    ax.text(XL, 7.70, "主钟 Master", ha="center", fontsize=11.0, color=COLORS["blue"])
    ax.text(XR, 7.70, "板端从钟 Slave", ha="center", fontsize=11.0, color=COLORS["green"])

    def node(x, y):
        ax.plot([x], [y], "o", ms=6.0, color=COLORS["dark"], zorder=6, mec="white", mew=1.0)

    def label_left(y, text, color=COLORS["dark"]):
        ax.text(XL - 0.16, y, text, ha="right", va="center", fontsize=9.0, color=color)

    def label_right(y, text, color=COLORS["dark"]):
        ax.text(XR + 0.16, y, text, ha="left", va="center", fontsize=9.0, color=color)

    def arrow(y0, y1, text, color, dy):
        ax.add_patch(FancyArrowPatch((XL + 0.08, y0), (XR - 0.08, y1), arrowstyle="-|>",
                                     mutation_scale=14, lw=1.7, color=color,
                                     shrinkA=1, shrinkB=1, zorder=4))
        ax.text((XL + XR) / 2.0, (y0 + y1) / 2.0 + dy, text, ha="center", va="center",
                fontsize=8.3, color=color, zorder=5)

    # t1 -> t2 Sync
    label_left(7.05, "t1  Sync 发出\n（MAC 硬件打戳）", COLORS["blue"])
    node(XL, 7.05)
    arrow(7.05, 6.55, "Sync（two-step, originTs=0）", COLORS["purple"], 0.30)
    node(XR, 6.55)
    label_right(6.55, "t2  Sync 到达（RDES6/7）", COLORS["green"])

    # Follow_Up
    node(XL, 5.95)
    arrow(5.95, 5.45, "Follow_Up（preciseOriginTimestamp = t1）", COLORS["purple"], -0.30)
    node(XR, 5.45)
    label_right(5.45, "t1 已知", COLORS["green"])

    # Delay_Req
    node(XR, 4.85)
    label_right(4.85, "t3  Delay_Req 发出（TDES6/7 + TTSE）", COLORS["green"])
    arrow(4.85, 4.35, "Delay_Req（UDP 319）", COLORS["orange"], -0.30)
    node(XL, 4.35)
    label_left(4.35, "t4  到达（打戳）", COLORS["blue"])

    # Delay_Resp
    node(XL, 3.75)
    arrow(3.75, 3.25, "Delay_Resp（receiveTimestamp = t4）", COLORS["orange"], 0.30)
    node(XR, 3.25)
    label_right(3.25, "t3' 收到", COLORS["green"])

    # Announce
    node(XL, 2.55)
    label_left(2.55, "Announce 周期", COLORS["blue"])
    arrow(2.55, 2.05, "Announce（GM 身份/优先级/clockClass）", COLORS["gray"], -0.28)
    node(XR, 2.05)
    label_right(2.05, "BMCA 选主钟", COLORS["gray"])

    # 公式与结论（放在底部，避免与事件标签打架）
    ax.text(0.25, -0.50, "① offset = (t2 − t1) − meanPathDelay，PI 伺服据此调频（TS_ADDEND）与调相（SYST_*_UPD）",
            fontsize=8.3, color=COLORS["dark"])
    ax.text(0.25, -0.88, "② meanPathDelay = [(t2 − t1) + (t4 − t3)] / 2 = 13830~13840 ns（实测，经 1000BASE-T1 转换盒）",
            fontsize=8.3, color=COLORS["dark"])
    ax.text(0.25, -1.20, "E2E（端到端）：中间交换设备/转换盒的排队与不对称会直接进入误差——本板精度的主要限制（U-37-1）。",
            fontsize=8.3, color=COLORS["dark"])
    ax.text(0.25, -1.48, "注：所有时刻均为 ENET MAC 硬件打戳点，与软件处理延迟无关；slaveOnly=1 的从钟只收不发。",
            fontsize=7.9, color=COLORS["gray"])

    save(fig, 38, "fig-38-1-ptp-e2e-sequence")


if __name__ == "__main__":
    main()