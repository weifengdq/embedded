#!/usr/bin/env python3
"""fig-40-3: 应用层协议选型决策树（graphviz）"""
import subprocess, pathlib
out = pathlib.Path(__file__).resolve().parents[2] / "docs/figures/39"
out.mkdir(parents=True, exist_ok=True)
dot = r"""
digraph selection {
  graph [rankdir=TB, fontname="Helvetica", nodesep=0.22, ranksep=0.30, bgcolor="white"];
  node  [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, fillcolor="#eef4fb", color="#4a78a8"];
  edge  [color="#6b8fb3", arrowsize=0.7, fontsize=8];

  start [label="要做什么？", shape=diamond, fillcolor="#fff3d6"];
  diag  [label="诊断/刷写/故障码"];
  net   [label="节点互操作 / 商用车网络"];
  cal   [label="标定与在线测量"];
  soa   [label="面向服务通信 / 机器人"];
  time  [label="多节点时间同步"];
  log   [label="日志与信号采集"];
  tunnel[label="上位机看真实 CAN 总线"];

  uds   [label="UDS(ISO 14229)\n+ ISO-TP(15765-2)\n第 29/30 章", fillcolor="#dff0df"];
  doip  [label="（以太网场景可换 DoIP）", fillcolor="#f7f7f7"];
  j1939 [label="J1939\n第 32 章", fillcolor="#dff0df"];
  co    [label="CANopen\n第 33 章", fillcolor="#dff0df"];
  xcp   [label="XCP (+A2L)\n第 34 章", fillcolor="#dff0df"];
  sip   [label="SOME/IP (+SD/TP)\n第 35 章", fillcolor="#dff0df"];
  xrce  [label="XRCE-DDS / ROS 2\n第 36 章", fillcolor="#dff0df"];
  ptp   [label="PTP / gPTP\n第 38 章", fillcolor="#dff0df"];
  dlt   [label="DLT / VSS\n第 39 章", fillcolor="#dff0df"];
  cann  [label="cannelloni 网关\n第 28 章", fillcolor="#dff0df"];

  start -> diag; start -> net; start -> cal; start -> soa; start -> time; start -> log; start -> tunnel;
  diag -> uds; diag -> doip [style=dashed];
  net -> j1939 [label="商用车"];
  net -> co [label="工业/汽车子系统"];
  cal -> xcp;
  soa -> sip [label="车载 SOA"];
  soa -> xrce [label="ROS 2 / DDS"];
  time -> ptp;
  log -> dlt;
  tunnel -> cann;
}
"""
p = out / "fig-40-3-selection.dot"
p.write_text(dot)
subprocess.run(["dot", "-Tpng", "-Gdpi=200", str(p), "-o", str(out / "fig-40-3-selection.png")], check=True)
print(out / "fig-40-3-selection.png")
