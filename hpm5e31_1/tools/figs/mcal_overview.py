#!/usr/bin/env python3
"""tools/figs/mcal_overview.py —— 第 42 章图：AUTOSAR 分层 / 模块-资源映射 / 开发闭环"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, render_dot, COLORS
import matplotlib.pyplot as plt




def fig_layers():
    """AUTOSAR CP 分层 + 本书实现的 15 个 MCAL 模块（matplotlib 手工布局，保证层次清晰）。"""
    from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
    fig, ax = plt.subplots(figsize=(7.4, 5.7))
    ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
    C = COLORS
    N = "#57606A"

    def layer(y, h, label, edge=N, fc="#FFFFFF", lw=1.0):
        ax.add_patch(FancyBboxPatch((0.02, y), 0.96, h, boxstyle="round,pad=0.004,rounding_size=0.012",
                                    linewidth=lw, edgecolor=edge, facecolor=fc, zorder=1))
        ax.text(0.035, y + h - 0.020, label, fontsize=9.0, color=C["dark"], fontweight="bold",
                va="top", ha="left", zorder=2)

    def chip(x, y, w, h, name, fc="#E6F4EA", fs=8.4):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.003,rounding_size=0.008",
                                    linewidth=0.9, edgecolor="#8B949E", facecolor=fc, zorder=3))
        ax.text(x + w / 2, y + h / 2, name, fontsize=fs, color=C["dark"], ha="center", va="center", zorder=4)

    def arrow(x1, y1, x2, y2, color=N, ls="-", lw=1.0):
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=9,
                                     linewidth=lw, color=color, linestyle=ls, zorder=2,
                                     shrinkA=1, shrinkB=1))

    # ---- 应用层 ----
    layer(0.862, 0.123, "应用层（第 28~41 章）—— Application SWC / 例程主循环", fc="#EAF2FF")
    ax.text(0.5, 0.903, "ISO-TP / UDS / DoIP / J1939 / CANopen / XCP / SOME/IP / XRCE-DDS / MQTT …",
            fontsize=8.4, color=C["dark"], ha="center", va="center")

    # ---- RTE ----
    layer(0.730, 0.105, "RTE（运行时环境）", fc="#F6F8FA")
    ax.text(0.5, 0.762, "RTE / OS —— 本仓库无 OS，用裸机主循环 + letter-shell 轮询代替",
            fontsize=8.4, color=C["dark"], ha="center", va="center")

    # ---- BSW ----
    layer(0.585, 0.110, "BSW 服务层 & ECU 抽象层", fc="#FFFFFF")
    chip(0.05, 0.598, 0.43, 0.048, "CanIf / Com / Dcm / Dem / Det（仅桩）", fc="#FFF4E5", fs=7.9)
    chip(0.52, 0.598, 0.43, 0.048, "IoHwAb / MemIf / Fee / EthIf（未实现，仅对比）", fc="#FFF4E5", fs=7.9)

    # ---- MCAL ----
    layer(0.200, 0.330, "MCAL —— 本书第 42~50 章实现的部分（15 个模块）", edge=C["blue"], lw=1.5, fc="#FFFFFF")
    row1 = ["Mcu", "Port", "Dio", "Gpt", "Pwm", "Icu", "Adc", "Spi"]
    row2 = ["I2c", "Can", "Wdg", "Mem", "Crc", "Eth", "EthTrcv"]
    w, h, gap = 0.0985, 0.070, 0.012
    x0 = 0.055
    for i, name in enumerate(row1):
        chip(x0 + i * (w + gap), 0.375, w, h, name)
    for i, name in enumerate(row2):
        special = name in ("Eth", "EthTrcv")
        chip(x0 + i * (w + gap), 0.255, w, h, name, fc="#DDF4E4" if special else "#E6F4EA")
    arrow(x0 + 5 * (w + gap) + w, 0.290, x0 + 6 * (w + gap), 0.290, color=C["green"], ls="--")
    ax.text(0.965, 0.212, "Eth/EthTrcv 为第 7 轮新增（第 49 章）", fontsize=7.6, color=C["green"],
            ha="right", va="bottom")

    # ---- 硬件 ----
    layer(0.020, 0.125, "HPM5E31 硬件（480 MHz RV32 / 1 MB Flash / 544 KB SRAM）", fc="#F6F8FA")
    ax.text(0.5, 0.058, "SYSCTL · IOC · GPIO · GPTMR · PWM · ADC16 · SPI · I2C",
            fontsize=7.6, color=C["dark"], ha="center", va="center")
    ax.text(0.5, 0.032, "MCAN ×4 · ENET0 + YT8011AN · EWDG · XPI NOR",
            fontsize=7.6, color=C["dark"], ha="center", va="center")

    # ---- 层间箭头 ----
    arrow(0.5, 0.862, 0.5, 0.837)
    arrow(0.5, 0.730, 0.5, 0.697)
    arrow(0.265, 0.585, 0.265, 0.532)
    arrow(0.735, 0.585, 0.735, 0.532, ls="--")
    arrow(0.30, 0.200, 0.30, 0.147, color=C["green"], lw=1.2)
    arrow(0.70, 0.200, 0.70, 0.147, color=C["green"], lw=1.2)

    save(fig, 42, "fig-42-1-autosar-layers")


def fig_mapping():
    rows = [
        ("Mcu", "SYSCTL / PLL / PPOR", "24 MHz XTAL → 480 MHz", "44"),
        ("Port", "IOC 复用/上下拉/开漏", "72 GPIO / 板载 4 组复用", "44"),
        ("Dio", "GPIO 输入/输出", "USR LED/BTN + 8 颗 CAN 灯", "44"),
        ("Gpt", "GPTMR0/1", "1 ms tick / oneshot", "45"),
        ("Pwm", "PWM0 4 通道", "占空比/周期/相位", "45"),
        ("Icu", "GPTMR2 捕获", "SPI 杜邦线回环信号", "45"),
        ("Adc", "ADC0 (ADC16)", "PF26 / 16 bit", "46"),
        ("Spi", "SPI1 主 ↔ SPI3 从", "杜邦线互连 10 MHz", "46"),
        ("I2c", "I2C2 主 ↔ I2C3 从", "杜邦线互连 100k~1M", "46"),
        ("Can", "MCAN0~3", "CAN0↔CAN1、CAN2↔CAN3", "47"),
        ("Wdg", "EWDG0", "窗口/超时复位", "45"),
        ("Mem", "XPI0 片内 Flash", "0x800F0000 保留扇区", "48"),
        ("Crc", "HPM CRC 外设 + 软件", "8/16/32/64 标准向量", "48"),
        ("Eth", "ENET0 MAC + DMA", "RGMII ↔ YT8011AN，lwIP", "50"),
        ("EthTrcv", "MDIO/MDC（PA30/PA31）", "YT8011AN 链路/速率/主从", "50"),
    ]
    fig, ax = plt.subplots(figsize=(7.6, 5.4))
    ax.axis("off")
    col_x = [0.02, 0.14, 0.30, 0.72, 0.90]
    headers = ["模块", "HPM5E31 外设", "板级资源/用法", "章"]
    for x, h in zip(col_x, headers):
        ax.text(x, 0.97, h, fontsize=10.5, fontweight="bold", color=COLORS["dark"], transform=ax.transAxes)
    ax.plot([0.02, 0.98], [0.945, 0.945], color="#57606A", lw=0.8, transform=ax.transAxes)
    for i, (m, ip, res, ch) in enumerate(rows):
        y = 0.90 - i * 0.068
        if i % 2 == 0:
            ax.add_patch(plt.Rectangle((0.02, y - 0.026), 0.96, 0.058, transform=ax.transAxes,
                                       facecolor="#F6F8FA", edgecolor="none", zorder=0))
        ax.text(col_x[0], y, m, fontsize=10, color=COLORS["blue"], fontweight="bold", transform=ax.transAxes)
        ax.text(col_x[1], y, ip, fontsize=9.2, color=COLORS["dark"], transform=ax.transAxes)
        ax.text(col_x[2], y, res, fontsize=9.2, color=COLORS["dark"], transform=ax.transAxes)
        ax.text(col_x[3] + 0.03, y, ch, fontsize=9.2, color=COLORS["gray"], transform=ax.transAxes)
    save(fig, 42, "fig-42-2-module-mapping")


def fig_workflow():
    dot = r"""digraph G {
  rankdir=LR; nodesep=0.25; ranksep=0.30; pad=0.10;
  graph [fontname="Noto Sans CJK SC", bgcolor="white"];
  node [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=10,
        color="#57606A", fillcolor="#F6F8FA"];
  edge [color="#57606A", arrowsize=0.7];
  s1 [label="AUTOSAR SWS\n(R21-11/R25-11)"];
  s2 [label="API / 配置设计\n(include + cfg)"];
  s3 [label="寄存器级驱动\n(src/*.c)", fillcolor="#E6F4EA"];
  s4 [label="例程 + shell\n(samples/mcal)"];
  s5 [label="上板实测\nhwrun + logs/", fillcolor="#FFF4E5"];
  s6 [label="章节 + 图 + PDF\n(ch42~49, v1.3.0)", fillcolor="#EAF2FF"];
  s1 -> s2 -> s3 -> s4 -> s5 -> s6;
  s5 -> s3 [label="修 bug", style=dashed, fontsize=9];
  s6 -> s2 [label="补配置/补 API", style=dashed, fontsize=9];
}"""
    render_dot(dot, 43, "fig-42-3-workflow")


def main():
    setup()
    fig_layers()
    fig_mapping()
    fig_workflow()


if __name__ == "__main__":
    main()
