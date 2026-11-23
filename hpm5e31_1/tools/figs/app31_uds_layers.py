#!/usr/bin/env python3
"""图 30-1：UDS 服务端分层与后端（本仓库 components/app_uds 的实际结构）

生成：python3 tools/figs/app31_uds_layers.py
产物：docs/figures/30/fig-30-1-layers.png
"""
import subprocess
DOT = r'''
digraph uds {
  rankdir=TB;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", pad=0.3, nodesep=0.35, ranksep=0.4];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled", fillcolor="#eef4f8", margin=0.12];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.5, color="#33506b"];

  subgraph cluster_svc {
    label="服务层 app_uds.c（0x10/11/14/19/22/27/28/2E/2F/31/34/36/37/3E/85）";
    style="rounded,filled"; fillcolor="#f7fbff"; color="#7aa6c2";
    disp [label="app_uds_handle()\nSID 分发 + 会话/安全门控\nNRC 0x11/12/13/22/24/31/33/35/36/37/7E/7F/78"];
    sess [label="会话 & 安全状态机\nP2=50 ms  P2*=5000 ms  S3=5000 ms", fillcolor="#ffd9a0"];
    seed [label="0x27 种子-密钥\n3 次错误锁 10 s", fillcolor="#ffd9a0"];
  }

  subgraph cluster_back {
    label="后端（可替换，全部通过函数表/结构体注入）";
    style="rounded,filled"; fillcolor="#f7fff7"; color="#7ac27a";
    did   [label="DID 表\nF190 VIN / F195 软件版本\nF187 零件号 / F18C 序列号\nF191 硬件版本 / F010 输出控制"];
    dtc   [label="DTC 内存\n状态位 bit0..bit7\n快照 3 DID + 扩展记录 02"];
    rtn   [label="例程表\n0x0203 自检\n0x0301 慢例程 (0x78)\n0xFF00 CRC / 0xFF01 依赖"];
    flash [label="刷写后端 app_uds_flash_driver_t\nbegin/write/finish/crc32/max_block\n片内 XPI NOR (最后 64 KB)"];
    obd   [label="OBD 钩子\n0x01/0x09（可选）", style="dashed"];
  }

  subgraph cluster_tp {
    label="传输层与驱动";
    style="rounded,filled"; fillcolor="#fff7f7"; color="#c27a7a";
    isotp [label="ISO-TP (第 29 章)\nSF/FF/CF/FC  BS/STmin  4 KB 消息"];
    mcan  [label="MCAN (CAN0 0x7E0/0x7E8)\n经典 500 kbit/s"];
  }

  disp -> sess [style=dashed, label="每个请求"];
  disp -> seed [style=dashed];
  disp -> did   [label="0x22 / 0x2E / 0x2F"];
  disp -> dtc   [label="0x14 / 0x19"];
  disp -> rtn   [label="0x31"];
  disp -> flash [label="0x34 / 0x36 / 0x37"];
  disp -> obd   [label="0x01 / 0x09"];
  disp -> isotp [label="响应 PDU"];
  isotp -> mcan [label="帧"];
}
'''
with open("/tmp/app31_layers.dot", "w") as f:
    f.write(DOT)
subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", "/tmp/app31_layers.dot",
                       "-o", "docs/figures/30/fig-30-1-layers.png"])
print("ok docs/figures/30/fig-30-1-layers.png")
