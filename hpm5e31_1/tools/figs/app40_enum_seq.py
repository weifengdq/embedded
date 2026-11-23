#!/usr/bin/env python3
"""图 41-3：枚举与启动时序（控制请求顺序）

生成：python3 tools/figs/app40_enum_seq.py
产物：docs/figures/41/fig-41-3-enum-seq.png
"""
import subprocess
DOT = r'''
digraph seq {
  rankdir=TB;
  graph [fontname="Noto Sans CJK SC", fontsize=10.5, bgcolor="white", pad=0.3, nodesep=0.35, ranksep=0.28];
  node  [fontname="Noto Sans CJK SC", fontsize=9, shape=box, style="rounded,filled", fillcolor="#eef4f8", margin=0.10];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.2, color="#33506b"];

  usb   [label="① USB 枚举\nVID 0x1209 / PID 0x2323\nVendor Class 0xFF，1 IN + 1 OUT，512 B", fillcolor="#e3f2fd"];
  cfg   [label="② DEVICE_CONFIG (bReq=5, IN)\n{reserved×3, icount=3, sw_ver, hw_ver}\n→ 内核创建 4 个 netdev", fillcolor="#fff3cd"];
  btc   [label="③ BT_CONST (4) / BT_CONST_EXT (11)\nfeature 位 + fclk_can + tseg/brp 范围\n→ 内核据此算位时序", fillcolor="#fff3cd"];
  hfmt  [label="④ HOST_FORMAT (0, OUT)\nbyte_order = 0x0000BEEF", fillcolor="#ffd9a0"];
  ipup  [label="⑤ ip link set canX up\n→ BITTIMING (1) + MODE(2){mode=START, flags}\nFD 时再发 DATA_BITTIMING (10)", fillcolor="#ffd9a0"];
  ts    [label="⑥ TIMESTAMP (6, IN)\n{ts_low, ts_high, flags}\n用于 32 位时间戳溢出补偿（约 71 min 一次）", fillcolor="#c8e6c9"];
  data  [label="⑦ 数据帧流\n主机 Bulk OUT 20/76 B → 设备\n设备 Bulk IN 24/80 B → 主机\nTX 帧成功后回显 echo_id", fillcolor="#c8e6c9"];
  state [label="⑧ GET_STATE (14, IN)\n{state, rxerr, txerr}\nip -details link show", fillcolor="#f8bbd0"];

  usb -> cfg -> btc -> hfmt -> ipup -> data;
  ipup -> ts [style=dashed, label="每 ~1.8 ks"];
  data -> state [style=dashed, label="按需轮询"];
}
'''
with open("/tmp/app40_seq.dot", "w") as f:
    f.write(DOT)
subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", "/tmp/app40_seq.dot",
                       "-o", "docs/figures/41/fig-41-3-enum-seq.png"])
print("ok docs/figures/41/fig-41-3-enum-seq.png")
