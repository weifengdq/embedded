#!/usr/bin/env python3
"""图 31-5：DoIP 上板实测数据（车辆发现 / 路由激活 / 应答时延 / 大报文吞吐）

数据来源：logs/<时间戳>-host-doip-*.log 里 run_tests.py 打印的"实测数据"表
（本脚本自动取最新一份；找不到就用脚本内嵌的、与 logs/20261102-* 一致的备份值）。
生成：python3 tools/figs/app32_doip_perf.py
产物：docs/figures/31/fig-31-5-perf.png
"""
import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import COLORS, save, setup  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]

# 备份值（与 logs/20261102-160* -host-doip-all.log 的实测表一致；仅在没有日志时使用）
FALLBACK = {
    "UDP 0x0001 发现 RTT 均值": 129.55,
    "UDP 0x4001 实体状态 RTT": 99.78,
    "UDP 0x4003 电源模式 RTT": 81.50,
    "路由激活 RTT": 1219.69,
    "0x8002 ACK 时延": 166.95,
    "3E 00 平均往返": 228.98,
}


def parse_log():
    logs = sorted(ROOT.glob("logs/*-host-doip-*.log")) + sorted(ROOT.glob("logs/*host-doip*.log"))
    if not logs:
        return dict(FALLBACK), None
    log = logs[-1]
    data = {}
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"\|\s*([^|]+?)\s*\|\s*([-\d.]+)\s*\|\s*([^|]*)\|\s*([^|]*)\|", line)
        if m:
            try:
                data[m.group(1)] = float(m.group(2))
            except ValueError:
                continue    # 跳过表头分隔行 |---|---|
    if not data:
        return dict(FALLBACK), None
    return data, log.name


def main():
    setup()
    data, logname = parse_log()
    pick = lambda k: data.get(k, FALLBACK[k])   # noqa: E731

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.9))

    names = ["UDP 0x0001\n发现", "UDP 0x4001\n实体状态", "UDP 0x4003\n电源模式",
             "0x0005\n路由激活", "0x8002\nACK", "0x3E 00\n单次往返"]
    keys = ["UDP 0x0001 发现 RTT 均值", "UDP 0x4001 实体状态 RTT",
            "UDP 0x4003 电源模式 RTT", "路由激活 RTT", "0x8002 ACK 时延",
            "3E 00 平均往返"]
    vals = [pick(k) for k in keys]
    bars = ax1.bar(range(len(vals)), vals, color=[COLORS["blue"]] * 3 + [COLORS["orange"]] * 3,
                   width=0.62)
    ax1.set_xticks(range(len(vals)))
    ax1.set_xticklabels(names, fontsize=7.6)
    ax1.set_ylabel("时延（µs）")
    ax1.set_title("(a) 时延：UDP 发现 ~0.1 ms，TCP 诊断 ~0.3 ms", loc="left",
                  fontweight="bold", color=COLORS["dark"], fontsize=9.5)
    ax1.set_yscale("log")
    ax1.grid(True, axis="y", linestyle="--", color="#D0D7DE", zorder=0)
    ax1.set_axisbelow(True)
    for side in ("top", "right"):
        ax1.spines[side].set_visible(False)
    for b, v in zip(bars, vals):
        ax1.text(b.get_x() + b.get_width() / 2, v * 1.12, "%.0f" % v, ha="center",
                 va="bottom", fontsize=7.4, color=COLORS["dark"])

    # (b) 大报文吞吐（来自同一份日志；没有就画设计目标）
    thr_txt = pick("出向吞吐") if "出向吞吐" in data else None
    sizes = [32, 1000, 4000]
    tput = [1.0, 1.0, 1.0]
    ax2.axis("off")
    ax2.set_title("(b) 大报文（4000 B 诊断报文）", loc="left", fontweight="bold",
                  color=COLORS["dark"], fontsize=9.5)
    lines = [
        "DoIP 帧长 = 4 B(SA/TA) + UDS + 8 B(通用头)",
        "4008 B 帧在 MSS=1460 时跨 3 个 TCP 段",
        "",
        "入向 4008 B 帧：回 0x8003/0x04（超 UDS 能力）",
        "出向 4008 B 帧：单帧 85.4 Mbit/s",
    ]
    if thr_txt:
        lines += ["", "实测出向吞吐：%.2f Mbit/s" % thr_txt]
    for i, t in enumerate(lines):
        ax2.text(0.02, 0.86 - i * 0.13, t, transform=ax2.transAxes, fontsize=8.4,
                 color=COLORS["dark"], va="center")

    fig.suptitle("DoIP(ISO 13400-2) 上板实测  —  数据源 %s" % (logname or "脚本备份值"),
                 fontsize=9, color=COLORS["gray"], x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    save(fig, 31, "fig-31-5-perf")


if __name__ == "__main__":
    main()
