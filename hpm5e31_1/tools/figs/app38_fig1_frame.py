#!/usr/bin/env python3
"""tools/figs/app38_fig1_frame.py —— 图 37-1：MQTT 报文结构、CONNECT 逐字节、剩余长度变长编码

CONNECT 报文按本板实际配置计算（client_id=hpm5e31、will=hpm5e31/status、
遗嘱载荷 47 B、keepalive=1080 s），与板端 shell 的 "mqtt hex connect" 输出一致：
 00-01 固定头 10 54 | 02-11 可变头 | 12-85 载荷（共 86 B，剩余长度 0x54=84）
输出：docs/figures/37/fig-37-1-frame.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


def main():
    setup()
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7.6, 6.2),
                                   gridspec_kw={"height_ratios": [1.55, 1.0]})

    ax1.set_xlim(-1, 90)
    ax1.set_ylim(1.0, 4.0)
    ax1.axis("off")
    ax1.set_title("(a) CONNECT 报文逐字节（固定头 2 B + 可变头 10 B + 载荷 74 B = 86 B）",
                  fontsize=8.8, color=COLORS["dark"], loc="left")

    # (起点, 宽度, 块内文字或空, 颜色, 图例说明)；空文字的小块用编号标注
    blocks = [
        (0, 1, "", COLORS["blue"], "0x10：CONNECT 报文类型"),
        (1, 1, "", COLORS["orange"], "0x54：剩余长度 84 B"),
        (2, 2, "", COLORS["teal"], "00 04：协议名长度 4"),
        (4, 4, "MQTT", COLORS["teal"], "协议名 MQTT"),
        (8, 1, "", COLORS["purple"], "04：协议级别 4（3.1.1）"),
        (9, 1, "", COLORS["red"], "2E：连接标志"),
        (10, 2, "", COLORS["green"], "04 38：keepalive 1080 s"),
        (12, 2, "", COLORS["teal"], "00 07：client id 长度"),
        (14, 7, "hpm5e31", COLORS["gray"], "client id"),
        (21, 2, "", COLORS["teal"], "00 0E：will topic 长度 14"),
        (23, 14, "hpm5e31/status", COLORS["gray"], "will topic"),
        (37, 2, "", COLORS["teal"], "00 2F：will 载荷长度 47"),
        (39, 47, "遗嘱载荷 47 B：{\"dev\":\"hpm5e31\",\"online\":false,\"reason\":\"lwt\"}",
         COLORS["gray"], "will payload"),
    ]
    legend = []
    n = 0
    for start, width, label, color, note in blocks:
        ax1.add_patch(Rectangle((start, 2.35), width, 0.95, facecolor=color, alpha=0.22,
                                edgecolor=color, linewidth=1.1))
        if width >= 4:
            ax1.text(start + width / 2, 2.82, label, ha="center", va="center",
                     fontsize=6.8, color=COLORS["dark"])
        else:
            n += 1
            ax1.text(start + width / 2, 2.82, str(n), ha="center", va="center",
                     fontsize=7.0, color=COLORS["dark"])
            legend.append("(%d) %s" % (n, note))
    for b in range(0, 88, 4):
        ax1.plot([b, b], [2.22, 2.35], color=COLORS["gray"], lw=0.7)
        ax1.text(b, 2.14, str(b), ha="center", va="top", fontsize=5.8, color=COLORS["gray"])
    ax1.text(-1, 3.82, "连接标志 0x2E = clean(0x02) | will(0x04) | will_qos1(0x08) | will_retain(0x20)",
             fontsize=6.8, color=COLORS["dark"])
    # 小块图例（3 列）
    for i, item in enumerate(legend):
        col = i % 3
        row = i // 3
        ax1.text(-1 + col * 30, 1.85 - row * 0.30, item, fontsize=6.2, color=COLORS["gray"])
    ax1.text(-1, 1.10, "剩余长度 0x54 = 84 B（可变头 10 + client id 9 + will topic 16 + will 载荷 49）；载荷合计 74 B",
             fontsize=6.6, color=COLORS["dark"])

    # ---------- (b) 剩余长度变长编码 ----------
    ax2.set_xlim(0, 10)
    ax2.set_ylim(0, 5)
    ax2.axis("off")
    ax2.set_title("(b) 剩余长度 1~4 字节变长编码：每字节 7 位数据 + 1 位延续位",
                  fontsize=8.8, color=COLORS["dark"], loc="left")
    rows = [
        ("0", "0x00", "1 字节"),
        ("127", "0x7F", "1 字节（单字节上限）"),
        ("128", "0x80 0x01", "2 字节：128 = 0x00 + 1*128"),
        ("143", "0x8F 0x01", "2 字节：telemetry PUBLISH 载荷 143 B"),
        ("16383", "0xFF 0x7F", "2 字节上限"),
        ("16384", "0x80 0x80 0x01", "3 字节起点"),
        ("268435455", "0xFF 0xFF 0xFF 0x7F", "4 字节上限（协议最大）"),
    ]
    ax2.text(0.1, 4.55, "数值", fontsize=7.2, color=COLORS["gray"])
    ax2.text(2.2, 4.55, "线上字节（小端 7 位组，最高位=延续）", fontsize=7.2, color=COLORS["gray"])
    ax2.text(7.6, 4.55, "说明", fontsize=7.2, color=COLORS["gray"])
    for i, (val, raw, note) in enumerate(rows):
        y = 4.0 - i * 0.55
        ax2.text(0.1, y, val, fontsize=7.4, color=COLORS["dark"])
        ax2.text(2.2, y, raw, fontsize=7.4, color=COLORS["blue"], family="monospace")
        ax2.text(7.6, y, note, fontsize=7.0, color=COLORS["gray"])
    fig.tight_layout()
    save(fig, 37, "fig-37-1-frame")


if __name__ == "__main__":
    main()
