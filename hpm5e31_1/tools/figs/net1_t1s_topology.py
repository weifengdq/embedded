#!/usr/bin/env python3
"""net1_t1s_topology.py —— 图 10-x: hpm5e31_1 + LAN8651 的 10BASE-T1S 多点混合段拓扑

依据: IEEE 802.3cg (10BASE-T1S, 多点混合段 P2MP, 最多 8 节点/25 m);
      LAN8650/1 DS60001734E §1/§3 (MAC-PHY 结构); 本板原理图 hpm5e31_1_sch.pdf。
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _common import render_dot  # noqa: E402

DOT = r"""
digraph t1s {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", nodesep=0.35, ranksep=0.55];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10,
         color="#57606A", fillcolor="#F6F8FA", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  subgraph cluster_pc {
    label="主机 (Ubuntu)"; style="rounded,dashed"; color="#8C959F"; fontsize=10;
    host [label="iperf3 / ping / python raw socket\n192.168.1.1/24", fillcolor="#E7EEF8"];
    usb  [label="USB-10BASE-T1S 适配器\nenx001ec0d1c337 (K2L 184f:0051)", fillcolor="#E7EEF8"];
  }

  subgraph cluster_seg {
    label="10BASE-T1S 多点混合段 (P2MP, 单对双绞线, 终端 100 Ω)"; style="rounded"; color="#2EA043"; fontsize=10;
    seg [shape=box, label="TRX_P / TRX_N\nPLCA: 节点 0 = 协调者(发 BEACON)", fillcolor="#E6F4EA", width=3.2];
  }

  subgraph cluster_board {
    label="hpm5e31_1"; style="rounded,dashed"; color="#8C959F"; fontsize=10;
    mcu  [label="HPM5E31IPB1\nRV32 @480 MHz", fillcolor="#FFF8E6"];
    phy  [label="LAN8651 (MAC-PHY)\nSPI client + 内部 PHY", fillcolor="#FFF8E6"];
  }

  host -> usb [label="TCP/IP"];
  usb  -> seg [label="10BASE-T1S\n10 Mbps 半双工"];
  seg  -> phy [label="10BASE-T1S"];
  phy  -> mcu [label="SPI2 (OPEN Alliance TC6)\nPA20/21/22/23 + nINT PA24 + nRST PA19", dir=both];
}
"""


def main():
    render_dot(DOT, 10, "fig-10-1-t1s-topology")


if __name__ == "__main__":
    main()
