#!/usr/bin/env python3
"""tools/figs/eth_tcp_fix_figs.py —— 第 49 章 TCP 收发停摆根因（TX 描述符发放顺序）插图

生成（输出 docs/figures/49/）：
  fig-49-8-tx-ring-order.png  TX 描述符环："按下标最小空闲发放" vs "TxNext 游标按序发放"
  fig-49-9-tx-timing.png      修复前后 ICMP echo request 的实际发出时刻（主机侧 tcpdump）
                              数据：tools/figs/data50/tx-order-before.csv / -after.csv，
                              分别由 logs/20261102-181021-t1a-probe.pcap 与
                              logs/20261102-182015-t1c-txorder.pcap 用 tcpdump 导出。

用法：python3 tools/figs/eth_tcp_fix_figs.py
"""
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, render_dot, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

CH = 49
DATA = Path(__file__).resolve().parent / "data50"


def fig_ring_order():
    dot = r"""
digraph txring {
  rankdir=TB;
  newrank=true;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.22, ranksep=0.34, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled",
         fillcolor="#F6F8FA", color="#6E7781", width=0.72, height=0.42];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#24292F"];

  subgraph cluster_bug {
    label="修复前：按下标最小空闲发放 → DMA 停在 old position，帧全部积压";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#CF222E";
    b0 [label="TD0\n已发/刚 FREE", fillcolor="#FFD7D5"];
    b1 [label="TD1\n已发", fillcolor="#FFD7D5"];
    b2 [label="TD2\n已发", fillcolor="#FFD7D5"];
    b3 [label="TD3\nDMA 停在这里\nOWN=0", fillcolor="#FFF1CC", color="#CF222E", penwidth=2];
    b4 [label="TD4\n新帧 OWN=1\n（发不出去）", fillcolor="#FFC9C7"];
    b5 [label="TD5\n新帧 OWN=1", fillcolor="#FFC9C7"];
    b6 [label="TD6\n新帧 OWN=1", fillcolor="#FFC9C7"];
    b7 [label="TD7\n新帧 OWN=1", fillcolor="#FFC9C7"];
    b0 -> b1 -> b2 -> b3 -> b4 -> b5 -> b6 -> b7 -> b0 [color="#8C959F"];
    n_new [label="上层新帧按 0,1,2 发放\n（下标最小）", shape=note, fillcolor="#FFEFC7", width=2.0];
    n_new -> b0 [color="#CF222E", style=dashed, label="第 9 帧落在 TD3 才解开"];
  }

  subgraph cluster_fix {
    label="修复后：TxNext 游标与 DMA 位置同步 → 提交即发";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#1A7F37";
    f0 [label="TD0"];
    f1 [label="TD1"];
    f2 [label="TD2"];
    f3 [label="TD3 ← TxNext\n== DMA", fillcolor="#DCFCE7", color="#1A7F37", penwidth=2];
    f4 [label="TD4"];
    f5 [label="TD5"];
    f6 [label="TD6"];
    f7 [label="TD7"];
    f0 -> f1 -> f2 -> f3 -> f4 -> f5 -> f6 -> f7 -> f0 [color="#8C959F"];
    n_fix [label="Eth_ProvideTxBuffer 只发\nTxNext 指向的缓冲；\nTransmit 成功后 TxNext++", shape=note,
           fillcolor="#DCFCE7", width=2.2];
    n_fix -> f3 [color="#1A7F37", style=dashed];
  }
}
"""
    render_dot(dot, CH, "fig-49-8-tx-ring-order")


def _load(kind):
    seq = []
    t = []
    with (DATA / ("tx-order-%s.csv" % kind)).open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            seq.append(int(row["seq"]))
            t.append(float(row["t_rel_s"]))
    return t, seq


def fig_tx_timing():
    setup()
    fig, axes = plt.subplots(1, 2, figsize=(8.6, 3.4), sharey=True)
    for ax, kind, title, color in (
        (axes[0], "before", "修复前（原版 Eth.c）", COLORS["red"]),
        (axes[1], "after", "修复后（TxNext 按序发放）", COLORS["green"]),
    ):
        t, seq = _load(kind)
        ax.scatter(t, seq, s=26, color=color, zorder=3)
        # 100 ms 间隔的理想节奏参考线
        ax.plot([0, max(t)], [min(seq), min(seq) + max(t) / 0.1], ":", color="#8C959F",
                linewidth=1.0, zorder=1, label="理想：每 100 ms 一帧")
        ax.set_title(title, fontsize=10)
        ax.set_xlabel("相对时间 (s)")
        ax.grid(linestyle=":", alpha=0.5)
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
    axes[0].set_ylabel("ICMP seq（板端发送序号）")
    axes[0].legend(fontsize=7.5, loc="lower right")
    axes[0].annotate("8 帧在 6 us 内一起喷出\n（seq 8,1,2,4,1,3,5,7）", xy=(4.1, 4.0),
                     xytext=(8.0, 2.0), fontsize=7.5, color=COLORS["red"],
                     arrowprops=dict(arrowstyle="->", color=COLORS["red"], lw=0.9))
    axes[0].annotate("之后 31 s 没有帧发出", xy=(20.0, 9.0), fontsize=7.5, color=COLORS["red"])
    save(fig, CH, "fig-49-9-tx-timing")


def main():
    fig_ring_order()
    fig_tx_timing()


if __name__ == "__main__":
    main()

