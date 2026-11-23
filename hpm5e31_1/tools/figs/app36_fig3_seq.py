#!/usr/bin/env python3
"""tools/figs/app36_fig3_seq.py —— 图 36-3：一次会话的 XRCE 请求/响应时序（实测抓包还原）

依据 logs/20261030-172701-xrce_pubsub.log 与 Agent 侧 /tmp/agent_run.log 的报文时间戳。
输出：docs/figures/36/fig-36-3-seq.png
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
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    lanes = {"板端 Client": 0.16, "Agent": 0.50, "ROS 2 节点": 0.84}
    for name, x in lanes.items():
        ax.add_patch(plt.Rectangle((x - 0.075, 0.02), 0.15, 0.12, color=COLORS["light"], zorder=2))
        ax.text(x, 0.08, name, ha="center", fontsize=9.0, fontweight="bold", color=COLORS["dark"])
        ax.plot([x, x], [0.14, 0.96], color=COLORS["gray"], lw=0.9, ls=":", zorder=1)

    def msg(y, frm, to, text, color, ls="-"):
        x1, x2 = lanes[frm], lanes[to]
        ax.add_patch(FancyArrowPatch((x1, y), (x2, y), arrowstyle="->", mutation_scale=9,
                                     color=color, lw=1.2, linestyle=ls, zorder=4))
        ax.text((x1 + x2) / 2, y + 0.012, text, ha="center", fontsize=7.8, color=color)

    msg(0.92, "板端 Client", "Agent", "CREATE_CLIENT（key=0x484D5035, session 0x81）", COLORS["blue"])
    msg(0.87, "Agent", "板端 Client", "STATUS OK（19 B）", COLORS["orange"])
    msg(0.81, "板端 Client", "Agent", "CREATE Participant/Publisher/Subscriber（92 B）", COLORS["blue"])
    msg(0.76, "Agent", "板端 Client", "STATUS OK ×3（14 B each）", COLORS["orange"])
    msg(0.70, "板端 Client", "Agent", "CREATE Topic + DataWriter（RELIABLE, depth 8）", COLORS["blue"])
    msg(0.65, "Agent", "板端 Client", "STATUS OK", COLORS["orange"])
    msg(0.59, "板端 Client", "Agent", "CREATE Topic + DataReader + READ_DATA(1)", COLORS["blue"])
    msg(0.54, "Agent", "板端 Client", "STATUS OK + 首次 READ_DATA 空响应", COLORS["orange"])
    msg(0.46, "板端 Client", "Agent", "WRITE_DATA hello（28 B, 10 Hz, 可靠流 0x80）", COLORS["purple"])
    msg(0.41, "Agent", "板端 Client", "ACKNACK/HEARTBEAT-ACK（流确认）", COLORS["orange"], ls="--")
    msg(0.35, "板端 Client", "Agent", "WRITE_DATA adc（32 B, 100 Hz, BE 流 0x01）", COLORS["purple"])
    msg(0.29, "Agent", "ROS 2 节点", "DDS 发布 /hpm5e31/adc（rt/hpm5e31/adc）", COLORS["green"])
    msg(0.22, "ROS 2 节点", "Agent", "DDS 发布 /hpm5e31/cmd（UInt8）", COLORS["green"])
    msg(0.17, "Agent", "板端 Client", "READ_DATA 响应（cmd 样本，20 B）", COLORS["orange"])

    ax.annotate("实测：命令往返 RTT\n2.29 / 2.70 / 3.44 ms", xy=(0.50, 0.20), xytext=(0.62, 0.055),
                fontsize=7.6, color=COLORS["red"],
                arrowprops=dict(arrowstyle="->", color=COLORS["red"], lw=0.9))
    ax.text(0.02, 0.985, "时间", fontsize=7.4, color=COLORS["gray"])
    fig.tight_layout()
    save(fig, 36, "fig-36-3-seq")


if __name__ == "__main__":
    main()
