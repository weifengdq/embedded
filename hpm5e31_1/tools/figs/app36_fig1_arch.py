#!/usr/bin/env python3
"""tools/figs/app36_fig1_arch.py —— 图 36-1：XRCE-DDS 在 ROS 2 与 MCU 之间的分层架构

输出：docs/figures/36/fig-36-1-arch.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch


def box(ax, x, y, w, h, title, lines, color):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                                linewidth=1.1, edgecolor=color, facecolor="white", zorder=3))
    ax.text(x + w / 2, y + h - 0.035, title, ha="center", va="top", fontsize=9.5,
            fontweight="bold", color=color, zorder=4)
    for i, t in enumerate(lines):
        ax.text(x + w / 2, y + h - 0.085 - i * 0.045, t, ha="center", va="top",
                fontsize=8.0, color=COLORS["dark"], zorder=4)


def main():
    setup()
    fig, ax = plt.subplots(figsize=(7.2, 4.3))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    # 板端（左）
    box(ax, 0.02, 0.10, 0.25, 0.78, "HPM5E31 板端", [
        "应用：adc_pubsub / bridge",
        "app_xrce（会话/实体/统计）",
        "Micro-XRCE-DDS Client 3.0.2",
        "custom transport（UDP）",
        "lwIP 2.1.2（NO_SYS）",
        "ENET0 RGMII + YT8011AN",
    ], COLORS["blue"])

    # 网络（中）
    ax.add_patch(FancyBboxPatch((0.30, 0.30), 0.16, 0.36, boxstyle="round,pad=0.012",
                                linewidth=1.0, edgecolor=COLORS["gray"], facecolor=COLORS["light"], zorder=3))
    ax.text(0.38, 0.61, "千兆以太网 UDP", ha="center", fontsize=9.0, fontweight="bold", color=COLORS["gray"])
    ax.text(0.38, 0.52, "板 192.168.2.100:2019", ha="center", fontsize=7.8, color=COLORS["dark"])
    ax.text(0.38, 0.46, "Agent 192.168.2.1:2018", ha="center", fontsize=7.8, color=COLORS["dark"])
    ax.text(0.38, 0.39, "XRCE 报文 ≤512 B", ha="center", fontsize=7.8, color=COLORS["dark"])

    # Agent（右中）
    box(ax, 0.49, 0.10, 0.23, 0.78, "Micro-XRCE-DDS Agent", [
        "UDPv4 传输服务",
        "会话/实体代理（ProxyClient）",
        "Fast DDS 3.6.2 中间件",
        "RTPS over UDP（7400 起）",
        "DDS 域 0（ROS_DOMAIN_ID）",
    ], COLORS["orange"])

    # ROS 2 节点（右）
    box(ax, 0.75, 0.10, 0.23, 0.78, "ROS 2 Lyrical 节点", [
        "rclpy / rclcpp",
        "/hpm5e31/adc（订阅）",
        "/hpm5e31/cmd（发布）",
        "/hpm5e31/hello、status",
        "QoS: RELIABLE / BEST_EFFORT",
    ], COLORS["green"])

    # 箭头
    ax.add_patch(FancyArrowPatch((0.275, 0.62), (0.300, 0.62), arrowstyle="<->", mutation_scale=11,
                                 color=COLORS["blue"], lw=1.4, zorder=5))
    ax.add_patch(FancyArrowPatch((0.462, 0.62), (0.490, 0.62), arrowstyle="<->", mutation_scale=11,
                                 color=COLORS["orange"], lw=1.4, zorder=5))
    ax.add_patch(FancyArrowPatch((0.722, 0.62), (0.750, 0.62), arrowstyle="<->", mutation_scale=11,
                                 color=COLORS["green"], lw=1.4, zorder=5))
    ax.text(0.38, 0.20, "XRCE 协议\n（CREATE / WRITE_DATA /\nREAD_DATA / DELETE / HEARTBEAT）",
            ha="center", fontsize=8.0, color=COLORS["gray"])
    ax.text(0.87, 0.055, "DDS/RTPS：发布订阅、QoS、发现", ha="center", fontsize=8.0,
            color=COLORS["gray"], style="italic")
    fig.tight_layout()
    save(fig, 36, "fig-36-1-arch")


if __name__ == "__main__":
    main()
