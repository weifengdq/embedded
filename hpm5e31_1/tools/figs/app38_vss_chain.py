#!/usr/bin/env python3
"""tools/figs/app38_vss_chain.py —— 图 39-2：VSS 信号链路（CAN 帧 -> VSS 路径 -> 消费者）

输出：docs/figures/39/fig-39-2-vss-chain.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from _common import render_dot  # noqa: E402

DOT = r'''
digraph vss_chain {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=10, nodesep=0.3, ranksep=0.5, bgcolor="white"];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=9,
         fillcolor="#F6F8FA", color="#6E7781", width=2.0];
  edge  [fontname="Noto Sans CJK SC", fontsize=8, color="#57606A", arrowsize=0.7];

  board [label="板端 samples/app/vss_signals\n0x400 车速/转速\n0x401 水温/挡位/车门\n0x402 SOC", fillcolor="#DDF4FF"];
  gw    [label="app_can_gw\nCAN1 <-> UDP 4000", fillcolor="#FFF8C5"];
  vcan  [label="主机 vcan0\n(SocketCAN)", fillcolor="#DAFBE1"];
  dec   [label="host/vss/vss_decode.py\n位域提取 + factor/offset\nVSS 路径校验", fillcolor="#DAFBE1"];
  tree  [label="COVESA VSS 树\nVehicle.Speed\nVehicle.Powertrain.*\nVehicle.Cabin.*", fillcolor="#FFEBE9"];
  app   [label="消费者\n仪表/云/ROS2", shape=box3d, fillcolor="#FFFFFF"];

  board -> gw  [label="CAN0 帧"];
  gw    -> vcan [label="cannelloni v2"];
  vcan  -> dec  [label="candump / recv"];
  dec   -> tree [label="路径匹配"];
  tree  -> app;
}
'''


def main():
    render_dot(DOT, 40, "fig-39-2-vss-chain")
    print("generated docs/figures/39/fig-39-2-vss-chain.png")


if __name__ == "__main__":
    main()
