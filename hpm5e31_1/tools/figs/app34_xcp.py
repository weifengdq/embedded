#!/usr/bin/env python3
"""app34_xcp.py —— 第 34 章（XCP 标定测量）图生成脚本

生成（输出到 docs/figures/34/，300 dpi 白底 PNG）：
  fig-34-1-arch.png       XCP 协议栈分层与 CTO/DTO 数据流
  fig-34-2-odt-pack.png   DAQ 列表 -> ODT -> CAN 帧的打包过程（含时间戳占位）
  fig-34-4-measured.png   实测：事件率 / DAQ 帧率 / 时间戳抖动（数据来自固件自证日志）

运行：python3 tools/figs/app34_xcp.py
依赖：matplotlib（pyenv 已装）
"""
from __future__ import annotations

import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# 中文字体（与其它章图一致，避免 CJK 缺字告警）
plt.rcParams["font.sans-serif"] = ["Noto Sans CJK JP", "Noto Sans CJK SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "34")
os.makedirs(OUT, exist_ok=True)

C_BOX = "#E8F0FE"
C_BOX2 = "#FFF3E0"
C_BOX3 = "#E8F5E9"
C_EDGE = "#37474F"


def box(ax, x, y, w, h, text, fc=C_BOX, fs=9):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.03",
                                linewidth=1.0, edgecolor=C_EDGE, facecolor=fc))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs)


def arrow(ax, x1, y1, x2, y2, text="", fs=8):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>",
                                 mutation_scale=12, linewidth=1.0, color=C_EDGE))
    if text:
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + 0.03, text, ha="center", fontsize=fs, color="#B71C1C")


def fig_arch():
    fig, ax = plt.subplots(figsize=(9.2, 5.4))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.axis("off")
    ax.text(5, 5.75, "XCP 从站分层与 CTO/DTO 数据流（hpm5e31_1 CAN 例程）",
            ha="center", fontsize=11, weight="bold")

    box(ax, 0.3, 4.3, 2.6, 1.0, "主机（pyxcp / CANape）", C_BOX2)
    box(ax, 3.6, 4.3, 2.8, 1.0, "传输层\nxcp_port_can.c（MCAN0 500k）", C_BOX)
    box(ax, 7.0, 4.3, 2.7, 1.0, "协议层 xcp_core.c\n命令解析 + DAQ 引擎", C_BOX)
    box(ax, 3.6, 2.6, 2.8, 1.0, "事件源 GPTMR\n1 kHz / 100 Hz", C_BOX3)
    box(ax, 7.0, 2.6, 2.7, 1.0, "应用变量\nA2L 绝对地址", C_BOX3)
    box(ax, 3.6, 0.7, 6.1, 1.2,
        "CAN 总线帧：CMD 0x555(data[0]=命令码)   RES/DAQ 0x556(data[0]=PID, 0x00..0xFB=DAQ / 0xFF=RES)",
        "#FFFFFF", fs=8)

    arrow(ax, 2.9, 4.9, 3.6, 4.9, "CTO")
    arrow(ax, 6.4, 4.6, 7.0, 4.6, "解析")
    arrow(ax, 8.3, 4.3, 8.3, 3.6, "事件采样")
    arrow(ax, 7.0, 3.1, 6.4, 3.1, "读变量")
    arrow(ax, 3.6, 3.4, 5.0, 4.3, "xcp_event()")
    arrow(ax, 6.4, 2.7, 6.4, 1.9, "DAQ DTO")
    arrow(ax, 3.6, 4.3, 4.2, 1.9, "CMD")
    ax.text(0.5, 2.0, "板端固件（单 MCU）\n\n上：上位机侧\n下：MCU 侧\n\n采样在事件中断里打包，\n发送在主循环 xcp_poll() 里完成",
            fontsize=8, va="top")
    fig.tight_layout()
    p = os.path.join(OUT, "fig-34-1-arch.png")
    fig.savefig(p, dpi=300, facecolor="white")
    plt.close(fig)
    print("wrote", p)


def fig_odt2():
    fig, ax = plt.subplots(figsize=(9.2, 4.8))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 5.4)
    ax.axis("off")
    ax.text(5, 5.1, "DAQ 列表 -> ODT -> CAN 帧打包（时间戳占 ODT0 的 4 字节）",
            ha="center", fontsize=11, weight="bold")

    box(ax, 0.2, 2.9, 1.8, 1.2, "DAQ list 0\nEVT 1 kHz\nmode bit4=1", C_BOX)
    for i, (label, desc) in enumerate([
            ("ODT0", "PID=0 | ts 4B | adc 2B | u8 1B"),
            ("ODT1", "PID=1 | tick 4B | cpu 2B | pad 1B"),
            ("ODT2", "PID=2 | pid_mid 4B | pad 3B")]):
        y = 3.6 - i * 1.1
        box(ax, 2.3, y, 1.0, 0.8, label, C_BOX2)
        arrow(ax, 2.0, 3.5, 2.3, y + 0.4)
        # 帧格子
        cells = desc.split(" | ")
        x = 3.5
        widths = [1.1] + [max(0.9, 0.55 * len(c)) for c in cells[1:]]
        for c, w in zip(cells, widths):
            ax.add_patch(FancyBboxPatch((x, y), w, 0.8, boxstyle="square,pad=0",
                                        linewidth=0.8, edgecolor=C_EDGE,
                                        facecolor="#FFFDE7" if x < 3.5 + widths[0] else C_BOX3))
            ax.text(x + w / 2, y + 0.4, c, ha="center", va="center", fontsize=7.5)
            x += w
        ax.text(x + 0.1, y + 0.4, "= DLC 8", fontsize=7.5, va="center", color="#B71C1C")

    ax.text(0.3, 0.9, "要点：时间戳打开后 ODT0 只剩 3 字节放数据（8-1 PID-4 ts），\n"
                      "MAX_DTO=8 时主机必须把 4 字节变量排到 ODT1/ODT2，否则 WRITE_DAQ 报 ERR_DAQ_CONFIG。",
            fontsize=8.5, va="top")
    fig.tight_layout()
    p = os.path.join(OUT, "fig-34-2-odt-pack.png")
    fig.savefig(p, dpi=300, facecolor="white")
    plt.close(fig)
    print("wrote", p)


def fig_measured():
    # 数据来源：logs/20261030-172706-xcp_can_daq.log（板内自证 PASS）
    cfg_rate = 1000.0
    meas_rate = 999.0
    frames_per_s = 3097.0
    ts_sd = 0.0
    pack_us = 3.93
    fig, axs = plt.subplots(1, 3, figsize=(10.5, 3.6))
    fig.suptitle("第 34 章实测（logs/20261030-172706-xcp_can_daq.log，板内自证 PASS）",
                 fontsize=10, weight="bold")

    ax = axs[0]
    ax.bar(["配置", "实测"], [cfg_rate, meas_rate], color=["#90CAF9", "#1565C0"], width=0.55)
    ax.set_ylim(0, 1200)
    ax.set_ylabel("事件率 (Hz)")
    ax.set_title("DAQ 事件率", fontsize=9)
    for i, v in enumerate([cfg_rate, meas_rate]):
        ax.text(i, v + 25, "%.0f" % v, ha="center", fontsize=9)

    ax = axs[1]
    ax.bar(["list0 3 ODT\n(1 kHz)", "list1 1 ODT\n(100 Hz)"],
           [3000.0, 97.0], color=["#66BB6A", "#2E7D32"], width=0.55)
    ax.set_ylim(0, 3600)
    ax.set_ylabel("DAO 帧率 (frame/s)")
    ax.set_title("DAQ DTO 帧率（总计 %.0f frame/s）" % frames_per_s, fontsize=9)
    for i, v in enumerate([3000.0, 97.0]):
        ax.text(i, v + 60, "%.0f" % v, ha="center", fontsize=9)

    ax = axs[2]
    ax.bar(["时间戳间隔\nsd (us)", "每事件打包\n(us)"], [ts_sd, pack_us],
           color=["#FFA726", "#EF6C00"], width=0.55)
    ax.set_ylim(0, 5)
    ax.set_ylabel("us")
    ax.set_title("抖动与开销", fontsize=9)
    ax.text(0, 0.12, "%.1f" % ts_sd, ha="center", fontsize=9)
    ax.text(1, pack_us + 0.1, "%.2f" % pack_us, ha="center", fontsize=9)
    fig.tight_layout()
    p = os.path.join(OUT, "fig-34-4-measured.png")
    fig.savefig(p, dpi=300, facecolor="white")
    plt.close(fig)
    print("wrote", p)


if __name__ == "__main__":
    fig_arch()
    fig_odt2()
    fig_measured()
