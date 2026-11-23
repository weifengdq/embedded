#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""第 50 章 图 50-3：13 个模块的驱动源码规模对比（本实现 / TC4x MCAL / S32K3 RTD）。

数据口径：
  * 本实现：mcal/<Mod>/{include,src,cfg}/*.{c,h} 行数（脚本运行时现场统计）；
  * Infineon AURIX_TC4x_MC-ISAR 2.20.0：MCAL/<Mod>/ssc/{inc,src} 行数
    （数据来自 2026-11-02 对 C:\AURIX_TC4x_MC-ISAR_2.20.0 的统计，见第 50 章表 49-x）；
  * NXP S32K3 RTD 6.0.0：<Mod>_TS_*/{include,src} 行数（同上）。
  三者都只统计驱动源码与公共头，不含示例、配置生成器插件、文档。

输出 docs/figures/50-mcal-compliance/fig-50-3-code-scale.png
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
MODULES = ["Mcu", "Port", "Dio", "Gpt", "Pwm", "Icu", "Wdg", "Adc", "Spi", "I2c", "Can", "Mem", "Crc"]
TC4X = {"Mcu": 9364, "Port": 2742, "Dio": 2164, "Gpt": 6691, "Pwm": 6775, "Icu": 8731,
        "Wdg": 1801, "Adc": 11568, "Spi": 10768, "I2c": 8874, "Can": 13736,
        "Mem": 16634, "Crc": 4059}
NXP = {"Mcu": 38063, "Port": 6251, "Dio": 3270, "Gpt": 17993, "Pwm": 24608, "Icu": 16387,
       "Wdg": 9367, "Adc": 36447, "Spi": 20693, "I2c": 14202, "Can": 22525,
       "Mem": 10834, "Crc": 7546}


def ours(mod):
    n = 0
    for p in (ROOT / "mcal" / mod).rglob("*"):
        if p.suffix in (".c", ".h"):
            n += len(p.read_text(encoding="utf-8", errors="replace").splitlines())
    return n


def main():
    ours_v = [ours(m) for m in MODULES]
    x = np.arange(len(MODULES))
    w = 0.27
    setup(10)
    fig, ax = plt.subplots(figsize=(11.0, 4.8))
    b1 = ax.bar(x - w, ours_v, w, label="本实现（HPM5E31 MCAL 子集）", color=COLORS["blue"])
    b2 = ax.bar(x, [TC4X[m] for m in MODULES], w, label="Infineon TC4x MCAL 2.20.0", color=COLORS["orange"])
    b3 = ax.bar(x + w, [NXP[m] for m in MODULES], w, label="NXP S32K3 RTD 6.0.0", color=COLORS["green"])
    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels(MODULES)
    ax.set_ylabel("驱动源码行数（对数轴）")
    ax.grid(True, axis="y", which="both", color="#D0D7DE", linewidth=0.6, linestyle="--", zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.legend(ncol=1, loc="upper right", frameon=False)
    for bars in (b1, b2, b3):
        for r in bars:
            h = r.get_height()
            if h > 0:
                ax.text(r.get_x() + r.get_width() / 2, h * 1.12, "%d" % h,
                        ha="center", va="bottom", fontsize=6.2, color=COLORS["dark"])
    ax.set_ylim(300, 120000)
    save(fig, "50-mcal-compliance", "fig-50-3-code-scale")


if __name__ == "__main__":
    main()