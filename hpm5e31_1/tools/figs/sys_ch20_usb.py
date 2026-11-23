#!/usr/bin/env python3
"""第 20 章 USB 插图: CDC ACM 枚举流程 + 端点/数据通路"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

def box(ax, x, y, w, h, t, fc="#e8f0fe"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02", fc=fc, ec="#33517a"))
    ax.text(x + w/2, y + h/2, t, ha="center", va="center", fontsize=7.5)

fig, ax = plt.subplots(figsize=(9, 3.6))
steps = ["bus reset\n(SE0)", "SET_ADDRESS", "GET_DESCRIPTOR\n(device/config)", "SET_CONFIGURATION",
         "CDC class req\n(SET_LINE_CODING)", "SET_CONTROL_LINE_STATE\n(DTR=1)", "bulk data\nIN/OUT"]
w = 0.125
for i, s in enumerate(steps):
    box(ax, 0.01 + i*(w+0.005), 0.55, w, 0.3, s, "#e8f0fe" if i < 5 else "#e6f4ea")
    if i:
        ax.add_patch(FancyArrowPatch((0.01 + (i-1)*(w+0.005) + w, 0.70), (0.01 + i*(w+0.005), 0.70),
                                     arrowstyle="-|>", mutation_scale=8, color="#33517a"))
box(ax, 0.30, 0.12, 0.40, 0.24, "host kernel: cdc_acm -> /dev/ttyACM2\nlogs/20261030-114253-usb_cdc_acm_host.log", fc="#fff4e5")
ax.set_xlim(0,1); ax.set_ylim(0,1); ax.axis("off")
ax.set_title("USB CDC ACM enumeration observed on host (dmesg: idVendor=34b7, Product HPMicro CDC DEMO)", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/20/fig-20-1-usb-enum-flow.png", dpi=160); plt.close()

fig, ax = plt.subplots(figsize=(8, 3.2))
box(ax, 0.05, 0.55, 0.22, 0.24, "USB0 device\n0xF142_0000")
box(ax, 0.40, 0.72, 0.20, 0.16, "EP0 control\n(8B setup)")
box(ax, 0.40, 0.50, 0.20, 0.14, "EP1 OUT (0x01)\nbulk, 512B HS")
box(ax, 0.40, 0.30, 0.20, 0.14, "EP1 IN (0x81)\nbulk, 512B HS")
box(ax, 0.70, 0.30, 0.25, 0.20, "read_buffer[2]\nwrite_buffer[2048]\n.noncacheable.non_init")
box(ax, 0.05, 0.10, 0.22, 0.20, "EP2 IN (0x83)\ninterrupt (CDC notify)")
for p1,p2 in [((0.27,0.67),(0.40,0.79)),((0.27,0.62),(0.40,0.57)),((0.27,0.57),(0.40,0.37)),
              ((0.60,0.57),(0.70,0.42)),((0.70,0.36),(0.60,0.36)),((0.27,0.50),(0.17,0.30))]:
    ax.add_patch(FancyArrowPatch(p1,p2, arrowstyle="<|-|>", mutation_scale=8, color="#33517a"))
ax.set_xlim(0,1); ax.set_ylim(0,1); ax.axis("off")
ax.set_title("CDC ACM endpoint / buffer layout (CherryUSB port)", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/20/fig-20-2-cdc-endpoints.png", dpi=160); plt.close()
print("ch20 figs ok")
