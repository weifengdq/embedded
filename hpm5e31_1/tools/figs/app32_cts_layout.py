
#!/usr/bin/env python3
"""图 32-5：TP.CM CTS 字段布局对照（本项目 vs J1939-21/Linux can-j1939）

数据来源（逐字节，不是示意）：
  Linux v6.12 net/can/j1939/transport.c
    j1939_session_tx_cts()  911-949 行：dat[0]=0x11 dat[1]=len dat[2]=pkt(rx+1) 其余 0xFF
    j1939_xtp_do_tx_ctl()   约 660-668 行：统一把 PGN 写进 byte5..7
  本项目 components/app_j1939/src/j1939_tp.c cm_send() 64~79 行：
    data[0]=ctrl data[1..2]=total data[3]=npackets data[4]=window data[5..7]=pgn

生成：python3 tools/figs/app32_cts_layout.py
产物：docs/figures/32/fig-32-5-cts-layout.png
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup  # noqa: E402

import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

setup()

ROWS = [
    ("J1939-21 / 内核 can-j1939",
     [("0x11", "#c62828"), ("允许包数", "#1565c0"), ("下一包号\n(低 8 位)", "#2e7d32"),
      ("下一包号\n中/高字节", "#90a4ae"), ("PGN\n(b5..b7)", "#6a1b9a"), ("PGN", "#6a1b9a"), ("PGN", "#6a1b9a")]),
    ("本项目 cm_send(CTS)",
     [("0x11", "#c62828"), ("总长度\n低字节", "#1565c0"), ("总长度\n高字节", "#1565c0"),
      ("总包数", "#2e7d32"), ("窗口", "#ef6c00"), ("PGN", "#6a1b9a"), ("PGN", "#6a1b9a")]),
]

fig, ax = plt.subplots(figsize=(11.2, 3.4))
ncol = 8
row_h = 0.62
for r, (name, cells) in enumerate(ROWS):
    y = len(ROWS) - 1 - r
    for c in range(ncol):
        label, color = cells[c] if c < len(cells) else ("", "#ffffff")
        ax.add_patch(Rectangle((c, y - row_h / 2), 1, row_h, facecolor=color, alpha=0.28,
                               edgecolor="#57606A", lw=1.0))
        ax.text(c + 0.5, y, label, ha="center", va="center", fontsize=8)
    ax.text(-0.15, y, name, ha="right", va="center", fontsize=9)
for c in range(ncol):
    ax.text(c + 0.5, len(ROWS) - 0.05, "byte%d" % c, ha="center", va="bottom", fontsize=8,
            color="#57606A")
ax.text(0.5, -0.55,
        "红：控制字  蓝：本项目当总长度、内核当允许包数  绿：序号/总包数  橙：窗口  紫：PGN",
        ha="left", va="center", fontsize=8, color="#24292F")
ax.set_xlim(-2.9, 8.05)
ax.set_ylim(-0.9, len(ROWS) + 0.35)
ax.axis("off")
plt.tight_layout()
os.makedirs("docs/figures/32", exist_ok=True)
plt.savefig("docs/figures/32/fig-32-5-cts-layout.png", bbox_inches="tight", facecolor="white", dpi=300)
print("ok docs/figures/32/fig-32-5-cts-layout.png")
