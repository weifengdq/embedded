#!/usr/bin/env python3
"""图 30-2：UDS 会话 / 安全访问状态机（含 S3 超时与 0x78 pending）

生成：python3 tools/figs/app31_uds_fsm.py
产物：docs/figures/30/fig-30-2-session-fsm.png
"""
import subprocess
DOT = r'''
digraph session {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=11, bgcolor="white", pad=0.3, nodesep=0.5, ranksep=0.6];
  node  [fontname="Noto Sans CJK SC", fontsize=10, shape=box, style="rounded,filled", fillcolor="#eef4f8"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#33506b"];

  DEF  [label="默认会话 0x01\n只读 DID / DTC\n0x27 一级可解锁", fillcolor="#e3f2fd"];
  EXT  [label="扩展会话 0x03\n0x2E 写 DID\n0x2F IO 控制\n0x28 / 0x85", fillcolor="#fff3cd"];
  PROG [label="编程会话 0x02\n0x34/0x36/0x37 刷写\n0x31 FF00/FF01", fillcolor="#ffd9a0"];
  L1   [label="安全等级 1\n(0x27 01/02)", fillcolor="#c8e6c9"];
  L2   [label="安全等级 2\n(0x27 03/04)", fillcolor="#c8e6c9"];
  LOCK [label="锁定\n(默认)", fillcolor="#ffcdd2"];
  PEND [label="0x78 pending\nP2* 续期", shape=note, fillcolor="#f8bbd0"];

  DEF  -> EXT  [label="10 03"];
  DEF  -> PROG [label="10 02"];
  EXT  -> PROG [label="10 02"];
  PROG -> EXT  [label="10 03"];
  EXT -> DEF   [label="10 01"];
  PROG -> DEF  [label="10 01"];
  DEF  -> LOCK  [label="会话切换 / S3 超时"];
  EXT  -> LOCK  [label="S3 = 5000 ms 无请求"];
  PROG -> LOCK  [label="S3 超时 / 0x11 复位"];
  LOCK -> L1    [label="27 01/02 成功"];
  L1   -> L2    [label="27 03/04 成功 (需扩展会话)"];
  L1   -> LOCK  [label="3 次错误 key\n锁 10 s"];
  EXT  -> PEND  [label="长操作 (擦除 32 KB)"];
  PROG -> PEND  [label="擦除超过 P2 = 50 ms"];
  PEND -> EXT   [label="操作完成\n发最终响应"];
}
'''
with open("/tmp/app31_fsm.dot", "w") as f:
    f.write(DOT)
subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", "/tmp/app31_fsm.dot",
                       "-o", "docs/figures/30/fig-30-2-session-fsm.png"])
print("ok docs/figures/30/fig-30-2-session-fsm.png")
