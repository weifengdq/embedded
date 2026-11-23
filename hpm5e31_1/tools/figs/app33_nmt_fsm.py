#!/usr/bin/env python3
"""tools/figs/app33_nmt_fsm.py —— 图 33-2：CANopen NMT 状态机（CiA 301）

输出：docs/figures/33/fig-33-2-nmt-fsm.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import render_dot  # noqa: E402

DOT = r'''
digraph nmt_fsm {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=10, nodesep=0.55, ranksep=0.75, bgcolor="white"];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10,
         fillcolor="#F6F8FA", color="#6E7781", penwidth=1.1, width=1.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.5, color="#57606A", arrowsize=0.7];

  init   [label="Initialising\n上电 / NMT Reset App/Comm\n发 boot-up 0x700+ID = 0x00", fillcolor="#FFF8C5"];
  preop  [label="Pre-operational\n(127)\n可 SDO/EMCY/心跳/SYNC\n不收发 PDO", fillcolor="#DDF4FF"];
  op     [label="Operational\n(5)\nSDO/PDO/EMCY/心跳全开", fillcolor="#DAFBE1"];
  stop   [label="Stopped\n(4)\n只响应 NMT/心跳\nSDO/PDO/EMCY 关闭", fillcolor="#FFEBE9"];

  init  -> preop [label="初始化完成\n（自动）"];
  preop -> op    [label="NMT Start\n(0x01, nodeID)"];
  op    -> preop [label="NMT Pre-op\n(0x80, nodeID)"];
  op    -> stop  [label="NMT Stop\n(0x02, nodeID)"];
  preop -> stop  [label="NMT Stop\n(0x02, nodeID)"];
  stop  -> preop [label="NMT Pre-op\n(0x80, nodeID)"];
  stop  -> op    [label="NMT Start\n(0x01, nodeID)"];

  init  -> init  [label="NMT Reset Communication (0x82)\n重新初始化通信对象"];
  op    -> init  [label="NMT Reset App (0x81)\n= MCU 重启", style=dashed];
  preop -> init  [label="NMT Reset App (0x81)\n= MCU 重启", style=dashed];
  stop  -> init  [label="NMT Reset App (0x81)\n= MCU 重启", style=dashed];
}
'''


def main():
    render_dot(DOT, 34, "fig-33-2-nmt-fsm")
    print("generated docs/figures/33/fig-33-2-nmt-fsm.png")


if __name__ == "__main__":
    main()
