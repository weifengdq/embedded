#!/usr/bin/env python3
"""net1_sw_stack.py —— 图 11-x: 从 lwIP 到 LAN8651 的软件栈与数据通路

对应本仓库实现: components/oa_tc6/ + samples/lan8651/（见第 10/11 章）。
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _common import render_dot  # noqa: E402

DOT = r"""
digraph stack {
  rankdir=TB;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", nodesep=0.28, ranksep=0.30];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=9.5,
         color="#57606A", fillcolor="#F6F8FA", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.5, color="#57606A", arrowsize=0.7];

  app  [label="应用 / 测试: ping, iperf3, 自定义 raw frame", fillcolor="#E7EEF8"];
  lwip [label="lwIP 2.x (SDK middleware/lwip)\nARP / IP / ICMP / UDP / TCP", fillcolor="#E6F4EA"];
  netif[label="netif 驱动 (oa_tc6_netif.c)\nlow_level_output / low_level_init / 轮询输入", fillcolor="#FFF8E6"];
  tc6  [label="OPEN Alliance TC6 层 (oa_tc6.c)\n控制事务 + 68 B 数据 chunk + TX credits + RX footer", fillcolor="#FFF8E6"];
  mac  [label="LAN865x MAC/PHY 配置 (lan865x.c)\nMAC 地址 / MAC_NET_CTL / AN1760 / PLCA(TC14)", fillcolor="#FFF8E6"];
  spi  [label="传输层: SPI2 控制器 (或 GPIO 位翻转 fallback)\nMode 0, 8 bit, MSB first, 2~15 MHz", fillcolor="#F3E8FD"];
  chip [label="LAN8651 (10BASE-T1S MAC-PHY)\n内部 PHY + PLCA + TRX 差分驱动", fillcolor="#FFEBE9"];

  app -> lwip -> netif -> tc6 -> spi -> chip;
  mac -> tc6 [style=dashed, label="配置"];
  chip -> mac [style=dashed, label="PHY MMD31 / TC14 寄存器", constraint=false];
}
"""


def main():
    render_dot(DOT, 11, "fig-11-2-software-stack")


if __name__ == "__main__":
    main()
