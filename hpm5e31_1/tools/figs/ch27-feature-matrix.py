#!/usr/bin/env python3
"""tools/figs/ch27-feature-matrix.py —— 图 27-2：同类 MCU 关键能力矩阵

输出：docs/figures/27/fig-27-2-feature-matrix.png
数据来源：各厂商官方产品页/数据手册（见第 27 章"参考来源"）；无法核实的单元格标"待确认"。
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

# 取值：2=支持/具备，1=部分支持或需外置，0=不支持/无，-1=待确认
FEATURES = [
    "RISC-V 内核",
    "主频 ≥480MHz",
    "片内 Flash ≥1MB",
    "4 路及以上 CAN FD",
    "EtherCAT 从站",
    "千兆以太网",
    "10BASE-T1S",
    "高精度 PWM(≤1ns)",
    "16 位 ADC",
    "双精度 FPU",
    "车规/功能安全",
]
MCUS = ["HPM5E31", "HPM6E00", "STM32H743", "GD32H759", "TC397"]
#     HPM5E31 HPM6E00 STM32H743 GD32H759 TC397
DATA = [
    [2, 2, 0, 0, 0],   # RISC-V
    [2, 2, 2, 2, -1],  # >=480MHz（TC397 主频待确认）
    [2, 2, 2, 2, -1],  # Flash
    [2, 2, -1, 2, -1], # CAN FD 路数（STM32H743/TC397 待确认）
    [2, 2, 0, 0, -1],  # EtherCAT
    [1, 2, 0, 0, -1],  # 千兆以太网（HPM5E31 为 1G MAC + 2x100M PHY）
    [1, 0, 0, 0, 0],   # 10BASE-T1S（HPM5E31 需外置 LAN8651）
    [2, 2, -1, -1, -1],# 高精度 PWM
    [2, -1, 2, -1, -1],# 16 位 ADC
    [2, 2, -1, -1, -1],# 双精度 FPU
    [-1, -1, -1, -1, -1],  # 功能安全（均未核实）
]
COLOR = {2: "#2EA043", 1: "#D29922", 0: "#DA3633", -1: "#D0D7DE"}
LABEL = {2: "支持", 1: "部分/需外置", 0: "不支持", -1: "待确认"}


def main():
    setup(font_size=9.5)
    fig, ax = plt.subplots(figsize=(8.6, 4.8))
    nrow, ncol = len(FEATURES), len(MCUS)
    for r in range(nrow):
        for c in range(ncol):
            v = DATA[r][c]
            ax.add_patch(Rectangle((c, nrow - 1 - r), 1, 1, facecolor=COLOR[v], alpha=0.28,
                                   edgecolor="white", linewidth=2))
            ax.text(c + 0.5, nrow - 1 - r + 0.5, LABEL[v], ha="center", va="center",
                    fontsize=8, color=COLORS["dark"])
    ax.set_xlim(0, ncol)
    ax.set_ylim(0, nrow)
    ax.set_xticks([c + 0.5 for c in range(ncol)])
    ax.set_xticklabels(MCUS, fontsize=10, fontweight="bold")
    ax.xaxis.tick_top()
    ax.set_yticks([nrow - 1 - r + 0.5 for r in range(nrow)])
    ax.set_yticklabels(FEATURES, fontsize=9)
    ax.tick_params(length=0)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_title("同类 MCU 关键能力矩阵（灰=待确认，数据见第 27 章参考来源）",
                 loc="left", fontsize=10, fontweight="bold", pad=18)
    fig.tight_layout()
    save(fig, 27, "fig-27-2-feature-matrix")


if __name__ == "__main__":
    main()
