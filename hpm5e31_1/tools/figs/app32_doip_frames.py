#!/usr/bin/env python3
"""图 31-2：DoIP 通用头与四类载荷的逐字节布局（ISO 13400-2 表 6~表 9）

生成：python3 tools/figs/app32_doip_frames.py
产物：docs/figures/31/fig-31-2-frames.png
"""
import sys
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import COLORS, save, setup  # noqa: E402

ROWS = [
    ("通用头 8 B", ["版本\n0x02", "取反\n0xFD", "载荷类型\n2 B 大端", "载荷长度\n4 B 大端"], [1, 1, 2, 4]),
    ("0x0004 车辆声明 32 B", ["VIN\n17 B ASCII", "实体地址\n2 B", "EID\n6 B", "GID\n6 B", "further\n1 B"], [17, 2, 6, 6, 1]),
    ("0x0005 路由激活请求 7/11 B", ["源地址 SA\n2 B", "激活类型\n1 B", "ISO 保留\n4 B", "OEM 4 B\n(可选)"], [2, 1, 4, 4]),
    ("0x0006 路由激活响应 13 B", ["tester SA\n2 B", "实体地址\n2 B", "响应码\n1 B", "ISO 保留\n4 B", "OEM\n4 B"], [2, 2, 1, 4, 4]),
    ("0x8001 诊断报文 4+n B", ["源地址 SA\n2 B", "目标地址 TA\n2 B", "UDS 数据 n B（最长由 max data size 声明）"], [2, 2, 20]),
    ("0x8002/0x8003 应答 5 B", ["实体地址\n2 B", "tester 地址\n2 B", "应答码\n1 B"], [2, 2, 1]),
]

FIG_BYTES = {"通用头 8 B": 8, "0x0004 车辆声明 32 B": 32, "0x0005 路由激活请求 7/11 B": 11,
             "0x0006 路由激活响应 13 B": 13, "0x8001 诊断报文 4+n B": 24, "0x8002/0x8003 应答 5 B": 5}


def draw_row(ax, y, name, cells, widths, total, color):
    ax.text(0.0, y + 0.82, name, ha="left", va="center", fontsize=10,
            fontweight="bold", color=COLORS["dark"])
    x = 0.0
    for label, w in zip(cells, widths):
        ww = w / total * 10.0
        ax.add_patch(Rectangle((x, y), ww, 0.58, facecolor=color, edgecolor=COLORS["gray"], lw=0.8))
        ax.text(x + ww / 2, y + 0.29, label, ha="center", va="center", fontsize=7.6,
                color=COLORS["dark"], linespacing=1.15)
        x += ww


def main():
    setup()
    fig, ax = plt.subplots(figsize=(9.2, 4.6))
    ax.set_xlim(-0.1, 10.1)
    ax.set_ylim(0, 7.9)
    ax.axis("off")
    colors = [COLORS["blue"], COLORS["green"], COLORS["orange"], COLORS["purple"],
              COLORS["teal"], COLORS["red"]]
    y = 6.75
    for (name, cells, widths), col in zip(ROWS, colors):
        total = max(FIG_BYTES[name], sum(widths))
        draw_row(ax, y, name, cells, widths, total, col)
        y -= 1.12
    ax.text(0.0, 0.35, "注：所有多字节字段为大端（网络序）；0x0005 实测 EcuBus-Pro 等工具默认只发 7 B（不带 OEM），"
                       "0x0004 在 ISO 13400-2:2019 后追加 1 B VIN/GID 同步状态。",
            fontsize=8.2, color=COLORS["gray"], ha="left", va="center")
    save(fig, 31, "fig-31-2-frames")


if __name__ == "__main__":
    main()
