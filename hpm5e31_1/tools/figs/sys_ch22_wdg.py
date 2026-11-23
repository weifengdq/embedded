#!/usr/bin/env python3
"""第 22 章 看门狗/低功耗/温度 插图: EWDG 复位路径 + 电源域/低功耗模式"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

def box(ax, x, y, w, h, t, fc="#e8f0fe"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02", fc=fc, ec="#33517a"))
    ax.text(x + w/2, y + h/2, t, ha="center", va="center", fontsize=8)

fig, ax = plt.subplots(figsize=(9, 3.6))
box(ax, 0.02, 0.60, 0.18, 0.22, "EWDG0\ncnt_clk = OSC32\n32768 Hz")
box(ax, 0.26, 0.60, 0.20, 0.22, "timeout counter\n(500 ms -> 16384 ticks)")
box(ax, 0.52, 0.60, 0.16, 0.22, "timeout reset\ngenerator")
box(ax, 0.74, 0.60, 0.22, 0.22, "PPOR reset\n(RESET_FLAG bit16)")
box(ax, 0.52, 0.22, 0.16, 0.20, "eWDG refresh\n(feed before timeout)")
box(ax, 0.74, 0.18, 0.22, 0.22, "SoC reboot\n-> boot banner", fc="#e6f4ea")
ax.add_patch(FancyArrowPatch((0.20,0.71),(0.26,0.71), arrowstyle="-|>", mutation_scale=9, color="#33517a"))
ax.add_patch(FancyArrowPatch((0.46,0.71),(0.52,0.71), arrowstyle="-|>", mutation_scale=9, color="#33517a"))
ax.add_patch(FancyArrowPatch((0.68,0.71),(0.74,0.71), arrowstyle="-|>", mutation_scale=9, color="#33517a"))
ax.add_patch(FancyArrowPatch((0.85,0.60),(0.85,0.40), arrowstyle="-|>", mutation_scale=9, color="#33517a"))
ax.add_patch(FancyArrowPatch((0.60,0.42),(0.60,0.60), arrowstyle="-|>", mutation_scale=9, color="#2e7d32", linestyle="--"))
ax.text(0.62, 0.50, "no refresh: reset", fontsize=7, color="#c62828")
ax.set_xlim(0,1); ax.set_ylim(0,1); ax.axis("off")
ax.set_title("EWDG timeout reset path (measured: request 500 ms, actual 499990 us, RESET_FLAG=0x00010000)", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/22/fig-22-1-ewdg-reset-path.png", dpi=160); plt.close()

fig, ax = plt.subplots(figsize=(9, 3.4))
box(ax, 0.02, 0.70, 0.22, 0.20, "VDD_SOC domain\nCPU/AXI/ADC/USB")
box(ax, 0.02, 0.40, 0.22, 0.20, "VDD_PMIC / DCDC\n0.9-1.3 V")
box(ax, 0.35, 0.62, 0.26, 0.16, "CPU WAIT (WFI)\nclock gated", fc="#fff4e5")
box(ax, 0.35, 0.40, 0.26, 0.16, "CPU STOP\nperiph clocks off", fc="#fff4e5")
box(ax, 0.35, 0.18, 0.26, 0.16, "SLEEP / PDGO\npower domain off", fc="#fff4e5")
box(ax, 0.70, 0.55, 0.26, 0.22, "wakeup sources\nGPIO / PTMR / RTC\nUART / CAN / EWDG", fc="#e6f4ea")
box(ax, 0.70, 0.18, 0.26, 0.22, "PPOR reset flags\n+ PGPR retention", fc="#e6f4ea")
for p1,p2 in [((0.24,0.80),(0.35,0.70)),((0.24,0.50),(0.35,0.48)),((0.35,0.26),(0.24,0.48)),
              ((0.61,0.70),(0.70,0.66)),((0.61,0.48),(0.70,0.62)),((0.70,0.30),(0.61,0.30))]:
    ax.add_patch(FancyArrowPatch(p1,p2, arrowstyle="<|-|>", mutation_scale=8, color="#33517a"))
ax.set_xlim(0,1); ax.set_ylim(0,1); ax.axis("off")
ax.set_title("Power domains, low-power modes and wakeup sources (RM 15 / 18.5 / 12)", fontsize=9)
plt.tight_layout(); plt.savefig("docs/figures/22/fig-22-2-power-modes.png", dpi=160); plt.close()
print("ch22 figs ok")
