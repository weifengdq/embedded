#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第 50 章 图 50-2：SWS API 覆盖热力图（读 docs/mcal/traceability_stats.json）。

输出 docs/figures/50-mcal-compliance/fig-50-2-api-coverage.png
"""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
STATS = ROOT / "docs" / "mcal" / "traceability_stats.json"

MODULES = ["Mcu", "Port", "Dio", "Gpt", "Pwm", "Icu", "Wdg", "Adc", "Spi", "I2c", "Can", "Mem", "Crc"]
GROUPS = ["初始化/去初始化", "时钟/复位/模式/低功耗", "数据通路/作业", "状态/查询",
          "通知/中断/唤醒控制", "版本信息", "调度函数"]


def main():
    data = json.loads(STATS.read_text(encoding="utf-8"))
    mat = np.full((len(MODULES), len(GROUPS) + 1), np.nan)
    txt = [["" for _ in range(len(GROUPS) + 1)] for _ in MODULES]
    for i, m in enumerate(MODULES):
        ms = data["modules"].get(m, {})
        for j, g in enumerate(GROUPS):
            gd = ms.get("groups", {}).get(g)
            if not gd:
                txt[i][j] = "–"
                continue
            mat[i, j] = gd["coverage"]
            txt[i][j] = "%d/%d" % (gd["implemented"], gd["total"])
        tot = ms.get("sws_total", 0)
        mat[i, -1] = ms.get("coverage", 0.0)
        txt[i][-1] = "%d/%d" % (ms.get("implemented", 0) + ms.get("partial", 0), tot)

    setup(10)
    fig, ax = plt.subplots(figsize=(9.2, 5.4))
    cmap = plt.get_cmap("RdYlGn").copy()
    cmap.set_bad("#E5E7EB")
    im = ax.imshow(np.ma.masked_invalid(mat), cmap=cmap, vmin=0.0, vmax=1.0, aspect="auto")
    ax.set_xticks(range(len(GROUPS) + 1))
    ax.set_xticklabels(GROUPS + ["总计"], rotation=28, ha="right")
    ax.set_yticks(range(len(MODULES)))
    ax.set_yticklabels(MODULES)
    ax.set_xticks(np.arange(-0.5, len(GROUPS) + 1, 1), minor=True)
    ax.set_yticks(np.arange(-0.5, len(MODULES), 1), minor=True)
    ax.grid(which="minor", color="white", linewidth=1.2)
    ax.tick_params(which="minor", length=0)
    for i in range(len(MODULES)):
        for j in range(len(GROUPS) + 1):
            v = mat[i, j]
            color = "white" if (v == v and v < 0.45) else COLORS["dark"]
            ax.text(j, i, txt[i][j], ha="center", va="center", fontsize=8, color=color)
    cbar = fig.colorbar(im, ax=ax, fraction=0.025, pad=0.02)
    cbar.set_label("覆盖率（已实现+0.5×部分）/SWS API 数")
    ax.set_title("SWS API 覆盖率（行=模块，列=API 组；格子=已实现/SWS API 数）",
                 loc="left", fontsize=11, color=COLORS["dark"], pad=8)
    save(fig, "50-mcal-compliance", "fig-50-2-api-coverage")


if __name__ == "__main__":
    main()