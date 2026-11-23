#!/usr/bin/env python3
"""tools/figs/app38_fig3_qos1.py —— 图 37-3：QoS1 发布/订阅时序 + LWT + keepalive

时延数字取自第 37 章实测（pcap 与 host 侧报告）：
  命令往返 RTT p50 0.282 ms（broker 开 TCP_NODELAY）、PUBACK 由板端在 recv 回调内回、
  LWT(kill) 1.6 ms、keepalive=10 s 时 PINGREQ 每 ~10 s 一次。
输出：docs/figures/37/fig-37-3-qos1.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch


def main():
    setup()
    fig, ax = plt.subplots(figsize=(7.4, 5.2))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    lanes = {"上位机\n(host/mqtt)": 0.13, "broker\n(mosquitto)": 0.50, "板端\napp_mqtt": 0.87}
    for name, x in lanes.items():
        ax.add_patch(plt.Rectangle((x - 0.075, 0.02), 0.15, 0.115, color=COLORS["light"], zorder=2))
        ax.text(x, 0.078, name, ha="center", fontsize=7.6, fontweight="bold",
                color=COLORS["dark"])
        ax.plot([x, x], [0.135, 0.97], color=COLORS["gray"], lw=0.9, ls=":", zorder=1)

    def msg(y, frm, to, text, color, ls="-"):
        x1, x2 = lanes[frm], lanes[to]
        ax.add_patch(FancyArrowPatch((x1, y), (x2, y), arrowstyle="-|>", mutation_scale=9,
                                     color=color, lw=1.2, linestyle=ls, zorder=4))
        ax.text((x1 + x2) / 2, y + 0.011, text, ha="center", fontsize=6.8, color=color)

    msg(0.94, "上位机\n(host/mqtt)", "broker\n(mosquitto)", "PUBLISH hpm5e31/cmd/echo  QoS1 pid=1", COLORS["blue"])
    msg(0.885, "broker\n(mosquitto)", "板端\napp_mqtt", "PUBLISH（转发）", COLORS["blue"])
    msg(0.83, "板端\napp_mqtt", "broker\n(mosquitto)", "PUBACK pid=1（recv 回调内立即回）", COLORS["orange"])
    msg(0.775, "板端\napp_mqtt", "broker\n(mosquitto)", "PUBLISH hpm5e31/echo/reply  QoS0", COLORS["green"])
    msg(0.72, "broker\n(mosquitto)", "上位机\n(host/mqtt)", "PUBLISH（转发）→ 上位机算 RTT", COLORS["green"])
    ax.annotate("实测 RTT：p50 0.282 ms / p95 0.345 ms\n（broker 开 TCP_NODELAY；未开时 41 ms）",
                xy=(0.50, 0.735), xytext=(0.60, 0.62), fontsize=6.8, color=COLORS["red"],
                arrowprops=dict(arrowstyle="->", color=COLORS["red"], lw=0.9))

    msg(0.54, "板端\napp_mqtt", "broker\n(mosquitto)", "PINGREQ（空闲 9 s 触发，keepalive=10 s）", COLORS["purple"], ls="--")
    msg(0.49, "broker\n(mosquitto)", "板端\napp_mqtt", "PINGRESP", COLORS["purple"], ls="--")
    ax.text(0.02, 0.455, "keepalive：30 s 空闲实测 PINGREQ/PINGRESP 各 3 次；"
                         "PINGRESP 超时（>keepalive 或 5 s）则断链重连",
            fontsize=6.6, color=COLORS["gray"])

    msg(0.36, "上位机\n(host/mqtt)", "broker\n(mosquitto)", "PUBLISH hpm5e31/cmd/kill  QoS1", COLORS["blue"])
    msg(0.305, "broker\n(mosquitto)", "板端\napp_mqtt", "PUBLISH（转发）", COLORS["blue"])
    msg(0.25, "板端\napp_mqtt", "broker\n(mosquitto)", "tcp_abort()：不发 DISCONNECT", COLORS["red"], ls="--")
    msg(0.19, "broker\n(mosquitto)", "上位机\n(host/mqtt)", "遗嘱 hpm5e31/status {\"online\":false,\"reason\":\"lwt\"}", COLORS["red"])
    ax.annotate("LWT 实测 1.6 ms", xy=(0.50, 0.20), xytext=(0.63, 0.14), fontsize=6.8,
                color=COLORS["red"], arrowprops=dict(arrowstyle="->", color=COLORS["red"], lw=0.9))
    msg(0.10, "板端\napp_mqtt", "broker\n(mosquitto)", "1 s 后自动重连 CONNECT（retained online:true）", COLORS["gray"], ls=":")

    fig.tight_layout()
    save(fig, 37, "fig-37-2-qos1")


if __name__ == "__main__":
    main()