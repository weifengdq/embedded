#!/usr/bin/env python3
"""第 45 章 图 45-1：Gpt/Pwm/Icu 定时器链路与捕获时序。

(a) 板级测量链路框图：PWM0 P2(PD02) --板载杜邦线--> PD07 --TRGM0_P07--> GPTMR2 CAPT_3
(b) PWM 输出波形与 GPTMR2 measure_width 模式的 CAPPRD / CAPDTY 关系

数据依据：mcal/Pwm/cfg/Pwm_PBcfg.c、mcal/Icu/cfg/Icu_PBcfg.c、HPM5E31 RM §37.4.8；
实测日志 logs/20261102-141059-pwm_basic.log。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

setup()

# ---------------- (a) 链路框图 ----------------
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7.2, 6.0), gridspec_kw={"height_ratios": [1.0, 1.0]})
ax1.set_xlim(0, 100)
ax1.set_ylim(0, 40)
ax1.axis("off")


def box(ax, x, y, w, h, text, color, fs=8.2):
    b = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.4,rounding_size=1.2",
                       linewidth=1.2, edgecolor=color, facecolor="white")
    ax.add_patch(b)
    ax.text(x + w / 2.0, y + h / 2.0, text, ha="center", va="center", fontsize=fs, color=COLORS["dark"])
    return b


def arrow(ax, x1, y1, x2, y2, color, label=None, dy=1.8):
    a = FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=9,
                        linewidth=1.1, color=color, shrinkA=0, shrinkB=0)
    ax.add_patch(a)
    if label:
        ax.text((x1 + x2) / 2.0, (y1 + y2) / 2.0 + dy, label, ha="center", fontsize=7.0, color=color)


box(ax1, 2, 24, 20, 11, "PWM0 计数器 1\nCMP4/CMP5  (CH2)", COLORS["blue"])
box(ax1, 2, 8, 20, 11, "PWM0 计数器 1\nCMP6/CMP7  (CH3)", COLORS["blue"])
box(ax1, 31, 24, 17, 11, "PD02  (I2C2_SCL)\nPWM0_P_2", COLORS["orange"])
box(ax1, 31, 8, 17, 11, "PD03  (I2C2_SDA)\nPWM0_P_3", COLORS["orange"])
box(ax1, 57, 24, 17, 11, "PD07  (I2C3_SCL)\nTRGM_P_07", COLORS["green"])
box(ax1, 57, 8, 17, 11, "PD06  (I2C3_SDA)\nTRGM_P_06", COLORS["green"])
box(ax1, 81, 24, 17, 11, "TRGM0\n-> GPTMR2_CAPT_3\n(Icu ch1)", COLORS["purple"])
box(ax1, 81, 8, 17, 11, "TRGM0\n-> GPTMR2_CAPT_2\n(Icu ch0)", COLORS["purple"])

for y in (29.5, 13.5):
    arrow(ax1, 22.5, y, 30.5, y, COLORS["blue"])
    arrow(ax1, 48.5, y, 56.5, y, COLORS["orange"], "板载杜邦线" if y > 20 else None)
    arrow(ax1, 74.5, y, 80.5, y, COLORS["green"], "TRGM 内部" if y > 20 else None)

ax1.text(50, 37.5, "(a) PWM 输出 -> 板载杜邦线 -> TRGM -> GPTMR2 硬件捕获（真实引脚测量）",
         ha="center", fontsize=8.6, color=COLORS["dark"])

# ---------------- (b) 波形与捕获 ----------------
# 只画 3 个周期，便于标清 CAPPRD / CAPDTY
T = 0.2                                        # 周期 200 us (5 kHz 示例)
phase = 0.05                                   # 相位 25%
high = 0.10                                    # 50% 占空比
t = [0.002 * i for i in range(0, 330)]         # 0..0.658 ms


def pwm_level(tt):
    pp = (tt - phase) % T
    return 1.0 if pp < high else 0.0


y = [pwm_level(tt) for tt in t]
ax2.plot(t, y, color=COLORS["blue"], linewidth=1.5, label="PWM 输出（相位 25%，占空比 50%）")
ax2.fill_between(t, 0, y, color=COLORS["blue"], alpha=0.12)

rise = [phase + k * T for k in range(0, 4)]
fall = [phase + high + k * T for k in range(0, 4)]

# 周期：rise[1] -> rise[2] 标一次
ax2.annotate("", xy=(rise[1], 1.32), xytext=(rise[2], 1.32),
             arrowprops=dict(arrowstyle="<->", color=COLORS["red"], linewidth=1.1))
ax2.text((rise[1] + rise[2]) / 2.0, 1.38, "CAPPRD = 周期 tick", ha="center", fontsize=7.4,
         color=COLORS["red"])
# 高电平：rise[1] -> fall[1]
ax2.annotate("", xy=(rise[1], 0.72), xytext=(fall[1], 0.72),
             arrowprops=dict(arrowstyle="<->", color=COLORS["green"], linewidth=1.1))
ax2.text((rise[1] + fall[1]) / 2.0, 0.54, "CAPDTY = 高电平 tick", ha="center", fontsize=7.4,
         color=COLORS["green"])
# 相位
ax2.annotate("", xy=(0.0, 1.10), xytext=(rise[0], 1.10),
             arrowprops=dict(arrowstyle="<->", color=COLORS["orange"], linewidth=1.1))
ax2.text(rise[0] / 2.0, 1.14, "相位 a", ha="center", fontsize=7.0, color=COLORS["orange"])
ax2.axvline(rise[0], color=COLORS["orange"], linewidth=0.8, linestyle="--")

ax2.set_xlabel("时间 (ms)")
ax2.set_ylabel("电平")
ax2.set_xlim(-0.01, 0.63)
ax2.set_ylim(-0.3, 1.55)
ax2.set_yticks([0, 1])
ax2.set_yticklabels(["低", "高"])
ax2.grid(True, axis="x", alpha=0.25)
ax2.legend(loc="lower right", fontsize=7.2, frameon=False)
ax2.set_title("(b) GPTMR measure_width 模式：CAPPRD/CAPDTY 由硬件自动填写（RM §15.4）", fontsize=8.6)

fig.tight_layout()
save(fig, "45-mcal-timer", "fig-45-1-timer-chain-and-capture-timing")
