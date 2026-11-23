#!/usr/bin/env python3
"""tools/figs/net2_fig_13_2_zero_copy.py —— 图 13-2：ENET DMA 与 lwIP pbuf 的收发数据通路

数据来源：hpm_sdk v1.13.0 samples/lwip/common/single/common.c 的实现
          （TX：pbuf 直接挂到描述符 buffer1；RX：custom pbuf 指向 DMA 缓冲区），
          本仓库 samples/enet/enet_lwip_iperf 实测（MTU 1500、ENET_TX/RX_BUFF_COUNT=30）。
输出：docs/figures/13/fig-13-2-zero-copy.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import render_dot  # noqa: E402

DOT = r"""
digraph zero_copy {
  graph [rankdir=LR, fontname="Helvetica", nodesep=0.35, ranksep=0.55, bgcolor="white"];
  node  [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9,
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [color="#57606A", penwidth=0.9, arrowsize=0.7, fontsize=8];

  subgraph cluster_tx {
    label="发送路径（零拷贝 TX）"; fontsize=10; style="rounded"; color="#1F6FEB";
    t1 [label="lwIP 构造 pbuf\n（PBUF_POOL / PBUF_RAM）"];
    t2 [label="enet_lwip_output()\n按 1536B 分段"];
    t3 [label="TX DMA 描述符\nbuffer1 = pbuf payload 物理地址"];
    t4 [label="ENET0 MAC\n硬件 CRC + 发送"];
    t1 -> t2 -> t3 -> t4;
  }

  subgraph cluster_rx {
    label="接收路径（custom pbuf 零拷贝 RX）"; fontsize=10; style="rounded"; color="#2EA043";
    r1 [label="ENET0 MAC\n写入 rx_buff[i]（1536B×30）"];
    r2 [label="RX DMA 描述符\nowner=软件，取 frame 长度"];
    r3 [label="custom pbuf\npayload 指向 rx_buff[i]"];
    r4 [label="ethernetif_input()\n→ netif->input（tcpip/ethernet_input）"];
    r1 -> r2 -> r3 -> r4;
  }

  txbuf [label="TX 完成回收：\n轮询描述符 owner 位\n释放 pbuf（enet_lwip_tx_release）", fillcolor="#FFF8C5", color="#D29922"];
  t4 -> txbuf [style=dashed, label="发送完成"];
  cache [label="Cache 维护：\nl1c_dc_writeback（TX）\nl1c_dc_invalidate（RX）\n描述符表放 non-cacheable 区", fillcolor="#FFEBE9", color="#DA3633"];
  t3 -> cache [style=dotted];
  cache -> r2 [style=dotted];
}
"""


def main():
    render_dot(DOT, 13, "fig-13-2-zero-copy")


if __name__ == "__main__":
    main()
