#!/usr/bin/env python3
"""R5: 长稳/可靠性测试框架（第 25 章）。graphviz。"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import render_dot

dot = r"""
digraph soak {
  graph [rankdir=LR, bgcolor="white", nodesep=0.30, ranksep=0.55, fontname="Noto Sans CJK SC"];
  node  [shape=box, style="rounded,filled", fillcolor="#F6F8FA", color="#57606A",
         fontname="Noto Sans CJK SC", fontsize=10, margin="0.10,0.06"];
  edge  [color="#57606A", fontname="Noto Sans CJK SC", fontsize=8.5, arrowsize=0.7];

  subgraph cluster_stress {
    label="持续负载（可组合）"; fontsize=10; color="#D0D7DE"; style="rounded,dashed";
    cpu    [label="CPU：CoreMark 循环"];
    can    [label="CAN：4 路收发\n500k/2M FD"];
    net    [label="以太网：iperf 打流\n+ 周期 ping"];
    flash  [label="Flash：擦写/参数存取"];
    wdg    [label="看门狗：周期喂狗\n+ 注入死循环"];
    pwr    [label="电源/复位：\n随机掉电（可选）"];
  }

  subgraph cluster_mon {
    label="指标采集"; fontsize=10; color="#D0D7DE"; style="rounded,dashed";
    m1 [label="mcycle / MCHTMR\n抖动与周期"];
    m2 [label="CAN ECR：TEC/REC/LEC\nbus-off 次数"];
    m3 [label="网络：丢包/重传\nRTT 分位数"];
    m4 [label="内存：栈涂色水位\n堆剩余 / 句柄"];
    m5 [label="串口日志\n+ 心跳计数"];
  }

  judge [label="判定\n（阈值 + 人工复核）", fillcolor="#FFF8C5", color="#D29922"];
  keep  [label="记录到 logs/\n与第 25 章表格", fillcolor="#DAFBE1", color="#2EA043"];
  fix   [label="定位 → 修复 → 复跑", fillcolor="#FFEBE9", color="#DA3633"];

  cpu -> m1; can -> m2; net -> m3; flash -> m1; wdg -> m5; pwr -> m5;
  m1 -> judge; m2 -> judge; m3 -> judge; m4 -> judge; m5 -> judge;
  judge -> keep [label="通过"];
  judge -> fix  [label="异常"];
  fix -> cpu [style=dashed, label="回归"];
}
"""

render_dot(dot, 25, "fig-25-1-soak-framework")
