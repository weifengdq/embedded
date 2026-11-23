#!/usr/bin/env python3
"""图 29-2：ISO-TP 发送/接收状态机（本仓库 app_isotp.c 的实际状态）

生成：python3 tools/figs/app30_isotp_fsm.py
产物：docs/figures/29/fig-29-2-fsm.png
"""
import subprocess
DOT = r'''
digraph isotp {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", pad=0.3];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled", fillcolor="#eef4f8"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#33506b"];

  subgraph cluster_tx {
    label="发送方（N_As / N_Bs）"; style="rounded,dashed"; color="#7aa6c2"; fontsize=10;
    TX_IDLE   [label="TX_IDLE\n空闲"];
    TX_FF     [label="发 SF/FF\n(记录 tx_off)", fillcolor="#ffd9a0"];
    TX_WAITFC [label="TX_WAIT_FC\n等 FC (N_Bs=1000 ms)", fillcolor="#ffd9a0"];
    TX_CF     [label="TX_SEND_CF\n按 BS/STmin 发 CF", fillcolor="#ffd9a0"];
    TX_DONE   [label="TX_DONE\n统计 last_tx_us", fillcolor="#c8e6c9"];
    TX_IDLE -> TX_FF   [label="isotp_send(len>7)"];
    TX_FF   -> TX_WAITFC;
    TX_WAITFC -> TX_CF [label="FC.CTS\nBS, STmin"];
    TX_WAITFC -> TX_IDLE [label="FC.OVFLW / N_Bs 超时", color="#c62828"];
    TX_CF   -> TX_CF   [label="下一 CF\n(等 STmin)"];
    TX_CF   -> TX_WAITFC [label="发满 BS 个 CF"];
    TX_CF   -> TX_DONE [label="tx_off == len"];
    TX_DONE -> TX_IDLE;
  }

  subgraph cluster_rx {
    label="接收方（N_Ar / N_Cr）"; style="rounded,dashed"; color="#7aa6c2"; fontsize=10;
    RX_IDLE   [label="RX_IDLE\n空闲"];
    RX_FF     [label="收 SF/FF", fillcolor="#ffd9a0"];
    RX_WAITCF [label="RX_WAIT_CF\n收 CF (N_Cr=1000 ms)", fillcolor="#ffd9a0"];
    RX_FC     [label="发 FC\n(CTS/WAIT/OVFLW)", fillcolor="#c8e6c9"];
    RX_CB     [label="on_msg()\n交上层", fillcolor="#c8e6c9"];
    RX_IDLE -> RX_FF [label="on_frame()"];
    RX_FF   -> RX_CB [label="SF"];
    RX_FF   -> RX_FC [label="FF"];
    RX_FC   -> RX_WAITCF;
    RX_WAITCF -> RX_WAITCF [label="CF\n(SN 校验)"];
    RX_WAITCF -> RX_FC   [label="收满 BS 个"];
    RX_WAITCF -> RX_CB   [label="rx_got == len"];
    RX_WAITCF -> RX_IDLE [label="SN 错 / N_Cr 超时", color="#c62828"];
    RX_FC -> RX_CB [label="单帧或尾帧"];
  }
}
'''
with open("/tmp/app30_fsm.dot", "w") as f:
    f.write(DOT)
subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", "/tmp/app30_fsm.dot",
                       "-o", "docs/figures/29/fig-29-2-fsm.png"])
print("ok docs/figures/29/fig-29-2-fsm.png")
