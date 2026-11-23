#!/usr/bin/env python3
"""tools/figs/mcal_io_spi_i2c.py -- Chapter 46 (MCAL IO: Spi/I2c/Adc) figures.

Data: all on-board measurements of hpm5e31_1, logs under logs/.
  SPI  : logs/20261102-142330-spi_master_slave.log
  I2C  : logs/20261102-142612-i2c_master_slave.log
  ADC  : logs/20261102-143752-adc_single.log, logs/20261102-143824-adc_bench.log (measured by mcal-core)
Output: docs/figures/46-mcal-io/fig-47-{1,2,3,4}-*.png (300 dpi, white background)
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "docs", "figures", "46-mcal-io")
os.makedirs(OUT, exist_ok=True)
plt.rcParams.update({"figure.facecolor": "white", "axes.facecolor": "white",
                     "savefig.facecolor": "white", "font.size": 10,
                     "axes.grid": True, "grid.alpha": 0.3})

# ---- Fig 46-1: SPI 1 MHz full-duplex latency / throughput ----
payload = [64, 256, 1024]
latency_us = [534.5, 2064.7, 8208.6]
theory_kbs = 250.0
meas_kbs = [2 * p * 1000.0 / t for p, t in zip(payload, latency_us)]

fig, ax1 = plt.subplots(figsize=(6.4, 4.0))
x = list(range(len(payload)))
ax1.bar([i - 0.18 for i in x], meas_kbs, width=0.36, color="#3b7dd8",
        label="measured bidirectional")
ax1.axhline(theory_kbs, color="#d64541", linestyle="--", label="theory 250 KB/s (1 MHz/8 bit)")
ax1.set_xticks(x)
ax1.set_xticklabels([f"{p} B" for p in payload])
ax1.set_ylabel("bidirectional throughput (KB/s)")
ax1.set_ylim(0, 300)
ax1.legend(loc="lower right", fontsize=8)
ax2 = ax1.twinx()
ax2.plot(x, latency_us, color="#222222", marker="o", label="transfer latency")
ax2.set_ylabel("transfer latency (us)")
ax2.set_yscale("log")
ax2.grid(False)
ax2.legend(loc="upper left", fontsize=8)
for i, v in enumerate(meas_kbs):
    ax1.text(i, v + 4, f"{v:.0f}", ha="center", fontsize=8)
plt.title("SPI1(master) <-> SPI3(slave), 1 MHz full duplex")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig-46-1-spi-throughput.png"), dpi=300)
plt.close(fig)

# ---- Fig 46-2: I2C 32 B write/read at 100k/400k/1M ----
speeds = ["100k", "400k", "1M"]
rate_khz = [100, 400, 1000]
# 2026-11-02 复测（目标侧 T_SUDAT 修复后，logs/20261102-161116-i2c_master_slave.log）
wr_us = [3238.8, 992.8, 538.4]
rd_us = [3233.8, 988.8, 536.6]
# 32 B x 9 bit / 速率 -> 毫秒，换算成 us（原脚本漏了 x1000，理论线与效率轴偏小 1000 倍）
theo_us = [32 * 9 / r * 1000.0 for r in rate_khz]

fig, ax1 = plt.subplots(figsize=(6.4, 4.0))
x = list(range(len(speeds)))
ax1.bar([i - 0.18 for i in x], wr_us, width=0.34, color="#2e8b57", label="write 32 B (measured)")
ax1.bar([i + 0.18 for i in x], rd_us, width=0.34, color="#87cefa", label="read 32 B (measured)")
ax1.plot(x, theo_us, color="#d64541", marker="s", linestyle="--", label="theory (9 bit/byte)")
ax1.set_xticks(x)
ax1.set_xticklabels(speeds)
ax1.set_ylabel("transfer latency (us)")
ax1.set_yscale("log")
ax1.legend(loc="upper right", fontsize=8)
ax2 = ax1.twinx()
eff = [100.0 * t / ((w + r) / 2) for t, w, r in zip(theo_us, wr_us, rd_us)]
ax2.plot(x, eff, color="#ff8c00", marker="^", label="theory/measured latency (%)")
ax2.set_ylabel("theory / measured (%)")
ax2.set_ylim(0, 110)
ax2.grid(False)
ax2.legend(loc="center left", fontsize=8)
plt.title("I2C2(controller) <-> I2C3(target), 32 B transfer")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig-46-2-i2c-rate-latency.png"), dpi=300)
plt.close(fig)

# ---- Fig 46-3: SPI 1 MB continuous transfer, 1 MHz vs 10 MHz ----
tot = 1024 * 1024
meas_ms = [8755.5, 1201.7]
vals = [tot / (ms / 1000.0) / 1000.0 for ms in meas_ms]
labels = ["1 MHz", "10 MHz"]
theo = [125.0, 1250.0]
fig, ax = plt.subplots(figsize=(6.4, 4.0))
x = [0, 1]
ax.bar(x, vals, width=0.4, color=["#3b7dd8", "#9bb7e8"], label="measured (1 MB, chunk 1 KB)")
ax.plot(x, theo, color="#d64541", linestyle="--", marker="o", label="theory (SCLK/8)")
ax.set_xticks(x)
ax.set_xticklabels(labels)
ax.set_ylabel("single direction throughput (KB/s)")
ax.set_title("SPI 1 MB continuous transfer: measured vs theory")
ax.set_ylim(0, 1400)
for i, v in enumerate(vals):
    ax.text(i, v + 30, f"{v:.0f} KB/s\n{v / theo[i] * 100:.1f}%", ha="center", fontsize=8)
ax.legend(fontsize=8)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig-46-3-spi-1mb.png"), dpi=300)
plt.close(fig)

# ---- Fig 46-4: ADC single vs continuous group (mcal-core measured) ----
fig, (axa, axb) = plt.subplots(1, 2, figsize=(9.6, 4.0))
labels = ["single\n(N=1000)", "single\n(N=5000)", "continuous\n(20k rounds)"]
thr = [491.8, 506.4, 740.4]          # kS/s measured
theo = 1388.9                         # kS/s theory: conv_clk/(16+20)
axa.bar(range(3), thr, width=0.5, color=["#3b7dd8", "#6fa0e2", "#2e8b57"])
axa.axhline(theo, color="#d64541", linestyle="--", label="theory 1388.9 kS/s")
axa.set_xticks(range(3))
axa.set_xticklabels(labels, fontsize=8)
axa.set_ylabel("sample rate (kS/s)")
axa.set_title("ADC0.IN01 sample rate (measured)")
for i, v in enumerate(thr):
    axa.text(i, v + 25, f"{v:.1f}", ha="center", fontsize=8)
axa.legend(fontsize=8)

x = [0, 1]
axb.bar([i - 0.18 for i in x], [62.66, 60.29], width=0.34, color="#ff8c00", label="sd (LSB)")
axb.set_ylabel("noise sd (LSB)")
axb.set_xticks(x)
axb.set_xticklabels(["N=1000", "N=5000"])
axb.set_title("ADC noise / ENOB (measured, floating input)")
axc = axb.twinx()
axc.bar([i + 0.18 for i in x], [8.43, 8.48], width=0.34, color="#8a2be2", label="ENOB (bit)")
axc.set_ylabel("ENOB (bit)")
axc.set_ylim(0, 18)
axc.grid(False)
h1, l1 = axb.get_legend_handles_labels()
h2, l2 = axc.get_legend_handles_labels()
axb.legend(h1 + h2, l1 + l2, loc="upper center", fontsize=8)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig-46-4-adc-group.png"), dpi=300)
plt.close(fig)

print("figures written to", OUT)
