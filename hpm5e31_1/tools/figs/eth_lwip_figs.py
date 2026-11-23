#!/usr/bin/env python3
"""tools/figs/eth_lwip_figs.py —— 第 49 章 "lwIP 移植 / FreeRTOS 集成" 两节的插图

生成（全部输出 docs/figures/49/）：
  fig-49-5-lwip-datapath.png   MCAL Eth 后端 vs SDK enet 后端的数据通路对比（含零拷贝位置）
  fig-49-6-rtos-tasks.png      FreeRTOS 版任务/优先级/数据流
  fig-49-7-sysarch-cost.png    sys_arch 原语开销实测（logs/20261102-153226-rtos_lwip_selftest.log）

用法：python3 tools/figs/eth_lwip_figs.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, render_dot, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

CH = 49


def fig_datapath():
    """图 49-1：两条数据通路。左=SDK enet 驱动（描述符直指 pbuf），右=MCAL Eth（驱动缓冲）。"""
    dot = r"""
digraph datapath {
  rankdir=TB;
  newrank=true;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.26, ranksep=0.30, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled", fillcolor="#F6F8FA", color="#6E7781"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#24292F"];

  subgraph cluster_sdk {
    label="基线：SDK enet 驱动（第 13 章，935 Mbit/s 下行）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#1F6FEB";
    s_lwip  [label="lwIP 协议栈\npbuf (PBUF_RAM)", fillcolor="#DDEBFF"];
    s_desc  [label="DMA 描述符\nbuffer1 = pbuf->payload\n（零拷贝 TX）", fillcolor="#DDEBFF"];
    s_dma   [label="ENET0 DMA"];
    s_phy   [label="YT8011AN\n1000BASE-T1"];
    s_lwip -> s_desc [label="只改描述符"];
    s_desc -> s_dma;
    s_dma -> s_phy;
  }

  subgraph cluster_mcal {
    label="本例：AUTOSAR MCAL Eth（只经 Eth.h 公共 API）";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#D29922";
    m_lwip  [label="lwIP 协议栈\npbuf", fillcolor="#FFF1CC"];
    m_netif [label="app_lwip_eth.c\nnetif->linkoutput", fillcolor="#FFF1CC"];
    m_prov  [label="Eth_ProvideTxBuffer()\n返回驱动内 1536 B 缓冲", fillcolor="#FFF1CC"];
    m_copy  [label="pbuf_copy_partial()\n整帧 memcpy（多一次拷贝）", fillcolor="#FFE3B3"];
    m_dma   [label="ENET0 DMA"];
    m_phy   [label="YT8011AN\n1000BASE-T1"];
    m_lwip -> m_netif;
    m_netif -> m_prov;
    m_prov -> m_copy;
    m_copy -> m_dma [label="Eth_Transmit()\nPhysAddrPtr=NULL"];
    m_dma -> m_phy;
  }

  subgraph cluster_rx {
    label="接收方向：两种后端都零拷贝，差别只在 pbuf 释放时把缓冲还给谁";
    fontname="Noto Sans CJK SC"; fontsize=10; color="#2EA043";
    r_phy  [label="PHY → DMA 环\n16 × 1536 B"];
    r_get  [label="Eth_GetRxBuffer()\n返回环内缓冲指针"];
    r_pbuf [label="custom pbuf\n(pbuf_alloced_custom)", fillcolor="#DDF4E0"];
    r_stk  [label="lwIP 处理\n（不搬数据）", fillcolor="#DDF4E0"];
    r_free [label="custom_free_function\n→ Eth_ReleaseRxBuffer()"];
    r_phy -> r_get -> r_pbuf -> r_stk;
    r_stk -> r_free [style=dashed, label="pbuf_free()"];
  }

  {rank=same; s_lwip; s_desc; s_dma; s_phy;}
  {rank=same; m_lwip; m_netif; m_prov; m_copy; m_dma; m_phy;}
  {rank=same; r_phy; r_get; r_pbuf; r_stk; r_free;}
  s_lwip -> m_lwip [style=invis];
  m_lwip -> r_phy [style=invis];
}
"""
    render_dot(dot, CH, "fig-49-5-lwip-datapath")


def fig_rtos_tasks():
    """图 49-2：FreeRTOS 版的任务、优先级与数据流（数字为 configMAX_PRIORITIES=8 下的实际值）。"""
    dot = r"""
digraph rtos {
  rankdir=TB;
  graph [fontname="Noto Sans CJK SC", fontsize=11, nodesep=0.26, ranksep=0.34, bgcolor="white"];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled", fillcolor="#F6F8FA", color="#6E7781"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#24292F"];

  phy   [label="PHY / ENET0 DMA 环（16 × Rx + 8 × Tx，1536 B）", fillcolor="#DDF4E0"];
  rx    [label="ethrx  优先级 6（最高任务）\nEth_GetRxBuffer → custom pbuf → tcpip_input\n无包 64 次空转后 vTaskDelay(1)，有包连续排空", fillcolor="#DDEBFF"];
  mbox  [label="tcpip mbox（深度 32，trypost 不阻塞）"];
  tcpip [label="tcpip  优先级 5\nlwIP 核心线程：协议解析 / TCP 状态机 / linkoutput\n→ Eth_ProvideTxBuffer + Eth_Transmit", fillcolor="#DDEBFF"];
  app   [label="app  优先级 4\n初始化 + 上电自检（完成后自删）"];
  mon   [label="ethmon  优先级 2\n5 s：CPU 占用 / 栈水位 / 吞吐计数", fillcolor="#FFF1CC"];
  shell [label="shell  优先级 1\nletter-shell 轮询（10 ms）", fillcolor="#FFF1CC"];
  idle  [label="IDLE  优先级 0\n1 kHz tick hook 采样空闲率 → CPU 占用", fillcolor="#F0F0F0"];

  phy   -> rx    [label="轮询收包"];
  rx    -> mbox  [label="netif->input = tcpip_input"];
  mbox  -> tcpip;
  tcpip -> phy   [style=dashed, label="linkoutput（Tx 只在 tcpip 线程）", constraint=false];
  shell -> tcpip [style=dotted, label="eth ping / iperf（LOCK_TCPIP_CORE）"];
  app   -> tcpip [style=dotted, label="tcpip_init → netif_add"];

  {rank=same; mon; shell; idle;}
  {rank=same; tcpip; }
  app -> mon [style=invis];
}
"""
    render_dot(dot, CH, "fig-49-6-rtos-tasks")


def fig_sysarch_cost():
    """图 49-3：sys_arch 原语实测开销（480 MHz，rdcycle 测量）。

    数据来源：logs/20261102-153226-rtos_lwip_selftest.log
      SYS_ARCH_PROTECT  63.03 cyc/次
      sys_sem give+take 192.13 cyc/对
      跨任务 sem 乒乓    7807.4 cyc/round（含 2 次上下文切换）
      sys_mbox post+fetch(try) 3917.09 cyc/对
    """
    setup()
    labels = ["SYS_ARCH_PROTECT\n(关中断临界区)", "sys_sem\ngive+take", "sys_mbox\ntrypost+tryfetch",
              "跨任务 sem 乒乓\n(含 2 次切换)"]
    cyc = [63.03, 192.13, 3917.09, 7807.4]
    us = [c / 480.0 for c in cyc]
    fig, ax = plt.subplots(figsize=(6.8, 3.6))
    bars = ax.bar(labels, us, color=[COLORS["green"], COLORS["blue"], COLORS["orange"], COLORS["red"]],
                  width=0.55)
    for b, c, u in zip(bars, cyc, us):
        ax.text(b.get_x() + b.get_width() / 2, u * 1.06, "%.2f us\n(%.0f cyc)" % (u, c),
                ha="center", va="bottom", fontsize=8.5)
    ax.set_ylabel("单次/单对耗时 (us，480 MHz)")
    ax.set_ylim(0, max(us) * 1.35)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    save(fig, CH, "fig-49-7-sysarch-cost")


def main():
    fig_datapath()
    fig_rtos_tasks()
    fig_sysarch_cost()


if __name__ == "__main__":
    main()
