#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""can_08_msg_ram.py - MCAN 消息 RAM 默认布局（经典 CAN / CAN FD 两套）

数据来源: hpm_sdk/drivers/src/hpm_mcan_drv.c 的 mcan_get_default_ram_config() 注释：
  经典: SID 32 x 1 word, XID 32 x 2 words, TXBUF 32 x 16B, RXFIFO0 32 x 16B,
        RXFIFO1 32 x 16B, RXBUF 16 x 16B, TXEVT 32 x 8B
  FD  : SID 16 x 1 word, XID 16 x 2 words, TXBUF 8 x 72B, RXFIFO0 8 x 72B,
        RXFIFO1 8 x 72B, RXBUF 4 x 72B, TXEVT 8 x 8B
元素大小按 M_CAN: 过滤器标准元素 1 word / 扩展 2 words；报文元素 = 2 words 头 + 数据域。
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

from matplotlib import font_manager
font_manager.fontManager.addfont("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
plt.rcParams["font.sans-serif"] = ["Noto Sans CJK JP", "Noto Sans CJK SC"]
plt.rcParams["axes.unicode_minus"] = False

# (名称, 元素数, 每个元素字节, 颜色)
CLASSIC = [
    ("SID 过滤器", 32, 4, "#9ecae1"),
    ("XID 过滤器", 32, 8, "#6baed6"),
    ("TX Buffer/FIFO", 32, 16, "#fdae6b"),
    ("RX FIFO0", 32, 16, "#a1d99b"),
    ("RX FIFO1", 32, 16, "#74c476"),
    ("RX Buffer", 16, 16, "#c7e9c0"),
    ("TX Event FIFO", 32, 8, "#bcbddc"),
]
FD = [
    ("SID 过滤器", 16, 4, "#9ecae1"),
    ("XID 过滤器", 16, 8, "#6baed6"),
    ("TX Buffer/FIFO", 8, 72, "#fdae6b"),
    ("RX FIFO0", 8, 72, "#a1d99b"),
    ("RX FIFO1", 8, 72, "#74c476"),
    ("RX Buffer", 4, 72, "#c7e9c0"),
    ("TX Event FIFO", 8, 8, "#bcbddc"),
]


def draw(ax, title, layout):
    total = sum(n * sz for _, n, sz, _ in layout)
    y = 0
    for idx, (name, n, sz, color) in enumerate(layout):
        size = n * sz
        ax.add_patch(Rectangle((y, 0), size, 1, facecolor=color, edgecolor="#333333", linewidth=0.8))
        label = f"{name}\n{n}x{sz}B = {size}B"
        if size >= 200:
            ax.text(y + size / 2, 0.5, label, ha="center", va="center", fontsize=8, color="#111111")
        else:
            ha = "right" if (y + size) > (total - 160) else "center"
            ax.annotate(label, xy=(y + size / 2, 1.0),
                        xytext=(y + size / 2, 1.16 + 0.34 * (idx % 2)),
                        ha=ha, va="bottom", fontsize=6.5, color="#111111",
                        arrowprops=dict(arrowstyle="-", color="#777777", linewidth=0.5))
        y += size
    ax.set_xlim(0, total)
    ax.set_ylim(0, 1.55)
    ax.set_yticks([])
    ax.set_xlabel("字节偏移（相对本实例消息 RAM 起始）")
    ax.set_title(f"{title}\n合计 {total} B / 2560 B，剩余 {2560-total} B 未用", fontsize=10)
    # 标尺
    step = 512
    for x in range(0, total + 1, step):
        ax.plot([x, x], [-0.05, 0], color="#555555", linewidth=0.6)
        ax.text(x, -0.12, str(x), ha="center", va="top", fontsize=7)
    ax.set_ylim(-0.25, 1.55)


fig, axes = plt.subplots(2, 1, figsize=(11, 5.2), dpi=300)
draw(axes[0], "经典 CAN 默认布局（mcan_get_default_ram_config(..., false)）", CLASSIC)
draw(axes[1], "CAN FD 默认布局（mcan_get_default_ram_config(..., true)，64B 数据域）", FD)
fig.tight_layout()
out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "figures", "08", "fig-08-2-msg-ram.png")
out = os.path.abspath(out)
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, dpi=300, facecolor="white")
print(out)
