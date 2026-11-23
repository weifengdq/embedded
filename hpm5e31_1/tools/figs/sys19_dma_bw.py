#!/usr/bin/env python3
"""fig-19-3: DMA 带宽 vs 存储位置 / 突发长度 / CPU 并发（数据直接解析实测日志）

数据来源: samples/dma/dma_mem2mem 的 dmamem/dmaburst/dmacpu 输出
          logs/*-dma_mem2mem-r5b.log（2026-11-02 实测）
输出: docs/figures/19/fig-19-3-dma-mem-bw.png
"""
import glob
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def latest_log():
    cands = sorted(glob.glob(os.path.join(ROOT, "logs", "*-dma_mem2mem-r5c.log")))
    if not cands:
        cands = sorted(glob.glob(os.path.join(ROOT, "logs", "*-dma_mem2mem*.log")))
    if not cands:
        sys.exit("no dma_mem2mem log found")
    return cands[-1]


def parse(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    bw = {}
    for m in re.finditer(r"BENCH_BW name=(\S+)\s+mbps=\s*([0-9.]+)", txt):
        bw[m.group(1)] = float(m.group(2))
    conc = None
    m = re.search(r"BENCH_CONC solo_mbps=([0-9.]+) alu_mbps=([0-9.]+) mem_mbps=([0-9.]+)", txt)
    if m:
        conc = [float(m.group(i)) for i in (1, 2, 3)]
    return bw, conc, path


bw, conc, src = parse(latest_log())

groups = [
    ("AXI_SCC->AXI_SCC", "AXI\u2192AXI"),
    ("AXI_SCC->ILM(local)", "AXI\u2192ILM"),
    ("AXI_SCC->ILM(alias)", "AXI\u2192ILM(alias)"),
    ("ILM(local)->AXI_SCC", "ILM\u2192AXI"),
    ("ILM(local)->ILM(local)", "ILM\u2192ILM"),
    ("ILM(alias)->ILM(alias)", "ILM(a)\u2192ILM(a)"),
    ("AXI_SCC->DLM(local)", "AXI\u2192DLM"),
    ("AXI_SCC->DLM(alias)", "AXI\u2192DLM(alias)"),
    ("DLM(alias)->AXI_SCC", "DLM\u2192AXI"),
    ("DLM(alias)->DLM(alias)", "DLM(a)\u2192DLM(a)"),
    ("HDMA AXI->ILM(local)", "HDMA AXI\u2192ILM"),
]
labels, vals = [], []
for key, label in groups:
    if key in bw:
        labels.append(label)
        vals.append(bw[key])

fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.2), dpi=300)
ax = axes[0]
colors = ["#1F6FEB"] * len(vals)
bars = ax.bar(range(len(vals)), vals, color=colors)
for i, v in enumerate(vals):
    ax.text(i, v + max(vals) * 0.015, "%.0f" % v, ha="center", fontsize=7)
ax.set_xticks(range(len(labels)))
ax.set_xticklabels(labels, rotation=35, ha="right", fontsize=7.5)
ax.set_ylabel("throughput (MB/s)")
ax.set_title("8 KB copy, 512 B burst, XDMA (HDMA last)", fontsize=9.5)
ax.grid(True, axis="y", linestyle=":", alpha=0.45)
ax.set_ylim(0, max(vals) * 1.18)

ax = axes[1]
if conc:
    names = ["DMA only", "+CPU ALU", "+CPU read AHB"]
    cl = [conc[0], conc[1], conc[2]]
    ax.bar(range(3), cl, color=["#2EA043", "#D29922", "#DA3633"])
    for i, v in enumerate(cl):
        d = 0.0 if i == 0 or cl[0] == 0 else 100.0 * (v - cl[0]) / cl[0]
        lbl = "%.0f" % v if i == 0 else "%.0f\n%+.1f%%" % (v, d)
        ax.text(i, v + max(cl) * 0.02, lbl, ha="center", fontsize=7.5)
    ax.set_xticks(range(3))
    ax.set_xticklabels(names, fontsize=8.5)
    ax.set_ylabel("throughput (MB/s)")
    ax.set_title("XDMA 16 KB copy with concurrent CPU load", fontsize=9.5)
    ax.grid(True, axis="y", linestyle=":", alpha=0.45)
    ax.set_ylim(0, max(cl) * 1.25)

fig.tight_layout()
out = os.path.join(ROOT, "docs", "figures", "19", "fig-19-3-dma-mem-bw.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, bbox_inches="tight")
print("src:", src)
print("wrote", out)
