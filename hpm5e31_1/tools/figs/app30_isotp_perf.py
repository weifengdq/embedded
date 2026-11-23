#!/usr/bin/env python3
"""图 29-4：实测 RTT 与吞吐 vs 消息长度（数据来自 logs/*isotp_echo.log）

生成：python3 tools/figs/app30_isotp_perf.py
产物：docs/figures/29/fig-29-4-perf.png
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup  # noqa: E402  全书统一样式/中文字体

import matplotlib.pyplot as plt  # noqa: E402

setup()

LEN = [1, 7, 8, 12, 64, 256, 1024, 4095]
# logs/20261030-165808-isotp_echo.log（经典 500k 与 CAN FD 1M/2M 两次 run 的 rtt_us 列）
RTT_CLASSIC = [519, 461, 1430, 1408, 5140, 17729, 69034, 273504]
RTT_FD = [170, 167, 209, 240, 982, 2913, 10756, 42188]

tp_classic = [2 * n / (t / 1e6) / 1024 for n, t in zip(LEN, RTT_CLASSIC)]
tp_fd = [2 * n / (t / 1e6) / 1024 for n, t in zip(LEN, RTT_FD)]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2), dpi=300)
ax1.semilogy(LEN, RTT_CLASSIC, "o-", label="经典 500 kbit/s", color="#c62828")
ax1.semilogy(LEN, RTT_FD, "s-", label="CAN FD 1M/2M (64 B)", color="#1565c0")
ax1.set_xscale("log", base=2)
ax1.set_xlabel("ISO-TP 消息长度 (字节)")
ax1.set_ylabel("回显往返时间 RTT (µs, 对数)")
ax1.grid(True, which="both", ls=":", alpha=0.5)
ax1.legend(fontsize=8)
ax1.set_title("回显 RTT：FD 比经典快 6.5×", fontsize=10)

ax2.plot(LEN, tp_classic, "o-", label="经典 500 kbit/s", color="#c62828")
ax2.plot(LEN, tp_fd, "s-", label="CAN FD 1M/2M", color="#1565c0")
ax2.set_xscale("log", base=2)
ax2.set_xlabel("ISO-TP 消息长度 (字节)")
ax2.set_ylabel("双向有效载荷吞吐 (KB/s)")
ax2.grid(True, ls=":", alpha=0.5)
ax2.legend(fontsize=8)
ax2.set_title("吞吐随长度上升并收敛到物理上限", fontsize=10)
for ax in (ax1, ax2):
    ax.tick_params(labelsize=8)

plt.tight_layout()
os.makedirs("docs/figures/30", exist_ok=True)
plt.savefig("docs/figures/29/fig-29-4-perf.png", bbox_inches="tight", facecolor="white")
print("ok docs/figures/29/fig-29-4-perf.png")
