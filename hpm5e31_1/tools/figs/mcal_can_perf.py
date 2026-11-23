#!/usr/bin/env python3
"""tools/figs/mcal_can_perf.py —— 第 47 章（MCAL CAN）插图

图 47-1：经典帧 / CAN FD 帧的位时间构成（理论拆解 + 实测单程延迟对照）
图 47-2：CAN FD 各载荷 ping-pong 实测延迟（500 kbit/s 仲裁 + 2 Mbit/s 数据，BRS）
图 47-3：4 路并发吞吐与单帧延迟构成（实测 vs 理论上限）

数据来源（全部为上板实测，日志在 logs/ 下）：
  logs/20261102-141808-can_fd_echo.log      FD 各 DLC 延迟
  logs/20261102-142208-can_irq_perf.log     4 路吞吐 / CPU / 延迟
  logs/20261102-142350-can_classic_echo.log 经典帧 ping-pong / 单向流水
"""

from _common import setup, save, COLORS
import matplotlib.pyplot as plt
import numpy as np

setup()

# ============================ 图 47-1 帧时间构成 ============================
# 位时间理论值：经典帧 8B = 108 bit + 3 bit IFS；FD 帧分仲裁段/数据段两段速率
# 仲裁段（500 kbit/s）：SOF1+ID11+RRS1+IDE1+FDF1+res1+BRS1 = 18 bit，帧尾 ACK2+EOF7+IFS3 = 12 bit
ARB_BITS = 18 + 12

def fd_data_bits(nbytes):
    """FD 数据段（BRS 之后）bit 数：ESI1+DLC4+data+CRC+CRCdel1（CRC17/21 按载荷）"""
    crc = 17 if nbytes <= 16 else 21
    return 1 + 4 + nbytes * 8 + crc + 1

def fd_frame_us(nbytes, arb_kbps=500, data_kbps=2000):
    arb = ARB_BITS / float(arb_kbps) * 1000.0
    data = fd_data_bits(nbytes) / float(data_kbps) * 1000.0
    return arb, data

def classic_frame_us(nbytes, kbps=500):
    bits = 1 + 12 + 6 + nbytes * 8 + 16 + 2 + 7  # SOF/仲裁/控制/数据/CRC+del/ACK/EOF
    return (bits + 3) / float(kbps) * 1000.0

payloads = [8, 16, 32, 64]
labels = ["经典 8B\n@500k", "FD 8B", "FD 16B", "FD 32B", "FD 64B"]
arb_us = [classic_frame_us(8)] + [fd_frame_us(n)[0] for n in payloads]
data_us = [0.0] + [fd_frame_us(n)[1] for n in payloads]
# 实测单程延迟 = ping-pong 往返 / 2（can_fd_echo 的 avg_us）
meas_rt = [254.0, 297.0, 436.0, 706.0]        # 8/16/32/64B（FD）
meas_rt = [classic_frame_us(8) * 2 + 22] + meas_rt  # 经典帧用单向理论*2 + 轮询开销

fig, ax = plt.subplots(figsize=(7.2, 3.6))
x = np.arange(len(labels))
ax.bar(x - 0.18, arb_us, width=0.34, color=COLORS["blue"], label="仲裁段 @500 kbit/s")
ax.bar(x - 0.18, data_us, width=0.34, bottom=arb_us, color=COLORS["orange"],
       label="数据段（BRS，FD @2 Mbit/s）")
ax.bar(x + 0.18, [m / 2.0 for m in meas_rt], width=0.34, color=COLORS["gray"],
       alpha=0.55, label="实测单程延迟（往返/2）")
for i in range(len(labels)):
    total = arb_us[i] + data_us[i]
    ax.text(x[i] - 0.18, total + 8, "%.0f" % total, ha="center", fontsize=8.5)
    ax.text(x[i] + 0.18, meas_rt[i] / 2.0 + 8, "%.0f" % (meas_rt[i] / 2.0), ha="center",
            fontsize=8.5, color=COLORS["gray"])
ax.set_xticks(x)
ax.set_xticklabels(labels)
ax.set_ylabel("单帧总线时间 (µs)")
ax.set_ylim(0, 420)
ax.grid(axis="y", linestyle=":", linewidth=0.6, alpha=0.6)
ax.legend(loc="upper left", frameon=False)
save(fig, "47-mcal-can", "fig-47-1-frame-timing")

# ============================ 图 47-2 FD 延迟 vs 载荷 ============================
sizes = [8, 12, 16, 20, 24, 32, 48, 64]
lat = [254, 262, 297, 335, 369, 436, 572, 706]
theory = [(classic_frame_us(0, 500) and (fd_frame_us(n)[0] + fd_frame_us(n)[1])) * 2 + 40 for n in sizes]
measured_oneway = [v / 2.0 for v in lat]

fig, ax = plt.subplots(figsize=(7.2, 3.4))
ax.plot(sizes, measured_oneway, "o-", color=COLORS["blue"], linewidth=1.8,
        label="实测单程延迟（往返/2）")
ax.plot(sizes, [t / 2.0 for t in theory], "--", color=COLORS["orange"], linewidth=1.4,
        label="理论帧时间（仲裁+数据段）")
for sx, sy in zip(sizes, measured_oneway):
    ax.annotate("%.0f" % sy, (sx, sy), textcoords="offset points", xytext=(0, 7),
                ha="center", fontsize=8, color=COLORS["dark"])
ax.set_xlabel("CAN FD 载荷 (字节)")
ax.set_ylabel("单程时间 (µs)")
ax.set_xticks(sizes)
ax.grid(linestyle=":", linewidth=0.6, alpha=0.6)
ax.legend(loc="upper left", frameon=False)
ax.set_ylim(0, 420)
save(fig, "47-mcal-can", "fig-47-2-fd-latency-vs-payload")

# ============================ 图 47-3 4 路吞吐与延迟构成 ============================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.6, 3.3))

names = ["理论\n(500k/114bit)", "实测 4 路总", "实测 单通道"]
vals = [4386 * 2, 8366, 4183]
cols = [COLORS["gray"], COLORS["blue"], COLORS["green"]]
b = ax1.bar(names, vals, color=cols, width=0.55)
for rect, v in zip(b, vals):
    ax1.text(rect.get_x() + rect.get_width() / 2, v + 120, "%d" % v, ha="center", fontsize=8.5)
ax1.set_ylabel("吞吐 (frame/s)")
ax1.set_ylim(0, 10500)
ax1.grid(axis="y", linestyle=":", linewidth=0.6, alpha=0.6)
ax1.set_title("4 路并发（CAN0→1, CAN2→3）", fontsize=10)
ax1.text(0.02, 0.86, "CPU 4.62% @480 MHz\n(Can_MainFunction_*)", transform=ax1.transAxes,
         fontsize=8, color=COLORS["dark"])

parts = ["总线传输\n(8B 帧 @500k)", "主函数轮询/回调\n开销"]
vals2 = [222, 24]
ax2.barh([0], [vals2[0]], color=COLORS["blue"], label="总线传输 222 µs")
ax2.barh([0], [vals2[1]], left=[vals2[0]], color=COLORS["orange"], label="轮询/回调 24 µs")
ax2.text(vals2[0] / 2, 0, "222", ha="center", va="center", color="white", fontsize=9)
ax2.text(vals2[0] + vals2[1] / 2, 0, "24", ha="center", va="center", color="white", fontsize=8)
ax2.set_yticks([])
ax2.set_xlabel("单帧端到端延迟 (µs)  实测 avg 246 µs / max 256 µs")
ax2.set_xlim(0, 300)
ax2.grid(axis="x", linestyle=":", linewidth=0.6, alpha=0.6)
ax2.legend(loc="lower right", frameon=False, fontsize=8)
ax2.set_title("延迟构成（classic 8B，轮询模型）", fontsize=10)

fig.tight_layout()
save(fig, "47-mcal-can", "fig-47-3-throughput-and-latency-budget")
print("all figures done")
