#!/usr/bin/env python3
"""tools/figs/net2_fig_13_5_tx_opt.py —— 图 13-5：上行（板→主机）发送路径优化前后对比

数据来源：tools/figs/net2_iperf_data.json 的 tx_opt 段（实测日志见第 13 章 6.3 节表格）
输出：docs/figures/13/fig-13-5-tx-opt.png
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS, new_fig, style_axes  # noqa: E402

import matplotlib.pyplot as plt


def main():
    data = json.loads((HERE / 'net2_iperf_data.json').read_text(encoding='utf-8'))
    tx = data['tx_opt']
    setup(font_size=9.5)
    fig, ax = new_fig(6.6, 3.8)
    labels = ['TCP 上行\n(优化前)', 'TCP 上行\n(优化后)', 'UDP 上行\n(优化前)', 'UDP 上行\n(优化后)']
    vals = [tx['tcp_up_before'], tx['tcp_up_after'], tx['udp_up_before'], tx['udp_up_after']]
    colors = [COLORS['gray'], COLORS['blue'], COLORS['gray'], COLORS['orange']]
    bars = ax.bar(labels, vals, color=colors, width=0.55, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + 12, '%.1f' % v, ha='center', va='bottom',
                fontsize=9.5, fontweight='bold', color=COLORS['dark'])
    ax.axhline(tx['driver_ceiling_mbps'], color=COLORS['red'], ls='--', lw=1.0, zorder=2)
    ax.text(3.42, tx['driver_ceiling_mbps'] + 8, '驱动层直发上限 %.0f Mbit/s（txbench）' % tx['driver_ceiling_mbps'],
            color=COLORS['red'], fontsize=8, ha='right')
    ax.axhline(1000, color=COLORS['purple'], ls=':', lw=0.9, zorder=2)
    ax.text(-0.42, 1008, '线速 1000 Mbit/s', color=COLORS['purple'], fontsize=8)
    ax.set_ylim(0, 1100)
    style_axes(ax, ylabel='吞吐 (Mbit/s)')
    fig.tight_layout()
    save(fig, 13, 'fig-13-5-tx-opt')


if __name__ == '__main__':
    main()