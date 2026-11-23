#!/usr/bin/env python3
"""tools/figs/mcal_can_tdc.py -- Chapter 48 figure: CAN FD 5 Mbit/s TDC offset scan.

Data: on-board measurements of hpm5e31_1 (mcal/can_fd_echo "can matrix 50"),
logs/20261102-155424-can_fd_echo.log.  Data-segment bit = 16 tq (80 MHz, BRP=1,
12.5 ns/tq, 5 Mbit/s).  TDCR.TDCO = SSP position in data-segment mtq.
Output: docs/figures/47-mcal-can/fig-47-4-tdc-offset-scan.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "docs", "figures", "47-mcal-can")
os.makedirs(OUT, exist_ok=True)
plt.rcParams.update({"figure.facecolor": "white", "axes.facecolor": "white",
                     "savefig.facecolor": "white", "font.size": 10,
                     "axes.grid": True, "grid.alpha": 0.3})

tdco = [1, 8, 10, 12, 13, 14, 20]
tec = [248, 0, 0, 0, 0, 0, 248]
ok = [False, True, True, True, True, True, False]
bit_tq = 16

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.8))

colors = ["#2e8b57" if o else "#d64541" for o in ok]
ax1.bar([str(t) for t in tdco], tec, width=0.6, color=colors)
ax1.set_xlabel("TDCR.TDCO (data-phase mtq)")
ax1.set_ylabel("TEC after 50 ping-pong rounds")
ax1.set_title("500k/5M, 64 B FD: TDCO sweep (TEC)")
for i, (t, o) in enumerate(zip(tdco, ok)):
    ax1.text(i, tec[i] + 6 if tec[i] else 8, "PASS" if o else "BUS-OFF",
             ha="center", fontsize=8, color="#222222")
ax1.set_ylim(0, 290)
ax1.axvspan(1.5, 5.5, color="#2e8b57", alpha=0.10)

ax2.axhspan(0, bit_tq, color="#eef3fa")
ax2.axhline(bit_tq, color="#222222", linestyle="--", linewidth=1.0,
            label="data bit = 16 tq (200 ns @5 Mbit/s)")
for t, o in zip(tdco, ok):
    ax2.plot([t], [t], marker="o", markersize=7,
             color="#2e8b57" if o else "#d64541")
    ax2.annotate(str(t), (t, t), textcoords="offset points", xytext=(6, -3), fontsize=8)
ax2.axhline(12, color="#ff8c00", linestyle=":", linewidth=1.2,
            label="default TDCO=12 (75% of bit)")
ax2.set_xlabel("TDCO (mtq)")
ax2.set_ylabel("SSP position (mtq from bit start)")
ax2.set_title("SSP must stay inside the 16 tq data bit")
ax2.set_xlim(0, 22)
ax2.set_ylim(0, 24)
ax2.legend(fontsize=8, loc="upper left")

fig.suptitle("CAN FD 5 Mbit/s TDC offset: legal window and bus-off beyond the bit", fontsize=10)
fig.tight_layout(rect=(0, 0, 1, 0.94))
fig.savefig(os.path.join(OUT, "fig-47-4-tdc-offset-scan.png"), dpi=300)
plt.close(fig)
print("figure written to", OUT)
