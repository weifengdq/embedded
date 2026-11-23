#!/usr/bin/env python3
"""tools/figs/app40_perf_enum.py —— 图 41-4：gs_usb 枚举结果与时间戳能力

数据来源：tools/figs/app40_data.json 的 enum 段
（实测日志 logs/20261102-103609-gs-usb-enum.log；枚举耗时由 host/gs_usb/bench_enum.py 采集）
输出：docs/figures/41/fig-41-4-enum-capability.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt


def main():
    d = json.loads((HERE / "app40_data.json").read_text(encoding="utf-8"))["enum"]
    setup(font_size=9.0)
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.2), gridspec_kw={"width_ratios": [1.0, 1.25]})

    # ---- 左：接口数量 + 枚举耗时 ----
    ax = axes[0]
    ifaces = d.get("ifaces") or 0
    bars = ax.bar(["期望接口数", "实际枚举"], [4, ifaces],
                  color=[COLORS["gray"], COLORS["blue"] if ifaces == 4 else COLORS["red"]], width=0.55, zorder=3)
    for b, v in zip(bars, [4, ifaces]):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.08, str(v), ha="center", fontsize=9.5,
                fontweight="bold", color=COLORS["dark"])
    ax.set_ylim(0, 5.45)
    ax.set_yticks([0, 1, 2, 3, 4])
    ax.set_ylabel("CAN 接口数")
    ax.set_title("netdev 枚举", loc="left", fontweight="bold", color=COLORS["dark"])
    nb = d.get("netdev_ready_ms")
    nu = d.get("usable_ms")
    if nb is None and nu is None:
        ax.text(0.5, 4.60, "枚举耗时：待实测", ha="center", fontsize=8.2, color=COLORS["red"])
    else:
        ax.text(0.5, 5.10, "netdev 出现: %s" % (("<1 ms（sysfs 异步）" if nb is not None and nb < 1 else "%.1f ms" % nb)
                                               if nb is not None else "待实测"),
                ha="center", fontsize=8.0, color=COLORS["dark"])
        ax.text(0.5, 4.84, "接口可配置: %s" % (("中位 %.1f ms（%.1f~%.1f）" % (nu, d.get("usable_min_ms", nu), d.get("usable_max_ms", nu)))
                                              if nu is not None else "待实测"),
                ha="center", fontsize=8.0, color=COLORS["dark"])
    ax.text(0.5, 4.55, "driver=%s  bus=%s" % (d.get("driver", "?"), d.get("bus_info", "?")),
            ha="center", fontsize=7.8, color=COLORS["gray"])
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

    # ---- 右：能力矩阵（硬件 vs 软件时间戳 / 收发） ----
    ax = axes[1]
    ax.axis("off")
    rows = [
        ("硬件发送时间戳", d.get("cap_hw_tx")),
        ("硬件接收时间戳", d.get("cap_hw_rx")),
        ("硬件原始时钟", d.get("cap_hw_raw_clock")),
        ("软件发送时间戳", d.get("cap_sw_tx")),
        ("软件接收时间戳", d.get("cap_sw_rx")),
    ]
    ax.text(0.02, 0.97, "ethtool -T 上报的能力", fontsize=9.0, fontweight="bold",
            color=COLORS["dark"], va="top")
    y = 0.84
    for name, ok in rows:
        color = COLORS["green"] if ok else COLORS["gray"]
        mark = "是" if ok else "否"
        ax.text(0.06, y, name, fontsize=8.6, color=COLORS["dark"], va="center")
        ax.text(0.93, y, mark, fontsize=10, color=color, va="center", ha="right", fontweight="bold")
        y -= 0.155
    if d.get("cap_hw_rx") and d.get("cap_hw_raw_clock"):
        ax.text(0.02, 0.05, "结论：固件已 advertise HW_TIMESTAMP\n"
                            "（SO_TIMESTAMPING 实测值见图 41-5/正文表）",
                fontsize=7.8, color=COLORS["green"], va="bottom")
    else:
        ax.text(0.02, 0.05, "结论：未 advertise 硬件时间戳", fontsize=7.8, color=COLORS["red"], va="bottom")

    fig.tight_layout()
    save(fig, 41, "fig-41-4-enum-capability")


if __name__ == "__main__":
    main()
