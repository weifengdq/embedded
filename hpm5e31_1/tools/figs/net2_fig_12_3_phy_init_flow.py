#!/usr/bin/env python3
"""tools/figs/net2_fig_12_3_phy_init_flow.py —— 图 12-3：YT8011AN 上电/复位/配置时序流程

数据来源：
  * YT8011A_1.pdf §3（p.38）：RESET_N 低有效 ≥10 ms；复位释放后锁存 RX_DV/RXC/RXD[3:0] 的 strapping
  * 应用说明 §5.3/§5.4（p.26-27）：MMD1 0x834 配速率/主从后需软复位
  * 本仓库实测：logs/20261030-110554、-110711、-110912（详见第 12 章第 5 节）
输出：docs/figures/12/fig-12-3-phy-init-flow.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import render_dot  # noqa: E402

DOT = r"""
digraph phy_init {
  graph [rankdir=TB, fontname="Helvetica", nodesep=0.28, ranksep=0.34, bgcolor="white"];
  node  [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=10,
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9];
  edge  [color="#57606A", penwidth=0.9, arrowsize=0.7];

  start [label="上电 / 复位释放", fillcolor="#DDF4FF", color="#1F6FEB"];
  strap [label="PHY 锁存 strapping\nRXD[1:0]=01 → PHYADDR=1\nRXD3=0 → Slave, RXD2=0 → RGMII\nRXC=1 → RXC 2ns 延迟使能\nRXDV=1 → Auto 模式"];
  host  [label="MCU: net2_enet_board_init()\n1) RGMII/MDIO 引脚 IOC\n2) PB29 输出高（释放 nRST）\n3) init_eth0_clock()"];
  rst   [label="nRST 拉低 2 ms → 释放 → 等待 30 ms\n(手册要求 ≥10 ms)", fillcolor="#FFF8C5"];
  id    [label="MDIO 读 0x02/0x03 校验 ID\nID1=0x4F51, ID2[15:10]=0x3A"];
  cfg   [label="配置寄存器\nEXT 0x9001: RXC 延迟/TXC 延迟档位\nMMD1 0x834: 1000BASE-T1 + Slave"];
  srst  [label="带速率编码软复位\n1000BASE-T1 写 BMCR=0x8140\n100BASE-T1 写 BMCR=0xA100"];
  link  [label="轮询 PHYSR(0x11)\n[15:14] 速率, [13] 主从, [10] link", fillcolor="#DAFBE1", color="#2EA043"];
  ok    [label="link UP → 使能 ENET MAC/DMA 收发", fillcolor="#DAFBE1", color="#2EA043"];
  fail  [label="link DOWN\n检查对端速率/主从、线缆、RGMII 延迟", fillcolor="#FFEBE9", color="#DA3633"];

  start -> strap -> host -> rst -> id -> cfg -> srst -> link;
  link -> ok;
  link -> fail [style=dashed];
}
"""


def main():
    render_dot(DOT, 12, "fig-12-3-phy-init-flow")


if __name__ == "__main__":
    main()
