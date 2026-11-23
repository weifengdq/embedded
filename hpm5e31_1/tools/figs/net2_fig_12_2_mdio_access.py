#!/usr/bin/env python3
"""tools/figs/net2_fig_12_2_mdio_access.py —— 图 12-2：MDIO Clause 22 帧 与 YT8011 三种寄存器访问路径

数据来源：
  * IEEE 802.3 Clause 22 管理帧格式（YT8011Ax_应用说明_V2.2.pdf §2.1.3 p.11 的 frame 表）
  * EXT 寄存器访问：应用说明 §2.1.2（p.10）"将 ext_reg 地址写入 mii_reg 0x1e，读写 0x1f"
  * MMD 间接访问：应用说明 §2.1.3（p.11）"0x0D=devad → 0x0E=reg → 0x0D=0x4000+devad → 0x0E=data"
输出：docs/figures/12/fig-12-2-mdio-access.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import setup, save, COLORS  # noqa: E402

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches


def draw_frame(ax):
    fields = [
        ("PRE\n32 bit", 32, COLORS["gray"]),
        ("ST\n2", 2, COLORS["blue"]),
        ("OP\n2", 2, COLORS["blue"]),
        ("PHYAD\n5", 5, COLORS["green"]),
        ("REGAD\n5", 5, COLORS["green"]),
        ("TA\n2", 2, COLORS["orange"]),
        ("DATA\n16", 16, COLORS["purple"]),
        ("IDLE", 3, COLORS["light"]),
    ]
    x = 0
    for name, width, color in fields:
        ax.add_patch(mpatches.Rectangle((x, 0), width, 1.0, facecolor=color,
                                        edgecolor="white", alpha=0.85 if width > 3 else 0.95))
        ax.text(x + width / 2, 0.5, name, ha="center", va="center",
                fontsize=8.5 if width > 3 else 7.2,
                color="white" if color != COLORS["light"] else COLORS["dark"],
                fontweight="bold")
        x += width
    ax.annotate("读: OP=10, TA=Z0\n写: OP=01, TA=10",
                xy=(32 + 2 + 1, 1.02), xytext=(36, 1.55),
                fontsize=8.5, color=COLORS["dark"],
                arrowprops=dict(arrowstyle="->", color=COLORS["gray"], lw=0.9))
    ax.set_xlim(0, x)
    ax.set_ylim(-0.15, 1.85)
    ax.set_yticks([])
    ax.set_xlabel("MDIO 位序（先发 PRE，再发 ST/OP/PHYAD/REGAD/TA/DATA）")
    ax.set_title("(a) Clause 22 管理帧（ENET0 \u2192 MDC，MDC \u2248 1.5~2.4 MHz）",
                 loc="left", fontweight="bold", color=COLORS["dark"])
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)


def draw_paths(ax):
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 7.2)
    ax.axis("off")
    ax.set_title("(b) YT8011AN 三种寄存器空间的访问路径（实测均通过）",
                 loc="left", fontweight="bold", color=COLORS["dark"])
    boxes = {
        "mii": (0.2, 4.6, "Clause 22 标准寄存器\n0x00~0x1F\nBMCR/BMSR/PHYSR…"),
        "ext": (0.2, 2.4, "EXT 寄存器\n0x0000~0x9xxx\n0x9001 RGMII_CFG1"),
        "mmd": (0.2, 0.2, "MMD 寄存器\n(clause 45 语义)\nMMD1 0x834 / MMD7 0x200"),
    }
    for k, (x, y, txt) in boxes.items():
        ax.add_patch(mpatches.FancyBboxPatch((x, y), 3.3, 1.5, boxstyle="round,pad=0.08",
                                            facecolor=COLORS["light"], edgecolor=COLORS["gray"]))
        ax.text(x + 1.65, y + 0.75, txt, ha="center", va="center", fontsize=8.2,
                color=COLORS["dark"])
    seq = [
        ("直接读写", "mii 0x00~0x1F", 4.6),
        ("写 mii 0x1E = ext 地址\n读/写 mii 0x1F = 数据", "EXT", 2.4),
        ("0x0D=devad → 0x0E=reg\n0x0D=0x4000+devad → 0x0E=data", "MMD", 0.2),
    ]
    for txt, tag, y in seq:
        ax.annotate("", xy=(6.4, y + 0.95), xytext=(3.6, y + 0.75),
                    arrowprops=dict(arrowstyle="->", color=COLORS["blue"], lw=1.6))
        ax.text(4.05, y + 1.15, txt, fontsize=8.0, color=COLORS["blue"])
    ax.add_patch(mpatches.FancyBboxPatch((6.4, 0.9), 3.3, 4.4, boxstyle="round,pad=0.1",
                                        facecolor="white", edgecolor=COLORS["blue"], lw=1.4))
    ax.text(8.05, 4.6, "ENET0 GMII_ADDR/GMII_DATA", ha="center", fontsize=8.6,
            fontweight="bold", color=COLORS["blue"])
    ax.text(8.05, 3.6, "PA=1 (PHYADDR)\nGR=寄存器号\nCR=MDC 分频(fixed div 102)\nGW=读/写\nGB=忙等待",
            ha="center", va="center", fontsize=7.8, color=COLORS["dark"])
    ax.text(8.05, 1.9, "hpm_enet_drv.c:\nenet_read_phy()/enet_write_phy()",
            ha="center", va="center", fontsize=7.8, color=COLORS["gray"])


def main():
    setup(font_size=9.0)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9.6, 7.6),
                                   gridspec_kw={"height_ratios": [1.0, 2.4]})
    draw_frame(ax1)
    draw_paths(ax2)
    fig.tight_layout(h_pad=1.4)
    save(fig, 12, "fig-12-2-mdio-access")


if __name__ == "__main__":
    main()
