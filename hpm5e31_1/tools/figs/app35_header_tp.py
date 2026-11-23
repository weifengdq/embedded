#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 35-1 / 图 35-3：SOME/IP 报文头与 SOME/IP-TP 分段结构（脚本生成，300 dpi）

用法: python3 tools/figs/app35_header_tp.py
产物: docs/figures/35/fig-35-1-someip-header.png
      docs/figures/35/fig-35-3-tp-segment.png
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS   # noqa: E402
import matplotlib.pyplot as plt           # noqa: E402
from matplotlib.patches import FancyBboxPatch, Rectangle  # noqa: E402

C_BLUE = COLORS.get("blue", "#0969DA")
C_GREEN = COLORS.get("green", "#1A7F37")
C_ORANGE = COLORS.get("orange", "#BC4C00")
C_GREY = COLORS.get("grey", "#57606A")
C_PURPLE = COLORS.get("purple", "#8250DF")


def box(ax, x, y, w, h, text, color, fs=8.5, tc="white", bold=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.004,rounding_size=0.012",
                                linewidth=0.8, edgecolor="white", facecolor=color))
    ax.text(x + w / 2.0, y + h / 2.0, text, ha="center", va="center",
            fontsize=fs, color=tc, fontweight=("bold" if bold else "normal"))


def fig_header():
    setup()
    fig, ax = plt.subplots(figsize=(9.2, 5.4))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")

    # 16 B 头部：4 行，每行 4 B
    rows = [
        [("Message ID / Service ID (u16)", 50, C_BLUE), ("Method ID (u16)", 50, C_BLUE)],
        [("Length (u32) = 8 + payload 长度", 100, C_GREEN)],
        [("Request ID / Client ID (u16)", 50, C_PURPLE), ("Session ID (u16)", 50, C_PURPLE)],
        [("Protocol Version 0x01", 25, C_ORANGE), ("Interface Version", 25, C_ORANGE),
         ("Message Type", 25, C_ORANGE), ("Return Code", 25, C_ORANGE)],
    ]
    y = 78.0
    row_h = 9.0
    for ri, row in enumerate(rows):
        x = 4.0
        for (label, width, color) in row:
            w = width * 0.92
            box(ax, x, y, w, row_h, label, color, fs=8.0 + (0.3 if width >= 50 else 0.0))
            x += w + 0.6
        ax.text(1.5, y + row_h / 2.0, "%d" % (ri * 4), ha="right", va="center", fontsize=8, color=C_GREY)
        y -= row_h + 1.2
    ax.text(1.5, 92.0, "字节", ha="right", va="center", fontsize=8, color=C_GREY)

    # 标注：Length 语义 / 大端
    ax.annotate("Length = 从 Request ID 到报文末尾的字节数\n（不含 Message ID 与 Length 自身，最小 8）",
                xy=(50, 78 + 2 * (row_h + 1.2)), xytext=(55, 40),
                fontsize=8.2, color=C_GREEN,
                arrowprops=dict(arrowstyle="->", color=C_GREEN, lw=1.0))
    ax.text(4, 36, "所有多字节字段 = 大端（网络字节序）；用结构体 memcpy 会得到反序 —— 第 35 章坑 1",
            fontsize=8.4, color=C_GREY)

    # Message Type / Return Code 取值
    mt = "Message Type: 0x00 REQUEST / 0x01 REQUEST_NO_RETURN / 0x02 NOTIFICATION\n" \
         "0x80 RESPONSE / 0x81 ERROR（bit5=0x20 表示 SOME/IP-TP 分段）"
    rc = "Return Code: 0x00 E_OK / 0x01 E_NOT_OK / 0x02 E_UNKNOWN_SERVICE /\n" \
         "0x03 E_UNKNOWN_METHOD / 0x07 E_WRONG_PROTOCOL_VERSION / 0x09 E_MALFORMED"
    ax.text(4, 26, mt, fontsize=8.4, color=C_ORANGE, va="top")
    ax.text(4, 16, rc, fontsize=8.4, color=C_ORANGE, va="top")
    ax.text(4, 6, "图注：板端 components/app_someip/src/app_someip.c 的 app_someip_encode_header()/decode() 逐字段实现",
            fontsize=8.0, color=C_GREY)
    save(fig, 35, "fig-35-1-someip-header")


def fig_tp():
    setup()
    fig, ax = plt.subplots(figsize=(9.2, 4.6))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")

    ax.text(2, 94, "4096 B 逻辑报文 -> 3 个 TP 段（每段 UDP 载荷 <= 1400 B）", fontsize=9.5, color=C_GREY)

    # 逻辑报文
    box(ax, 2, 78, 94, 10, "逻辑报文 payload 4096 B（method 0x0010 get_bulk / 0x0007 crc32）", C_GREEN, fs=9.0)
    # 分段
    segs = [("段0  offset=0     data 1392 B", 0, 1392), ("段1  offset=1392  data 1392 B", 1392, 1392),
            ("段2  offset=2784  data 1312 B（末段 More=0）", 2784, 1312)]
    x = 2.0
    total_w = 94.0
    for label, off, length in segs:
        w = total_w * length / 4096.0
        box(ax, x, 60, w, 10, "", C_BLUE if off != 2784 else C_PURPLE)
        ax.text(x + w / 2.0, 65, "data %d B" % length, ha="center", va="center", fontsize=8.2, color="white")
        ax.text(x + w / 2.0, 56.5, label, ha="center", va="top", fontsize=7.6, color=C_GREY)
        x += w + 0.4

    # 段结构
    ax.text(2, 48, "每个 TP 段 = [ SOME/IP 头 16 B | TP 头 4 B | data ]，Message Type 置 0x20", fontsize=8.6, color=C_GREY)
    box(ax, 2, 36, 26, 8, "SOME/IP Header 16 B", C_ORANGE, fs=8.4)
    box(ax, 28.4, 36, 22, 8, "TP Header 4 B", C_PURPLE, fs=8.4)
    box(ax, 50.8, 36, 45, 8, "TP data <= 1392 B（87 x 16）", C_BLUE, fs=8.4)

    # TP 头位域
    y = 22.0
    ax.text(2, y + 8.5, "TP 头（大端 u32）位域：", fontsize=8.6, color=C_GREY)
    box(ax, 2, y, 62, 7, "Offset（bit31..4，单位 16 B，最大 2^28 x 16 B）", C_PURPLE, fs=8.2)
    box(ax, 64.4, y, 13, 7, "Rsv", C_GREY, fs=8.2)
    box(ax, 77.8, y, 18, 7, "More (1b)", C_ORANGE, fs=8.2)

    ax.text(2, 12, "约束：非末段数据长度必须是 16 的倍数（1392 = 87 x 16）；Offsets 必须按序、无空洞；\n"
                   "同一逻辑报文的 Service/Method/Client/Session 字段在所有段中一致（板端重组时校验，不一致整包丢弃）。",
            fontsize=8.2, color=C_GREY, va="top")
    ax.text(2, 3, "图注：app_someip_tp.c 的 app_someip_tp_send() / app_someip_tp_feed()；上位机重组见 host/someip/someip_client.py",
            fontsize=8.0, color=C_GREY)
    save(fig, 35, "fig-35-3-tp-segment")


if __name__ == "__main__":
    fig_header()
    fig_tp()
