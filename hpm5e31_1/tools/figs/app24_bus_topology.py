#!/usr/bin/env python3
"""图 24-1：HPM5E31 存储/总线拓扑示意（由本仓实测的地址与时钟绘制）

生成：python3 tools/figs/app24_bus_topology.py
产物：docs/figures/24/fig-24-1-bus-topology.png

说明：本图是**示意图**（结构关系），图中标注的吞吐数字来自本仓实测（来源见第 23/24 章表格与 logs/）；
"Axi 200 MHz / 32 bit -> 理论 800 MB/s" 为按频率与位宽推算的理论值。
"""
import os
import subprocess

DOT = r'''
digraph bus {
  rankdir=TB;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", pad=0.30, nodesep=0.30, ranksep=0.42];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled", fillcolor="#eaf2fb", color="#2b6cb0", margin=0.12];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.5, color="#33506b"];

  cpu [label="CPU0  RV32IMAC  480 MHz\nL1 I-Cache / D-Cache", fillcolor="#dbeafe"];

  subgraph cluster_local {
    label="紧耦合本地存储（最快）"; style="rounded,dashed"; color="#2f855a"; fontsize=9.5;
    ilm [label="ILM 128 KB  @0x0000_0000\n.vectors / .fast（中断入口）", fillcolor="#e6fffa"];
    dlm [label="DLM 128 KB  @0x0020_0000\n栈 / 堆 / 热数据", fillcolor="#e6fffa"];
  }

  subgraph cluster_axi {
    label="AXI 总线矩阵  200 MHz / 32 bit（理论 800 MB/s）"; style="rounded,dashed"; color="#b7791f"; fontsize=9.5;
    sram [label="AXI SRAM 256 KB  @0x0120_0000\n.data / .bss（可缓存）\n实测 memset 959.6 MB/s、copy 922.5 MB/s", fillcolor="#fffaf0"];
    xpi  [label="XPI0  100 MHz", fillcolor="#fffaf0"];
    dma  [label="XDMA / HDMA\nXDMA mem2mem 406 MB/s（链式 485）\nHDMA 327.7 MB/s", fillcolor="#fffaf0"];
  }

  flash [label="片内 Flash 1 MB  @0x8000_0000\nXIP 执行：顺序读实测 40 MB/s（本仓）\n布局：nor_cfg@0x400 / boot_header@0x1000 / 应用@0x3000", shape=cylinder, fillcolor="#f0fff4"];
  periph [label="外设：MCAN0..3 / ENET(RGMII) / USB0\nUART0 / SPI0..2 / I2C / ADC16 / GPTMR", fillcolor="#f7fafc"];

  cpu -> ilm [label="取指/常数"];
  cpu -> dlm [label="数据/栈"];
  cpu -> sram [label="AXI 读 / 写"];
  cpu -> xpi [label="取指 / rodata"];
  cpu -> dma [label="搬运"];

  xpi -> flash [label="XIP 读"];
  dma -> sram [label="mem2mem", style=dashed];
  dma -> periph [label="外设搬运", style=dashed];
  sram -> periph [label="寄存器 / FIFO", style=dashed];

  note [label="示意见图；数字为实测（见第 23/24 章表格与 logs/），\n只有 AXI 800 MB/s 是按 200 MHz×32 bit 推算的理论值", shape=note, fillcolor="#fffbea", color="#975a16"];
  flash -> note [style=invis];
}
'''

with open("/tmp/app24_bus.dot", "w") as f:
    f.write(DOT)
os.makedirs("docs/figures/24", exist_ok=True)
subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", "/tmp/app24_bus.dot",
                       "-o", "docs/figures/24/fig-24-1-bus-topology.png"])
print("ok docs/figures/24/fig-24-1-bus-topology.png")
