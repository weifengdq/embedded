#!/usr/bin/env python3
"""tools/figs/app38_fig4_measured.py —— 图 37-4：MQTT 端到端时延的分跳拆解与关键指标

数据来源（全部为 2026-11-02 上板实测）：
  * 分跳 p50：host/mqtt/pcap_latency.py 解析
    logs/20261102-161054-host-mqtt-stress.txt（broker 默认 Nagle）
    logs/20261102-161616-host-mqtt-stress.txt（broker set_tcp_nodelay true）
  * 单条 RTT：logs/20261102-161106-host-mqtt-stress.json（默认 41.0 ms）
               logs/20261102-162009-host-mqtt-stress.json（0.282 ms）
  * 命令回执：logs/20261102-160247-host-mqtt-pub-period.json（1.05 ms）
  * 板→主机 QoS1 1000 条：logs/20261102-162341-mqtt_telemetry.log（55 ms, 18179 msg/s）
  * LWT(kill)：logs/20261102-162530-host-mqtt-lwt-kill.json（1.6 ms）
输出：docs/figures/37/fig-37-4-measured.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
import numpy as np


def main():
    setup()
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.6, 3.9))

    # ---------- (a) 分跳 p50：默认 vs TCP_NODELAY ----------
    hops = ["上位机→broker\n(lo)", "broker→板\n(enp6s0)", "broker→上位机\n(lo)"]
    before = [0.14, 0.12, 39.97]
    after = [0.01, 0.10, 0.02]
    x = np.arange(len(hops))
    w = 0.36
    b1 = ax1.bar(x - w / 2, before, w, color=COLORS["red"], alpha=0.85, label="mosquitto 默认")
    b2 = ax1.bar(x + w / 2, after, w, color=COLORS["green"], alpha=0.85,
                 label="set_tcp_nodelay true")
    ax1.set_yscale("log")
    ax1.set_ylim(0.005, 100)
    ax1.set_ylabel("单跳时延 p50 / ms（对数轴）")
    ax1.set_xticks(x)
    ax1.set_xticklabels(hops, fontsize=7.6)
    ax1.legend(fontsize=7.2, frameon=False)
    ax1.set_title("(a) 单条 MQTT 往返的三跳拆解（pcap 实测）", fontsize=8.6, loc="left")
    for bars in (b1, b2):
        for r in bars:
            h = r.get_height()
            ax1.text(r.get_x() + r.get_width() / 2, h * 1.15, ("%.2f" % h),
                     ha="center", fontsize=6.8, color=COLORS["dark"])
    ax1.annotate("40 ms 卡在 broker→订阅者：\nNagle + Linux delayed ACK",
                 xy=(2.18, 39.97), xytext=(0.55, 3.2), fontsize=6.8, color=COLORS["red"],
                 arrowprops=dict(arrowstyle="->", color=COLORS["red"], lw=0.9))
    ax1.grid(axis="y", ls=":", alpha=0.45)

    # ---------- (b) 关键指标 ----------
    items = ["单条 RTT\n(默认 broker)", "单条 RTT\n(TCP_NODELAY)", "命令回执\n(led ack)",
             "LWT(kill)\n遗嘱时延", "板→主机 QoS1\n单条均摊"]
    vals = [41.0, 0.282, 1.05, 1.6, 0.055]
    colors = [COLORS["red"], COLORS["green"], COLORS["blue"], COLORS["orange"], COLORS["purple"]]
    y = np.arange(len(items))
    bars = ax2.barh(y, vals, color=colors, alpha=0.85)
    ax2.set_xscale("log")
    ax2.set_xlim(0.02, 200)
    ax2.set_yticks(y)
    ax2.set_yticklabels(items, fontsize=7.2)
    ax2.invert_yaxis()
    ax2.set_xlabel("时延 / ms（对数轴）")
    ax2.set_title("(b) 关键时延指标（上板实测）", fontsize=8.6, loc="left")
    labels = ["41.0", "0.282", "1.05", "1.6", "0.055"]
    for r, lab in zip(bars, labels):
        ax2.text(r.get_width() * 1.15, r.get_y() + r.get_height() / 2, lab,
                 va="center", fontsize=7.0, color=COLORS["dark"])
    ax2.text(0.03, 4.62, "板→主机 QoS1：1000 条 / 55 ms = 18179 msg/s，0 丢包",
             fontsize=6.8, color=COLORS["gray"])
    ax2.grid(axis="x", ls=":", alpha=0.45)

    fig.tight_layout()
    save(fig, 37, "fig-37-4-measured")


if __name__ == "__main__":
    main()
