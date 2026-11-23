#!/usr/bin/env python3
"""tools/figs/app38_fig2_fsm.py —— 图 37-2：app_mqtt 连接状态机与定时器

对应 components/app_mqtt/src/app_mqtt.c 的 app_mqtt_state_t 与 app_mqtt_poll()。
输出：docs/figures/37/fig-37-2-fsm.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch


def box(ax, x, y, w, h, text, color):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                                boxstyle="round,pad=0.012,rounding_size=0.012",
                                facecolor=color, alpha=0.16, edgecolor=color, linewidth=1.3))
    ax.text(x, y, text, ha="center", va="center", fontsize=7.6, color=COLORS["dark"])


def arrow(ax, x1, y1, x2, y2, text, color, rad=0.0, fs=6.6, dx=0.0, dy=0.02):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=9,
                                 color=color, lw=1.1,
                                 connectionstyle="arc3,rad=%.2f" % rad))
    ax.text((x1 + x2) / 2 + dx, (y1 + y2) / 2 + dy, text, ha="center", va="bottom",
            fontsize=fs, color=color)


def main():
    setup()
    fig, ax = plt.subplots(figsize=(7.4, 4.9))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    box(ax, 0.13, 0.80, 0.17, 0.10, "IDLE\n(未发起)", COLORS["gray"])
    box(ax, 0.13, 0.50, 0.19, 0.10, "TCP_CONNECTING\n(SYN 已发)", COLORS["blue"])
    box(ax, 0.13, 0.20, 0.17, 0.10, "WAIT_CONNACK\n(CONNECT 已发)", COLORS["orange"])
    box(ax, 0.50, 0.20, 0.19, 0.11, "CONNECTED\n(可收发, keepalive)", COLORS["green"])
    box(ax, 0.50, 0.52, 0.21, 0.10, "WAIT_RECONNECT\n(指数退避 1~30 s)", COLORS["red"])
    box(ax, 0.50, 0.82, 0.17, 0.09, "CLOSED\n(不再重连)", COLORS["gray"])

    arrow(ax, 0.13, 0.75, 0.13, 0.55, "app_mqtt_connect()\ntcp_new+tcp_connect", COLORS["blue"])
    arrow(ax, 0.13, 0.45, 0.13, 0.25, "tcp_connected 回调\n发 CONNECT", COLORS["orange"])
    arrow(ax, 0.215, 0.20, 0.40, 0.20, "CONNACK rc=0", COLORS["green"])
    arrow(ax, 0.40, 0.235, 0.215, 0.235, "SUBSCRIBE / 发 retained 在线状态", COLORS["teal"],
          rad=0.25, dy=-0.055, fs=6.2)
    arrow(ax, 0.42, 0.25, 0.46, 0.47, "TCP 断链 / PINGRESP 超时 /\n协议错误 / CONNACK rc!=0",
          COLORS["red"], rad=-0.12, fs=6.0, dx=-0.09)
    arrow(ax, 0.47, 0.47, 0.42, 0.24, "退避到点\n重新 connect", COLORS["blue"], rad=-0.12,
          fs=6.0, dx=0.10, dy=-0.05)
    arrow(ax, 0.605, 0.52, 0.58, 0.79, "auto_reconnect=0\n或主动 DISCONNECT", COLORS["gray"],
          fs=6.0, dx=0.06)
    arrow(ax, 0.50, 0.25, 0.47, 0.20, "", COLORS["green"])

    ax.text(0.02, 0.04, "定时器（app_mqtt_poll，1 ms 时基 sys_now）：connect_timeout 5 s | "
                        "keepalive 空闲发 PINGREQ、PINGRESP 超时断链 | QoS1 5 s 重发(DUP=1)",
            fontsize=6.8, color=COLORS["gray"])
    fig.tight_layout()
    save(fig, 37, "fig-37-3-fsm")


if __name__ == "__main__":
    main()