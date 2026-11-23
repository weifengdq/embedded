#!/usr/bin/env python3
"""tools/figs/app33_sdo_seg.py —— 图 33-3：SDO 分段下载（segmented download）报文时序

上：客户端 (主站) 请求；下：服务端 (节点 1) 响应。标注 ccs/scs/toggle/n 位域。
输出：docs/figures/33/fig-33-3-sdo-segmented.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt


def main():
    setup(font_size=9.0)
    fig, ax = plt.subplots(figsize=(6.9, 4.4))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    x_c, x_s = 2.4, 7.6
    ax.plot([x_c, x_c], [1.2, 9.2], color=COLORS["blue"], lw=1.4)
    ax.plot([x_s, x_s], [1.2, 9.2], color=COLORS["green"], lw=1.4)
    ax.text(x_c, 9.45, "SDO client（主站 / 上位机）", ha="center", color=COLORS["blue"], fontsize=9.5)
    ax.text(x_s, 9.45, "SDO server（节点 1）", ha="center", color=COLORS["green"], fontsize=9.5)

    idx = 0x2001

    def arrow(y, x0, x1, label, color, dashed=False):
        ax.annotate("", xy=(x1, y), xytext=(x0, y),
                    arrowprops=dict(arrowstyle="-|>", color=color, lw=1.3,
                                    ls="--" if dashed else "-", shrinkA=0, shrinkB=0))
        ax.text((x0 + x1) / 2, y + 0.16, label, ha="center", va="bottom", fontsize=8.0, color=color)

    y = 8.5
    arrow(y, x_c, x_s, "① initiate download req: 0x21 + index=%04X + size=1024" % idx, COLORS["blue"])
    y -= 0.55
    arrow(y, x_s, x_c, "② initiate download rsp: 0x60", COLORS["green"], dashed=True)

    for k in range(2):
        y -= 0.75
        arrow(y, x_c, x_s, "③ segment #%d: cmd=0x%02X (n=%d,t=%d,c=%d) + 7 B" %
              (k, (0 << 1) | ((k & 1) << 4), 0, k & 1, 0), COLORS["blue"])
        y -= 0.55
        arrow(y, x_s, x_c, "④ segment rsp: 0x%02X (t=%d)" % (0x20 | ((k & 1) << 4), k & 1), COLORS["green"],
              dashed=True)

    y -= 0.55
    ax.text(5.0, y, "...", ha="center", fontsize=12)
    y -= 0.6
    arrow(y, x_c, x_s, "⑤ last segment: c=1（1024 B = 147 段 × 7 B，最后一帧 n=4）", COLORS["blue"])
    y -= 0.55
    arrow(y, x_s, x_c, "⑥ last segment rsp: 0x%02X" % (0x20 | 0x10 | 0x01), COLORS["green"], dashed=True)

    ax.text(0.2, 0.55,
            "帧内容按小端：index/sub-index 低字节在前；toggle 每段翻转，收到重复 toggle 或 c 位不对 → 中止 (0x05030000)\n"
            "1 KB = 147 段 × (1 请求 + 1 响应) = 294 帧；4 KB = 586 段 = 1172 帧（每帧 8 B 经典 CAN）",
            fontsize=7.6, color=COLORS["dark"], va="bottom")

    fig.tight_layout()
    save(fig, 33, "fig-33-3-sdo-segmented")
    print("generated docs/figures/33/fig-33-3-sdo-segmented.png")


if __name__ == "__main__":
    main()
