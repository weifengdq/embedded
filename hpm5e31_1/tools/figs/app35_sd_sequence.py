#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 35-2：SOME/IP-SD 与服务/事件交互时序（脚本生成，300 dpi）

用法: python3 tools/figs/app35_sd_sequence.py
产物: docs/figures/35/fig-35-2-sd-sequence.png
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS   # noqa: E402
import matplotlib.pyplot as plt           # noqa: E402
from matplotlib.patches import FancyBboxPatch   # noqa: E402

C_BLUE = COLORS.get("blue", "#0969DA")
C_GREEN = COLORS.get("green", "#1A7F37")
C_ORANGE = COLORS.get("orange", "#BC4C00")
C_GREY = COLORS.get("grey", "#57606A")
C_PURPLE = COLORS.get("purple", "#8250DF")

LANES = [("板端 192.168.2.100\n(server)", 16.0),
         ("组播 224.244.224.245:30490", 48.0),
         ("主机 192.168.2.1\n(client)", 82.0)]


def head(ax, text, color):
    ax.add_patch(FancyBboxPatch((0.0, 0.0), 1.0, 1.0, boxstyle="round,pad=0.02",
                                transform=ax.transAxes, facecolor="none", edgecolor="none"))


def arrow(ax, x1, x2, y, text, color, dashed=False, above=True):
    ax.annotate("", xy=(x2, y), xytext=(x1, y),
                arrowprops=dict(arrowstyle="-|>", color=color, lw=1.2,
                                linestyle=("--" if dashed else "-"),
                                shrinkA=2, shrinkB=2))
    ax.text((x1 + x2) / 2.0, y + (1.4 if above else -2.2), text, ha="center",
            va="bottom" if above else "top", fontsize=7.8, color=color)


def main():
    setup()
    fig, ax = plt.subplots(figsize=(9.2, 6.6))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")

    # 生命线
    for name, x in LANES:
        ax.plot([x, x], [8, 93], color=C_GREY, lw=0.9, linestyle=":")
        ax.add_patch(FancyBboxPatch((x - 13, 93), 26, 5.5,
                                    boxstyle="round,pad=0.02,rounding_size=0.6",
                                    facecolor="#F6F8FA", edgecolor=C_GREY, lw=0.8))
        ax.text(x, 95.7, name, ha="center", va="center", fontsize=8.2, color="#24292F")

    y = 87.0
    # 1) 周期 OfferService（组播）
    arrow(ax, 16, 48, y, "OfferService（组播, TTL=3 s, 每 TTL/2=1.5 s 重发）", C_BLUE)
    arrow(ax, 48, 82, y - 3.0, "OfferService（组播, 被主机 SD 收到 -> service AVAILABLE）", C_BLUE)
    y -= 9.0

    # 2) FindService（可选）
    arrow(ax, 82, 48, y, "FindService（组播, 立刻要 offer）", C_ORANGE)
    arrow(ax, 48, 16, y - 3.0, "FindService 到达", C_ORANGE)
    arrow(ax, 16, 82, y - 6.0, "OfferService（单播回给请求方 SD 端点，实测 0.8 ms）", C_GREEN)
    y -= 13.0

    # 3) SubscribeEventgroup
    arrow(ax, 82, 48, y, "SubscribeEventgroup（单播, TTL=3 s, 带 IPv4 endpoint option）", C_PURPLE)
    arrow(ax, 48, 16, y - 3.0, "SubscribeEventgroup 到达 30490", C_PURPLE)
    ax.text(16.2, y - 6.0, "订阅表登记\n(ip:port, eventgroup, TTL)", fontsize=7.4, color=C_PURPLE, va="top")
    arrow(ax, 16, 82, y - 9.0, "SubscribeEventgroupAck（单播给订阅者 SD 端口, TTL=3 s）", C_GREEN)
    y -= 17.0

    # 4) 事件流
    for i, (period, ev) in enumerate([(100, "counter 0x8001"), (10, "adc 0x8002"),
                                      (500, "temperature 0x8004")]):
        arrow(ax, 16, 82, y - i * 4.2,
              "NOTIFICATION %s（%d ms, 只发给订阅者端点）" % (ev, period), C_GREEN, dashed=True)
    y -= 14.0

    # 5) 退订 / TTL 过期
    arrow(ax, 82, 48, y, "StopSubscribeEventgroup（TTL=0）或 TTL 到期自动摘除", C_BLUE)
    ax.text(2, 6.5, "说明：SD 报文固定发到 UDP 30490；Offer 里 IPv4 endpoint option 给出服务实例端口（本板 30501）。\n"
                    "订阅 TTL 到期后板端自动删表（app_someip_poll 里每轮检查），订阅者需每 TTL/2 重发续约。\n"
                    "图注：app_someip_sd.c（Offer/Find/Subscribe/Ack 状态机）+ 第 35 章 §实测。",
            fontsize=8.0, color=C_GREY, va="bottom")
    save(fig, 35, "fig-35-2-sd-sequence")


if __name__ == "__main__":
    main()
