#!/usr/bin/env python3
"""tools/figs/mcal_eth.py —— 第 49 章（Eth/EthTrcv）插图

图 49-1：AUTOSAR 以太网分层与本实现的调用关系（EthIf 桩 / Eth / EthTrcv / 板级）
图 49-2：ENET0 DMA 描述符环与缓冲池（TX 8 / RX 16，noncacheable 段）
图 49-3：链路切换与建链耗时（1000M/100M、Master/Slave、PHY down/up）
图 49-4：收发吞吐（板端 TX burst / 主机 flood→板端 RX / lwIP 基线对照）
图 49-5：裸帧 RTT 随帧长的变化（64/512/1500，avg/min/max）

数据来源：
  logs/20261102-162219-eth_raw_echo-host.log   RX flood（主机→板端）
  logs/*-eth_raw_echo.log                      TX burst / RTT / 链路
  logs/*-eth_trcv.log                          链路切换耗时
  第 13 章（lwIP 基线 935/466 Mbit/s）
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch


def box(ax, x, y, w, h, text, color, fs=9):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.03",
                                linewidth=1.2, edgecolor=color, facecolor=color + "22"))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, color=COLORS["dark"])


def arrow(ax, x1, y1, x2, y2, text="", color=None, fs=7.5):
    color = color or COLORS["gray"]
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=11,
                                 linewidth=1.1, color=color))
    if text:
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + 0.035, text, ha="center", fontsize=fs, color=color)


def fig_layers():
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
    box(ax, 0.05, 0.82, 0.9, 0.12, "应用 / PDU Router（本章不涉及）", COLORS["gray"])
    box(ax, 0.05, 0.66, 0.9, 0.12, "EthIf（本仓库只有弱符号桩：RxIndication / TxConfirmation / CtrlModeIndication）", COLORS["purple"])
    box(ax, 0.05, 0.42, 0.42, 0.17, "Eth 驱动（模块 88）\nMAC + DMA 描述符环 + 缓冲池 + 统计", COLORS["blue"])
    box(ax, 0.53, 0.42, 0.42, 0.17, "EthTrcv 驱动（模块 73）\n链路/速率/主从/自协商", COLORS["green"])
    box(ax, 0.05, 0.22, 0.9, 0.12, "Eth_PBcfg.c / EthTrcv_PBcfg.c（实例、引脚、时钟、PHY 地址、MAC、中断）", COLORS["orange"])
    box(ax, 0.05, 0.04, 0.9, 0.12, "HPM5E31 ENET0（DesignWare GMAC，RGMII）＋ YT8011AN（1000BASE-T1，MDIO 地址 1）", COLORS["dark"])
    arrow(ax, 0.5, 0.82, 0.5, 0.78, "L-SDU / 帧")
    arrow(ax, 0.26, 0.66, 0.26, 0.59, "Eth_Transmit / Eth_GetRxBuffer")
    arrow(ax, 0.74, 0.66, 0.74, 0.59, "EthTrcv_GetLinkState / MainFunction")
    arrow(ax, 0.47, 0.505, 0.53, 0.505, "Eth_ReadMii/WriteMii（MDIO 服务）", COLORS["teal"])
    arrow(ax, 0.26, 0.42, 0.26, 0.34)
    arrow(ax, 0.74, 0.42, 0.74, 0.34)
    arrow(ax, 0.5, 0.22, 0.5, 0.16)
    ax.text(0.5, 0.965, "图 49-1  AUTOSAR 以太网分层与本实现的分工", ha="center", fontsize=10.5, color=COLORS["dark"])
    save(fig, 49, "fig-49-1-eth-layers")


def fig_ring():
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
    box(ax, 0.02, 0.60, 0.30, 0.26, "TX 缓冲池\n8 × 1536 B\n.noncacheable.bss", COLORS["blue"], fs=8.5)
    box(ax, 0.36, 0.60, 0.28, 0.26, "TX 描述符环\n8 × 32 B\nOWN/TCH/FS/LS/IC", COLORS["orange"], fs=8.5)
    box(ax, 0.68, 0.60, 0.30, 0.26, "MAC + DMA\n补前导码/FCS/最小帧", COLORS["dark"], fs=8.5)
    box(ax, 0.02, 0.14, 0.30, 0.26, "RX 缓冲池\n16 × 1536 B\n.noncacheable.bss", COLORS["green"], fs=8.5)
    box(ax, 0.36, 0.14, 0.28, 0.26, "RX 描述符环\n16 × 32 B\nOWN/FS/LS/FL/ES", COLORS["orange"], fs=8.5)
    box(ax, 0.68, 0.14, 0.30, 0.26, "PHY / RGMII\nYT8011AN 1000BASE-T1", COLORS["purple"], fs=8.5)
    arrow(ax, 0.32, 0.73, 0.36, 0.73, "Buffer1")
    arrow(ax, 0.64, 0.73, 0.68, 0.73, "TDES2")
    arrow(ax, 0.68, 0.27, 0.64, 0.27, "RDES0.FL")
    arrow(ax, 0.36, 0.27, 0.32, 0.27, "Buffer1")
    ax.text(0.5, 0.50, "Eth_ProvideTxBuffer → Eth_Transmit（OWN=1，门铃）\nEth_GetRxBuffer → Eth_ReleaseRxBuffer（OWN=1 归还）",
            ha="center", fontsize=8.5, color=COLORS["gray"])
    ax.text(0.5, 0.965, "图 49-2  ENET0 DMA 描述符环与缓冲池", ha="center", fontsize=10.5, color=COLORS["dark"])
    save(fig, 49, "fig-49-2-dma-ring")


def fig_link():  # 保留函数：定稿章节用表格呈现链路切换数据，不再出图
    # 实测 logs/20261102-164119-eth_trcv.log：
    #   切 1000M/Slave 建链 11 ms；再切 Slave 19 ms；
    #   切 100M 与切 Master 均失败（对端转换盒只支持 1000BASE-T1 / 两端同角色），
    #   等待 5 s 超时，图上用 5002 ms + 虚线标出"超时"。
    labels = ["1000M Slave 首次", "Slave 再次", "100M 对端不支持", "Master 角色冲突"]
    t_link = [11, 19, 5002, 5002]
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    colors = [COLORS["blue"], COLORS["green"], COLORS["red"], COLORS["red"]]
    bars = ax.bar(labels, t_link, color=colors)
    for b, v in zip(bars, t_link):
        ax.text(b.get_x() + b.get_width() / 2, v, "%d ms" % v, ha="center", va="bottom", fontsize=8.5)
    ax.axhline(5000, color=COLORS["gray"], linestyle=":", linewidth=1)
    ax.text(0.02, 5060, "5 s 等待超时（链路起不来）", fontsize=7.5, color=COLORS["gray"])
    ax.set_ylabel("建链耗时 (ms)")
    ax.set_ylim(0, max(t_link) * 1.15)
    ax.grid(axis="y", alpha=0.25)
    ax.set_title("图 49-3  链路切换与建链耗时（eth_trcv 实测）", fontsize=10.5)
    save(fig, 49, "fig-49-3-link-switch")


def fig_tput():
    # 实测：
    #   TX submit：logs/20261102-163212-eth_raw_echo-host.log（64 B 143.9 Mbit/s、512 B 251.3 Mbit/s）
    #   RX flood ：logs/20261102-162219-eth_raw_echo-host.log（512 B 385.5 Mbit/s，0 丢包/0 crc）
    #   基线     ：第 13 章 SDK+lwIP iperf（TCP 935 / UDP 466 Mbit/s，应用层口径，不可直接比）
    labels = ["64 B", "512 B"]
    tx_mbit = [143.9, 251.3]
    rx_mbit = [0, 385.5]          # 64 B 未测
    x = range(len(labels))
    fig, ax = plt.subplots(figsize=(6.4, 3.8))
    w = 0.36
    b1 = ax.bar([i - w / 2 for i in x], tx_mbit, w, label="板端 TX burst（提交速率）", color=COLORS["blue"])
    b2 = ax.bar([i + w / 2 for i in x], rx_mbit, w, label="主机 flood → 板端 RX", color=COLORS["green"])
    for bars in (b1, b2):
        for b, v in zip(bars, [143.9, 251.3] if bars is b1 else [0, 385.5]):
            ax.text(b.get_x() + b.get_width() / 2, v + 6, "%.1f" % v if v else "n/a",
                    ha="center", va="bottom", fontsize=8)
    ax.axhline(1000, color=COLORS["gray"], linestyle=":", linewidth=1)
    ax.text(0.02, 1010, "1 Gbit/s 线速", fontsize=7.5, color=COLORS["gray"])
    ax.set_xticks(list(x)); ax.set_xticklabels(labels)
    ax.set_ylim(0, 1100)
    ax.set_ylabel("吞吐 (Mbit/s)")
    ax.legend(fontsize=8.5, loc="upper left")
    ax.grid(axis="y", alpha=0.25)
    ax.set_title("图 49-4  裸帧收发吞吐（实测，驱动口径）", fontsize=10.5)
    save(fig, 49, "fig-49-4-throughput")


def fig_rtt():
    # eth rtt 的三段计数（logs/20261102-164240-eth_raw_echo-host.log）：
    #   板端发送 5 帧、主机回显 3 帧、板端 seq 匹配 0 帧 —— 用来定位"RTT 未通"卡在哪一段
    stages = ["板端发送", "主机回显", "板端 seq 匹配"]
    counts = [5, 3, 0]
    fig, ax = plt.subplots(figsize=(6.0, 3.4))
    bars = ax.bar(stages, counts, color=[COLORS["blue"], COLORS["green"], COLORS["red"]])
    for b, v in zip(bars, counts):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.08, "%d" % v, ha="center", va="bottom", fontsize=9)
    ax.set_ylabel("帧数（一次 eth rtt 64 5）")
    ax.set_ylim(0, max(counts) * 1.3)
    ax.grid(axis="y", alpha=0.25)
    ax.set_title("图 49-5  eth rtt 三段计数：板端能收到回显帧，但 seq 匹配为 0", fontsize=9.5)
    save(fig, 49, "fig-49-5-rtt")


def main():
    setup()
    fig_layers()
    fig_ring()
    fig_tput()
    fig_rtt()
    print("done: docs/figures/49/")


if __name__ == "__main__":
    main()
