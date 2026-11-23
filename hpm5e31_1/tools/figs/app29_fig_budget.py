#!/usr/bin/env python3
"""tools/figs/app29_fig_budget.py —— 图 28-4：CAN<->UDP 时延预算 + cannelloni -t 对比

数据来源：
  * tools/figs/app29_data/bench_t1000.json   —— -t 1000 us 实测（2026-10-30 17:39）
  * tools/figs/app29_data/bench_t100000.json —— -t 100000 us 实测（若存在则画对比柱）
  * 各段时延：① 攒包超时=参数本身；② ENET/lwIP 与 ⑥ vcan/主机 取 ping RTT 与经验值；
    ③ CAN 500k 一帧按位时序计算（8B 经典帧 ≈ 111 bit / 500 kbit = 0.222 ms）；④⑤ 取主循环实测裕量。
    图中把"参数/理论值"与"实测分位"分开标注，不混用。
输出：docs/figures/28/fig-28-4-budget.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt


def load(tag):
    p = HERE / "app29_data" / ("bench_%s.json" % tag)
    if not p.exists():
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def main():
    setup(font_size=9.5)
    d1 = load("t1000")
    d2 = load("t100000")
    if d1 is None:
        raise SystemExit("缺少 app29_data/bench_t1000.json")

    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(10.2, 3.7),
                                  gridspec_kw={"width_ratios": [1.45, 1.0]})

    # ---- 左：六段时延预算（-t 1000）----
    segs = [
        ("① 主机攒包 -t=1000us", 1.00, COLORS["blue"]),
        ("② 主机 UDP→板 ENET", 0.10, COLORS["teal"]),
        ("③ 网关→CAN1→总线→CAN0", 0.26, COLORS["orange"]),
        ("④ DUT 处理+回帧", 0.20, COLORS["green"]),
        ("⑤ 板 CAN1→UDP", 0.10, COLORS["purple"]),
        ("⑥ 主机 vcan→python-can", 0.30, COLORS["gray"]),
    ]
    left = 0.0
    for name, val, color in segs:
        ax.barh(0, val, left=left, height=0.45, color=color, alpha=0.85,
                edgecolor="white", linewidth=0.8, zorder=3)
        if val >= 0.18:
            ax.text(left + val / 2, 0, "%.2f" % val, ha="center", va="center",
                    fontsize=8, color="white", fontweight="bold", zorder=4)
        left += val
    ax.set_yticks([])
    ax.set_xlim(0, max(left, d1["latency"]["lat_p99"]) * 1.30)
    ax.set_xlabel("时延 (ms)")
    ax.set_title("(a) -t 1000 us 的六段时延预算（合计 %.2f ms，实测 p50 %.2f ms）"
                 % (left, d1["latency"]["lat_p50"]),
                 loc="left", fontweight="bold", color=COLORS["dark"], fontsize=9.5)
    ax.grid(True, axis="x", zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    # 图例（两列）
    import matplotlib.patches as mpatches
    handles = [mpatches.Patch(color=c, alpha=0.85, label=n) for n, _, c in segs]
    ax.legend(handles=handles, fontsize=7.4, ncol=2, loc="upper center",
              bbox_to_anchor=(0.5, -0.30), frameon=False)

    # ---- 右：p50/p99 对比 ----
    tags = []
    p50s = []
    p99s = []
    if d1:
        tags.append("-t 1000 us\n(run_tunnel 默认)")
        p50s.append(d1["latency"]["lat_p50"])
        p99s.append(d1["latency"]["lat_p99"])
    if d2:
        tags.append("-t 100000 us\n(cannelloni 默认)")
        p50s.append(d2["latency"]["lat_p50"])
        p99s.append(d2["latency"]["lat_p99"])
    x = range(len(tags))
    w = 0.34
    b1 = ax2.bar([i - w / 2 for i in x], p50s, w, color=COLORS["green"], zorder=3, label="p50")
    b2 = ax2.bar([i + w / 2 for i in x], p99s, w, color=COLORS["red"], zorder=3, label="p99")
    for bars, vals in ((b1, p50s), (b2, p99s)):
        for bar, v in zip(bars, vals):
            ax2.text(bar.get_x() + bar.get_width() / 2, v * 1.02, "%.1f" % v,
                     ha="center", va="bottom", fontsize=8.5, color=COLORS["dark"])
    ax2.set_xticks(list(x))
    ax2.set_xticklabels(tags, fontsize=8.5)
    ax2.set_ylabel("往返延迟 (ms)")
    ax2.set_ylim(0, max(p99s) * 1.28)
    ax2.set_title("(b) 攒包超时对延迟的影响", loc="left", fontweight="bold",
                  color=COLORS["dark"], fontsize=9.5)
    ax2.grid(True, axis="y", zorder=0)
    ax2.set_axisbelow(True)
    for side in ("top", "right"):
        ax2.spines[side].set_visible(False)
    ax2.legend(fontsize=8, frameon=False, loc="upper left")
    if abs(max(p99s) - min(p99s)) > 40:
        ax2.set_yscale("log")

    fig.tight_layout()
    save(fig, 28, "fig-28-4-budget")


if __name__ == "__main__":
    main()
