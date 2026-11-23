#!/usr/bin/env python3
"""net1_tc6_frames.py —— 图 10-2: OPEN Alliance TC6 的 SPI 帧格式（控制事务 / 数据块）

依据: OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface V1.1;
      LAN8650/1 DS60001734E §5.4/§5.5、Table 5-4、Figure 5-5/5-6。
数值: 控制事务 = 4 B 头 + 4 B×LEN + 4 B 忽略（LEN=1 时 12 B）;
      数据块 = 68 B（发送 4 B 头 + 64 B 负载；接收 64 B 负载 + 4 B 尾）。
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

BLUE, ORANGE, GREEN, PURPLE, GRAY = (COLORS["blue"], COLORS["orange"], COLORS["green"],
                                     COLORS["purple"], COLORS["gray"])
LIGHT = "#E7EEF8"
PALE = "#F3E8FD"
DARK = "#24292F"


def block(ax, x, y, w, text, color, tc="white", fs=9.0):
    ax.add_patch(Rectangle((x, y), w, 0.52, facecolor=color, edgecolor=DARK, linewidth=0.8, zorder=2))
    ax.text(x + w / 2, y + 0.26, text, ha="center", va="center", color=tc, fontsize=fs, zorder=3)


def row(ax, y, title, blocks, note=None):
    ax.text(0.0, y + 0.62, title, fontsize=9.8, fontweight="bold", color=DARK, va="bottom")
    x = 0.0
    for w, text, color, tc in blocks:
        block(ax, x, y, w, text, color, tc)
        x += w
    if note:
        ax.text(x + 0.3, y + 0.26, note, fontsize=8.6, color=GRAY, va="center")


def main():
    setup()
    fig, ax = plt.subplots(figsize=(9.6, 6.4))
    ax.set_xlim(-0.3, 22.5)
    ax.set_ylim(-1.7, 8.0)
    ax.axis("off")

    ax.text(0.0, 7.55, "OPEN Alliance TC6: 控制事务与数据块", fontsize=12.5, fontweight="bold", color=DARK)

    row(ax, 6.30, "控制读 (control read, LEN = 1 时共 12 B) — SDI / MOSI",
        [(4.0, "控制命令头 4 B", BLUE, "white"),
         (4.0, "0x00000000（忽略）", LIGHT, DARK),
         (4.0, "尾部 4 B（忽略）", LIGHT, DARK)])
    row(ax, 5.15, "  同一次事务的 SDO / MISO（整体延迟 4 字节）",
        [(4.0, "忽略 4 B", LIGHT, DARK),
         (4.0, "回显命令头 4 B", GREEN, "white"),
         (4.0, "寄存器值 4 B", GREEN, "white")])

    row(ax, 3.60, "控制写 (control write, LEN = 1 时共 12 B) — SDI / MOSI",
        [(4.0, "控制命令头 4 B", BLUE, "white"),
         (4.0, "写入值 4 B", ORANGE, "white"),
         (4.0, "尾部 4 B（忽略）", LIGHT, DARK)])
    row(ax, 2.45, "  同一次事务的 SDO / MISO（回显头 + 回显数据）",
        [(4.0, "忽略 4 B", LIGHT, DARK),
         (4.0, "回显命令头 4 B", GREEN, "white"),
         (4.0, "回显写入值 4 B", GREEN, "white")])

    row(ax, 0.95, "以太网数据块 (data chunk, 68 B；一次 SPI 事务可含 N 个块) — SDI / MOSI",
        [(4.0, "数据头 4 B", PURPLE, "white"),
         (12.0, "负载 64 B（以太网帧片段）", PALE, DARK)])
    row(ax, -0.20, "  接收方向 (SDO / MISO)：64 B 负载 + 4 B 尾",
        [(16.0, "负载 64 B（与上方 4+64 的 MOSI 错开 4 B 对齐）", PALE, DARK),
         (4.0, "尾 4 B", GREEN, "white")],
        note="TX credits / RX chunks / 错误位")

    ax.text(0.0, -1.10,
            "注：多字节字段均为大端（MSB first）。控制头 = DNC(31)=0 | HDRB(30) | WNR(29) | AID(28) | MMS(27:24) |"
            " ADDR(23:8) | LEN(7:1) | P(0，奇校验)。",
            fontsize=8.4, color=GRAY)
    ax.text(0.0, -1.45,
            "自证：读事务若 rx[4..7] == tx[0..3]，说明 SPI 四线、帧长度、采样边沿全部正确（本仓库驱动内建该检查）。",
            fontsize=8.4, color=GRAY)
    save(fig, 10, "fig-10-2-tc6-spi-frames")


if __name__ == "__main__":
    main()
