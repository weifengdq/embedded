#!/usr/bin/env python3
"""图 41-1：gs_usb 适配器的分层与数据通路

生成：python3 tools/figs/app40_pipeline.py
产物：docs/figures/41/fig-41-1-pipeline.png
"""
import subprocess
DOT = r'''
digraph gsusb {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", pad=0.3, nodesep=0.30, ranksep=0.45];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled", fillcolor="#eef4f8", margin=0.10];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.5, color="#33506b"];

  subgraph cluster_host {
    label="主机（Linux 7.0.0，gs_usb.ko 已加载）"; style="rounded,dashed"; color="#7aa6c2";
    app   [label="SocketCAN 工具\ncandump / cansend / cangen\npython-can / CANopen / UDS", fillcolor="#e3f2fd"];
    netdev[label="netdev can0..can3\n(ip link / SO_TIMESTAMPING)", fillcolor="#e3f2fd"];
    ko    [label="gs_usb 驱动\n控制请求 + host_frame 编解码\nRX offload / 时间戳补偿", fillcolor="#fff3cd"];
  }

  subgraph cluster_dev {
    label="hpm5e31_1（HPM5E31 + CherryUSB 1.6.1）"; style="rounded,dashed"; color="#c27a7a";
    usb   [label="USB0 设备口\nVendor Class 0xFF\nEP 0x81 IN / 0x02 OUT, 512 B, HS", fillcolor="#ffe0e0"];
    proto [label="gs_usb 协议层\n控制请求分发\nhost_frame 打包/解包\n1 MHz 时间戳", fillcolor="#ffd9a0"];
    ring  [label="TX/RX 环形队列\nbackpressure + echo", fillcolor="#ffd9a0"];
    mcan  [label="MCAN0..3\n经典 500k / FD 1M-2M\nTDC 内部计算", fillcolor="#c8e6c9"];
  }

  bus [label="CAN 总线\nCAN0↔CAN1 / CAN2↔CAN3（120 Ω）", shape=cylinder, fillcolor="#f1f8e9"];

  app -> netdev [label="socketcan 帧"];
  netdev -> ko [label="can_frame/canfd_frame"];
  ko -> usb [label="Bulk OUT\n20 B / 76 B", dir=both];
  usb -> proto [label="控制请求 + 帧"];
  proto -> ring [label="入队"];
  ring -> mcan [label="MCAN TX FIFO"];
  mcan -> ring [label="RX FIFO + 时基"];
  ring -> proto [label="出队"];
  proto -> usb [label="Bulk IN\n24 B / 80 B"];
  mcan -> bus [label="CAN_H/L", dir=both];
  ko -> netdev [label="含 32 位 us 时间戳"];
}
'''
with open("/tmp/app40_pipeline.dot", "w") as f:
    f.write(DOT)
subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", "/tmp/app40_pipeline.dot",
                       "-o", "docs/figures/41/fig-41-1-pipeline.png"])
print("ok docs/figures/41/fig-41-1-pipeline.png")
