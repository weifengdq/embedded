#!/usr/bin/env python3
"""fig-40-2: 实测端到端延迟与有效吞吐对比（数据来自各章实测日志）"""
import pathlib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

out = pathlib.Path(__file__).resolve().parents[2] / "docs/figures/39"
out.mkdir(parents=True, exist_ok=True)

# (名称, 延迟 ms, 备注)  —— 均为本手册上板实测值，详见第 28~39 章
lat = [
    ("PTP offset |mean| (ns→ms)", 0.0000155),
    ("SOME/IP RPC (0.103)", 0.103),
    ("J1939 request/response (0.972)", 0.972),
    ("CANopen PDO A (0.341)", 0.341),
    ("XCP DAQ event period (1.0)", 1.0),
    ("CANopen PDO B via gateway (2.30)", 2.30),
    ("XRCE cmd RTT (2.70)", 2.70),
    ("cannelloni round-trip (2.24)", 2.24),
]
thr = [
    ("CANopen SDO seg", 14.0),
    ("ISO-TP classic 500k", 29.2),
    ("ISO-TP CAN FD", 189.6),
    ("J1939 RTS/CTS", 6.9),
    ("J1939 BAM", 0.14),
    ("SOME/IP events", 1.3),
    ("XRCE-DDS 100Hz", 2.0),
]

fig, axes = plt.subplots(1, 2, figsize=(11.2, 4.2))
names = [n for n, _ in lat][::-1]
vals = [v for _, v in lat][::-1]
axes[0].barh(names, vals, color="#4a78a8")
axes[0].set_xscale("log")
axes[0].set_xlabel("end-to-end latency / period  [ms, log scale]")
axes[0].set_title("(a) latency: PTP ns-level to ms-level gateway tunnel")
axes[0].grid(axis="x", ls=":", alpha=0.5)
for i, v in enumerate(vals):
    axes[0].text(v * 1.15, i, f"{v:g}", va="center", fontsize=8)

n2 = [n for n, _ in thr][::-1]
v2 = [v for _, v in thr][::-1]
axes[1].barh(n2, v2, color="#2e8b57")
axes[1].set_xscale("log")
axes[1].set_xlabel("application throughput [KB/s, log scale]")
axes[1].set_title("(b) effective throughput: single-frame vs multi-frame/FD")
axes[1].grid(axis="x", ls=":", alpha=0.5)
for i, v in enumerate(v2):
    axes[1].text(v * 1.15, i, f"{v:g}", va="center", fontsize=8)

fig.suptitle("HPM5E31 application-layer protocols: measured latency & throughput (2026-10-30)", fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.94))
fig.savefig(out / "fig-40-2-perf-compare.png", dpi=200, facecolor="white")
print(out / "fig-40-2-perf-compare.png")