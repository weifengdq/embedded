
#!/usr/bin/env python3
"""图 29-3：STmin=0 时相邻 CF 间隔实测（经典 500k vs CAN FD 1M/2M）

数据来源：logs/20261102-120902-isotp_echo.log（isotp_echo 的 `stat` 输出 cf gap 行）
  classic: cnt=584 avg=231 min=224 max=246  tx_us=136099 (4095 B)
  FD     : cnt=64  avg=320 min=312 max=338  tx_us=21014  (4095 B, CF 载荷 63 B)

生成：python3 tools/figs/app30_cf_gap.py
产物：docs/figures/29/fig-29-3-cf-gap.png
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup  # noqa: E402  全书统一样式/中文字体

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

setup()

MODES = ["经典 500 kbit/s\n(CF 载荷 7 B)", "CAN FD 1 M/2 M\n(CF 载荷 63 B)"]
AVG = [231, 320]
MIN = [224, 312]
MAX = [246, 338]
THEORY = [222, 300]
PAYLOAD = [7, 63]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.5, 4.0), dpi=300)

x = np.arange(len(MODES))
w = 0.34
ax1.bar(x - w / 2, THEORY, w, label="单帧线上时间（估算）", color="#90a4ae")
ax1.bar(x + w / 2, AVG, w, label="实测平均 CF 间隔", color="#1565c0")
ax1.errorbar(x + w / 2, AVG, yerr=[np.array(AVG) - np.array(MIN), np.array(MAX) - np.array(AVG)],
             fmt="none", ecolor="#c62828", capsize=4, lw=1.2, label="实测 min~max")
for i in range(len(MODES)):
    ax1.text(x[i] + w / 2, MAX[i] + 8, "%d µs" % AVG[i], ha="center", fontsize=9)
ax1.set_xticks(x)
ax1.set_xticklabels(MODES, fontsize=9)
ax1.set_ylabel("相邻 CF 间隔 (µs)")
ax1.set_ylim(0, 400)
ax1.grid(True, axis="y", ls=":", alpha=0.5)
ax1.legend(fontsize=8, loc="upper left")
ax1.set_title("间隔≈单帧线上时间，软件开销 9~20 µs", fontsize=10)

per_byte = [p / (t * 1e-6) / 1024.0 for p, t in zip(PAYLOAD, AVG)]
bars = ax2.bar(x, per_byte, 0.5, color=["#90a4ae", "#1565c0"])
for i, v in enumerate(per_byte):
    ax2.text(x[i], v + 3, "%.0f KB/s" % v, ha="center", fontsize=9)
ax2.set_xticks(x)
ax2.set_xticklabels(MODES, fontsize=9)
ax2.set_ylabel("单方向有效载荷速率 (KB/s)")
ax2.set_ylim(0, max(per_byte) * 1.25)
ax2.grid(True, axis="y", ls=":", alpha=0.5)
ax2.set_title("FD 每帧多装 56 B，按字节算是经典的 6.5×", fontsize=10)

plt.tight_layout()
os.makedirs("docs/figures/30", exist_ok=True)
plt.savefig("docs/figures/29/fig-29-3-cf-gap.png", bbox_inches="tight", facecolor="white")
print("ok docs/figures/29/fig-29-3-cf-gap.png")
