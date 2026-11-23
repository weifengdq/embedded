#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第 50 章 图 50-1：AUTOSAR CP 分层与 MCAL 边界（graphviz dot）。

输出 docs/figures/50-mcal-compliance/fig-50-1-autosar-layers.png
"""
from _common import render_dot

DOT = r"""
digraph autosar_layers {
  rankdir=TB;
  fontname="Noto Sans CJK SC";
  fontsize=11;
  nodesep=0.22;
  ranksep=0.8;
  compound=true;
  splines=spline;
  node [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10,
        penwidth=1.1, color="#57606A", fillcolor="#F6F8FA", margin="0.12,0.07"];
  edge [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A", arrowsize=0.7];

  subgraph cluster_app {
    label="应用层（Application Layer）"; labeljust=left;
    style="rounded,filled"; fillcolor="#E8F0FE"; color="#1F6FEB"; fontcolor="#1F6FEB";
    swc1 [label="SWC：车身 / 电机 / 通信"];
    swc2 [label="SWC：诊断 / 标定"];
  }

  rte [label="RTE（Runtime Environment）\nRte_Read / Rte_Write / Rte_Call", fillcolor="#EAF7EA",
       color="#2EA043", style="rounded,filled"];

  subgraph cluster_bsw {
    label="BSW（Basic Software）"; labeljust=left;
    style="rounded,filled"; fillcolor="#FFF4E5"; color="#D29922"; fontcolor="#D29922";
    services [label="服务层 Services\nDem · Det · EcuM · ComM · NvM"];
    ecuab    [label="ECU 抽象层 EcuAbstraction\nIoHwAb · MemIf · CanIf · PduR\n+ CDD 复杂驱动"];
    os       [label="OS\n任务 / ISR / 计数器\nCategory 0/1/2"];
    services -> ecuab;
    services -> os [style=dotted, label="调度"];
  }

  subgraph cluster_mcal {
    label="MCAL（Microcontroller Abstraction Layer）—— 本实现 13 个模块"; labeljust=left;
    style="rounded,filled"; fillcolor="#F3EDFF"; color="#8957E5"; fontcolor="#8957E5";
    io    [label="I/O 驱动\nAdc 123 · Dio 120\nIcu 122 · Pwm 121 · Port 124"];
    micro [label="微控制器驱动\nGpt 100 · Mcu 101\nWdg 102"];
    comm  [label="通信驱动\nCan 080 · I2c 113\nSpi 083"];
    mem   [label="存储器驱动\nMem 091（原 Fls）"];
    lib   [label="库\nCrc 201"];
  }

  subgraph cluster_hw {
    label="HPM5E31 硬件（RISC-V RV32，hpm5e31_1 板）"; labeljust=left;
    style="rounded,filled"; fillcolor="#FDE8E8"; color="#DA3633"; fontcolor="#DA3633";
    hw1 [label="IOC / GPIO\nGPTMR0~3 · PWMv2"];
    hw2 [label="ADC16 · SPI1/3\nI2C2/3 · MCAN0~3"];
    hw3 [label="EWDG0 · XPI NOR Flash\nHPM CRC 外设"];
  }

  swc1 -> rte [style=dashed, xlabel="端口"];
  swc2 -> rte [style=dashed];
  rte -> services;
  ecuab -> lib;
  ecuab -> mem;
  ecuab -> comm;
  ecuab -> micro;
  ecuab -> io [xlabel="MCAL API"];
  os -> io [style=dotted, xlabel="ISR"];
  io -> hw1 [xlabel="寄存器"];
  comm -> hw2;
  micro -> hw1;
  micro -> hw3;
  mem -> hw3;
  lib -> hw3;
}
"""

if __name__ == "__main__":
    render_dot(DOT, "50-mcal-compliance", "fig-50-1-autosar-layers")