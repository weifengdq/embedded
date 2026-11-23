#!/usr/bin/env python3
"""tools/figs/mcal_infra.py —— 第 43 章图：配置三阶段 / 构建与上板链 / 初始化顺序"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, render_dot, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch


def _box(ax, x, y, w, h, title, body, color):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                                facecolor=color, edgecolor="#57606A", lw=0.9, transform=ax.transAxes))
    ax.text(x + 0.015, y + h - 0.055, title, fontsize=10.5, fontweight="bold",
            color=COLORS["dark"], transform=ax.transAxes)
    ax.text(x + 0.015, y + h - 0.14, body, fontsize=8.8, color=COLORS["dark"],
            transform=ax.transAxes, va="top", linespacing=1.5)


def fig_config():
    fig, ax = plt.subplots(figsize=(7.6, 4.2))
    ax.axis("off")
    _box(ax, 0.02, 0.55, 0.30, 0.36, "① Pre-compile（编译期）",
         "mcal/<Mod>/cfg/<Mod>_Cfg.h\n模块开关 / 通道数 / DEV_ERROR_DETECT\n改配置 → 重新编译", "#EAF2FF")
    _box(ax, 0.35, 0.55, 0.30, 0.36, "② Link-time（链接期）",
         "本工程未单独实现\n（说明见 43.3，保留扩展点）", "#F6F8FA")
    _box(ax, 0.68, 0.55, 0.30, 0.36, "③ Post-build（运行前）",
         "mcal/<Mod>/cfg/<Mod>_PBcfg.c\nconst <Mod>_ConfigType <Mod>_Config\n引脚/实例/回调/时序", "#E6F4EA")
    ax.annotate("", xy=(0.35, 0.73), xytext=(0.32, 0.73), xycoords="axes fraction",
                arrowprops=dict(arrowstyle="->", color="#57606A", lw=1.0))
    ax.annotate("", xy=(0.68, 0.73), xytext=(0.65, 0.73), xycoords="axes fraction",
                arrowprops=dict(arrowstyle="->", color="#57606A", lw=1.0))
    _box(ax, 0.10, 0.10, 0.34, 0.28, "编译产物",
         "libhpm_sdk_lib.a + mcal/*.o\n→ output/demo.elf", "#FFF4E5")
    _box(ax, 0.55, 0.10, 0.34, 0.28, "运行时",
         "<Mod>_Init(&<Mod>_Config) → 把配置写进寄存器\n换板只换 PBcfg，不改驱动", "#E6F4EA")
    ax.annotate("", xy=(0.55, 0.24), xytext=(0.44, 0.24), xycoords="axes fraction",
                arrowprops=dict(arrowstyle="->", color="#57606A", lw=1.0))
    save(fig, 43, "fig-43-1-config-stages")


def fig_build():
    dot = r"""digraph G {
  rankdir=LR; nodesep=0.20; ranksep=0.25; pad=0.10;
  graph [fontname="Noto Sans CJK SC", bgcolor="white"];
  node [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=9.5,
        color="#57606A", fillcolor="#F6F8FA"];
  edge [color="#57606A", arrowsize=0.7];
  cm [label="例程\nCMakeLists.txt"];
  mc [label="mcal.cmake\nmcal_add(Mcu Port …)", fillcolor="#EAF2FF"];
  sdk [label="hpm-sdk\n(cmake-ext)"];
  gcc [label="riscv32-unknown-elf-gcc\n11.1.0 -O2"];
  elf [label="output/demo.elf"];
  jl [label="J-Link\n(4 MHz)"];
  bd [label="hpm5e31_1 板", fillcolor="#E6F4EA"];
  se [label="CH343\n/dev/ttyACM0", ];
  sc [label="serial_cap.py"];
  lg [label="logs/*.log"];
  dc [label="docs/chapters\n+ PDF"];
  cm -> mc; mc -> sdk; sdk -> gcc; gcc -> elf; elf -> jl; jl -> bd;
  bd -> se -> sc -> lg -> dc;
}"""
    render_dot(dot, 44, "fig-43-2-build-chain")


def fig_init():
    dot = r"""digraph G {
  rankdir=LR; nodesep=0.18; ranksep=0.22; pad=0.10;
  graph [fontname="Noto Sans CJK SC", bgcolor="white"];
  node [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=9,
        color="#57606A", fillcolor="#F6F8FA"];
  edge [color="#57606A", arrowsize=0.7];
  a [label="bsp_init()\nBSP 时钟/串口"];
  b [label="Det_Init()"];
  c [label="Mcu_Init()\nMcu_InitClock()\nMcu_DistributePllClock()", fillcolor="#EAF2FF"];
  d [label="Port_Init()"];
  e [label="Gpt/Pwm/Adc/Spi\nI2c/Can/Mem 各自 Init()", fillcolor="#E6F4EA"];
  f [label="Wdg_Init()\n+ 周期 trigger"];
  g [label="主循环\nletter-shell / 1 ms 任务"];
  h [label="Wdg_SetTriggerCondition()\n喂狗", fillcolor="#FFF4E5"];
  a -> b -> c -> d -> e -> f -> g;
  g -> h [style=dashed, label="每轮", fontsize=8];
  h -> g [style=dashed];
}"""
    render_dot(dot, 44, "fig-43-3-init-sequence")


def main():
    setup()
    fig_config()
    fig_build()
    fig_init()


if __name__ == "__main__":
    main()
