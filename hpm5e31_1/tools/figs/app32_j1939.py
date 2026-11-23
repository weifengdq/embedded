#!/usr/bin/env python3
"""app32_j1939.py —— 第 32 章 J1939 插图（4 张）

图 32-1  29 位 CAN ID 位域与 PGN 计算（PDU1 / PDU2）
图 32-2  地址声明状态机（J1939-81）
图 32-3  BAM 与 RTS/CTS 时序对比（实测参数：BAM 50.0 ms 包间隔、CTS 窗口 ~1 ms）
图 32-4  DM1 报文布局（灯状态字节 + DTC 4 字节的 SPN/FMI/OC 位域）

数据来源：
  - 位域定义：SAE J1939-21 §4.3、J1939-73 §5.9、J1939-81 §4.3（公开摘要）；
  - 时序数字：logs/20261030-170808-j1939_tp.log（BAM/CTS 实测包间隔）；
  - 灯与 DTC：logs/20261030-172059-j1939_dm.log。
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle, FancyArrowPatch  # noqa: E402


def fig_id():
    """图 32-1：29 位 ID 位域 + PGN 计算。"""
    setup()
    fig, ax = plt.subplots(figsize=(9.4, 4.6))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")
    blue, orange, green, purple, gray = (COLORS["blue"], COLORS["orange"],
                                         COLORS["green"], COLORS["purple"], COLORS["gray"])

    # 29 位 ID 行
    y_id = 78
    fields = [("Priority", 3, blue), ("EDP", 1, orange), ("DP", 1, orange),
              ("PF", 8, green), ("PS", 8, purple), ("SA", 8, blue)]
    total_bits = sum(f[1] for f in fields)
    x = 4.0
    width = 92.0
    ax.text(2, y_id + 9, "29 位扩展 CAN ID（高位在左）", fontsize=9.5, color=gray)
    for name, bits, color in fields:
        w = width * bits / total_bits
        ax.add_patch(Rectangle((x, y_id), w, 8, facecolor=color, edgecolor="white", lw=1.0))
        ax.text(x + w / 2, y_id + 4, name, ha="center", va="center", color="white",
                fontsize=9, fontweight="bold")
        ax.text(x + w / 2, y_id - 3, "%d bit" % bits, ha="center", va="center",
                fontsize=8, color=gray)
        x += w

    # PGN 行
    y_pgn = 50
    ax.text(2, y_pgn + 9, "PGN（18 位：EDP+DP+PF+PS，PDU1 的 PS 位置为 0）", fontsize=9.5, color=gray)
    x = 4.0
    pgn_fields = [("EDP", 1, orange), ("DP", 1, orange), ("PF", 8, green), ("PS / GE", 8, purple)]
    total2 = 18
    for name, bits, color in pgn_fields:
        w = width * bits / total2
        ax.add_patch(Rectangle((x, y_pgn), w, 8, facecolor=color, edgecolor="white", lw=1.0))
        ax.text(x + w / 2, y_pgn + 4, name, ha="center", va="center", color="white",
                fontsize=9, fontweight="bold")
        x += w

    # 两个例子
    examples = [
        ("PGN 0x00EE00 地址声明:  PF=0xEE<240 -> PDU1，PS=DA=0xFF",
         "ID=0x18EEFF00  Prio=6  SA=0x00", y_pgn - 20),
        ("PGN 0x00FECA DM1:         PF=0xFE>=240 -> PDU2，PS=GE=0xCA",
         "ID=0x18FECA00  Prio=6  SA=0x00", y_pgn - 36),
    ]
    for text1, text2, y in examples:
        ax.text(4, y, text1, fontsize=8.8, color=COLORS["dark"])
        ax.text(4, y - 6.5, text2, fontsize=8.2, color=gray, family="monospace")

    save(fig, 32, "fig-32-1-j1939-id")


def fig_addr():
    """图 32-2：地址声明状态机。"""
    setup()
    fig, ax = plt.subplots(figsize=(9.2, 4.6))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")
    blue, orange, green, red, gray = (COLORS["blue"], COLORS["orange"], COLORS["green"],
                                      COLORS["red"], COLORS["gray"])

    boxes = {
        "UNCLAIMED": (6, 70, 20, 12),
        "WAIT": (38, 70, 22, 12),
        "CLAIMED": (72, 70, 22, 12),
        "CANNOT": (72, 26, 22, 12),
    }
    labels = {
        "UNCLAIMED": "UNCLAIMED\n上电/复位",
        "WAIT": "WAIT 250 ms\n请求 PGN 0xEA00",
        "CLAIMED": "CLAIMED\n广播 NAME(0xEE00)",
        "CANNOT": "CANNOT CLAIM\nSA = 0xFE",
    }
    for key, (x, y, w, h) in boxes.items():
        color = green if key == "CLAIMED" else (red if key == "CANNOT" else blue)
        ax.add_patch(Rectangle((x, y), w, h, facecolor=color, edgecolor="none", alpha=0.92))
        ax.text(x + w / 2, y + h / 2, labels[key], ha="center", va="center",
                color="white", fontsize=8.5)

    def arrow(p1, p2, text, dy=1.5, color=None):
        color = color or gray
        ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=12,
                                     color=color, lw=1.4))
        ax.text((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2 + dy, text, ha="center",
                fontsize=7.8, color=color)

    arrow((26, 76), (38, 76), "发送请求")
    arrow((49, 82), (49, 94), "收到同名更低 NAME 的声明", dy=0.8, color=orange)
    arrow((38, 94), (49, 94), "")
    ax.text(60, 88, "冲突：任意地址能力 -> 换址并重新请求\n固定地址 -> 发 SA=0xFE 的声明(CANNOT)",
            fontsize=7.6, color=orange)
    arrow((60, 76), (72, 76), "250 ms 内无冲突")
    arrow((83, 70), (83, 38), "上电即冲突且无任意地址能力", dy=0, color=red)
    ax.text(30, 30, "J1939-81 §4.5：地址声明期间用 SA=0xFE 发请求；\n"
                    "声明成功后周期 PGN 才允许发送（本实现据此门控调度器）。",
            fontsize=8.2, color=gray)
    save(fig, 32, "fig-32-2-addr-claim")


def fig_tp():
    """图 32-3：BAM 与 RTS/CTS 时序对比。"""
    setup()
    fig, ax = plt.subplots(figsize=(9.4, 4.8))
    ax.set_xlim(-2, 102)
    ax.set_ylim(0, 100)
    ax.axis("off")
    blue, orange, green, purple, gray = (COLORS["blue"], COLORS["orange"], COLORS["green"],
                                         COLORS["purple"], COLORS["gray"])

    ax.text(0, 96, "BAM（广播，50 ms 包间隔，无流控）", fontsize=9.5, color=blue)
    y1, y2 = 82, 68
    ax.text(-1, y1, "发送方", fontsize=8, color=gray, ha="right")
    ax.text(-1, y2, "接收方", fontsize=8, color=gray, ha="right")
    ax.plot([2, 50], [y1, y1], color=blue, lw=2)
    ax.plot([2, 50], [y2, y2], color=gray, lw=1)
    ax.add_patch(Rectangle((2, y1 - 4), 5, 8, facecolor=orange, edgecolor="none"))
    ax.text(4.5, y1 + 5, "CM(BAM)", fontsize=7.5, ha="center", color=orange)
    xs = [8 + 6.5 * i for i in range(6)]
    for i, x in enumerate(xs):
        ax.add_patch(Rectangle((x, y1 - 3), 3.5, 6, facecolor=blue, edgecolor="none"))
        ax.text(x + 1.7, y1 - 8, "DT%d" % (i + 1), fontsize=6.8, ha="center", color=gray)
    ax.annotate("", xy=(xs[1] + 1.7, y1 + 8), xytext=(xs[0] + 1.7, y1 + 8),
                arrowprops=dict(arrowstyle="<->", color=blue, lw=1.0))
    ax.text(xs[0] + 4.6, y1 + 9.5, "≈50.0 ms（实测 49.98 ms）", fontsize=7.6, color=blue)
    ax.text(52, y1 - 2, "…72 包/500 B", fontsize=7.6, color=gray)

    ax.text(0, 56, "RTS/CTS（点对点，窗口流控，实测 ≈1 ms/包）", fontsize=9.5, color=green)
    y3, y4 = 42, 26
    ax.text(-1, y3, "发送方", fontsize=8, color=gray, ha="right")
    ax.text(-1, y4, "接收方", fontsize=8, color=gray, ha="right")
    ax.plot([2, 92], [y3, y3], color=green, lw=2)
    ax.plot([2, 92], [y4, y4], color=gray, lw=1)
    seq = [("RTS", 2, 6, orange, y3), ("DT 1..16", 16, 22, green, y3),
           ("DT 17..32", 42, 22, green, y3), ("EOMA", 84, 6, orange, y3)]
    for name, x, w, color, y in seq:
        ax.add_patch(Rectangle((x, y - 4), w, 8, facecolor=color, edgecolor="none"))
        ax.text(x + w / 2, y + 5, name, fontsize=7.2, ha="center", color=color)
    cts = [("CTS(16)", 10, 5), ("CTS(16)", 34, 5), ("CTS(16)", 56, 5), ("CTS(16)", 76, 5)]
    for name, x, w in cts:
        ax.add_patch(Rectangle((x, y4 - 3), w, 6, facecolor=purple, edgecolor="none"))
        ax.text(x + w / 2, y4 - 7, name, fontsize=6.8, ha="center", color=purple)
    ax.text(2, 10, "T1=750 ms（等 DT） T2=1250 ms（等 CTS） T3=1250 ms（包间隙） T4=1050 ms（等 EOMA）",
            fontsize=7.8, color=gray)
    ax.text(2, 3, "BAM 500 B 实测 140.8 B/s；RTS/CTS 500 B 实测 6867.9 B/s（50 倍）",
            fontsize=8.0, color=COLORS["dark"])
    save(fig, 32, "fig-32-3-tp-timing")


def fig_dm():
    """图 32-4：DM1 布局（灯状态字节 + DTC 4 字节）。"""
    setup()
    fig, ax = plt.subplots(figsize=(9.4, 4.2))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")
    blue, orange, green, purple, gray = (COLORS["blue"], COLORS["orange"],
                                         COLORS["green"], COLORS["purple"], COLORS["gray"])

    ax.text(2, 92, "DM1（PGN 0xFECA，1 s 周期，单帧 8 字节）", fontsize=9.5, color=COLORS["dark"])
    # 字节 1：灯
    ax.text(2, 80, "byte1 灯状态（每灯 2 bit：0=off 1=on 2=error 3=N/A）", fontsize=8.4, color=gray)
    lamps = [("MIL", blue), ("RSL", orange), ("AWL", green), ("PL", purple)]
    x = 4
    for name, color in lamps:
        ax.add_patch(Rectangle((x, 68), 10, 7, facecolor=color, edgecolor="white"))
        ax.text(x + 5, 71.5, name, ha="center", va="center", color="white", fontsize=8.2)
        ax.text(x + 5, 65, "bit %d-%d" % (2 * (lamps.index((name, color))) + 1,
                                          2 * (lamps.index((name, color))) + 2),
                ha="center", fontsize=6.8, color=gray)
        x += 10

    ax.text(2, 54, "byte3-6 DTC1：SPN(19 bit) + FMI(5) + OC(7) + SPN 高位", fontsize=8.4,
            color=gray)
    ax.add_patch(Rectangle((4, 40), 20, 8, facecolor=blue, edgecolor="white"))
    ax.text(14, 44, "SPN[2:0] + FMI[4:0]", ha="center", va="center", color="white", fontsize=7.4)
    ax.add_patch(Rectangle((24, 40), 10, 8, facecolor=orange, edgecolor="white"))
    ax.text(29, 44, "OC[6:0]", ha="center", va="center", color="white", fontsize=7.4)
    ax.add_patch(Rectangle((34, 40), 12, 8, facecolor=green, edgecolor="white"))
    ax.text(40, 44, "SPN[10:3]", ha="center", va="center", color="white", fontsize=7.4)
    ax.add_patch(Rectangle((46, 40), 12, 8, facecolor=purple, edgecolor="white"))
    ax.text(52, 44, "SPN[18:11]", ha="center", va="center", color="white", fontsize=7.4)
    ax.text(60, 44, "（byte3 的 bit8 = SPN[19] 在 byte4）", fontsize=7.4, color=gray, va="center")

    ax.text(2, 26, "实测（logs/20261030-172059-j1939_dm.log）：SPN=100 / FMI=4 / OC=1 "
                   "→ byte3-6 = 24 01 0C 00", fontsize=8.2, color=COLORS["dark"])
    ax.text(2, 18, "灯字节：0x00 off -> 0x10 AWL -> 0x11 MIL+AWL -> 0x00（DM11 清除）",
            fontsize=8.2, color=COLORS["dark"])
    ax.text(2, 8, "SPN = (b0 & 7) | (b2<<3) | (b3<<11) | ((b1>>7)<<19)；FMI = b0>>3；OC = b1 & 0x7F",
            fontsize=8.0, color=gray, family="monospace")
    save(fig, 32, "fig-32-4-dm1-layout")


def main():
    fig_id()
    fig_addr()
    fig_tp()
    fig_dm()
    print("generated docs/figures/32/fig-32-1..4")


if __name__ == "__main__":
    main()
