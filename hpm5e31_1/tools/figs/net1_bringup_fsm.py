#!/usr/bin/env python3
"""net1_bringup_fsm.py —— 图 10-x: LAN8651 上电/复位后的启动与配置状态机

依据: LAN8650/1 DS60001734E §4.3 "Startup Sequence" + Figure 4-1;
      §4.3.2 External Pin Reset (RESET_N), Table 9-8 (t_rstia >= 5 us)。
本图同时标出本仓库实测到的状态（见 docs/chapters/10-lan8651-t1s.md 第 7 节）。
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _common import render_dot  # noqa: E402

DOT = r"""
digraph bringup {
  rankdir=TB;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", nodesep=0.32, ranksep=0.42];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10,
         color="#57606A", fillcolor="#F6F8FA", penwidth=0.9];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  por    [label="上电 / POR\n(所有电源越过阈值)", fillcolor="#E7EEF8"];
  rstn   [label="RESET_N 拉低 >= 5 us\n(Table 9-8 t_rstia)"];
  rel    [label="RESET_N 释放", fillcolor="#E7EEF8"];
  state  [label="LAN8650/1 内部动作:\n• STATUS0.RESETC = 1\n• CONFIG0.SYNC = 0、数据尾 SYNC = 0\n• IRQ_N 拉低(实低有效)"];

  read   [label="主机读 OA_STATUS0 (0x0008)\n本仓库: 必须先打通 SPI 控制事务", fillcolor="#FFF8E6"];
  cfg    [label="主机配置:\n• PHY AN1760 参数(MMD31)\n• MAC 地址/MAC_NET_CTL\n• PLCA 参数(0xCA00~0xCA05)"];
  sync   [label="写 CONFIG0.SYNC = 1\n写 STATUS0.RESETC = 1 清除"];
  run    [label="IRQ_N 释放; 数据尾 SYNC = 1\n可以进行以太网帧收发", fillcolor="#E6F4EA"];

  por -> rstn -> rel -> state -> read -> cfg -> sync -> run;

  meas   [shape=note, label="本仓库实测(2026-10-30):\nRESET_N=0 时 IRQ_N=1、SDO=高阻\nRESET_N=1 后 IRQ_N=0、CS 有效时 SDO 被驱动\n=> 复位/中断/SDO 链路正确;\n控制事务回显仍为全 0(见 10.9 节坑 2)", fillcolor="#FFEBE9", color="#DA3633"];
  state -> meas [style=dashed, arrowhead=none, color="#DA3633"];
}
"""


def main():
    render_dot(DOT, 10, "fig-10-3-bringup-fsm")


if __name__ == "__main__":
    main()
