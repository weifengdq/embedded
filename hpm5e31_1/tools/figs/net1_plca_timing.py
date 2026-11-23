#!/usr/bin/env python3
"""net1_plca_timing.py —— 图 11-x: PLCA 循环（BEACON / TO 定时器 / burst 模式）

依据: OPEN Alliance TC14 (10BASE-T1S PLCA) 定义, 寄存器映射见
      mdio-open-alliance.h (MDIO_OATC14_PLCA_*);
      LAN8650/1 DS60001734E §7 "PLCA"; 参数单位见
      drivers/lan865x/mdio-open-alliance.h。
说明: 图中时间为"PLCA 位时间"归一化示意, 不代表某一具体 to_tmr 的绝对时间;
      实测 to_tmr/burst 组合的吞吐见 docs/chapters/11-lan8651-plca-lwip.md 第 7 节。
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle, FancyArrowPatch  # noqa: E402


def main():
    setup()
    fig, ax = plt.subplots(figsize=(9.0, 4.2))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 10)
    ax.axis("off")

    blue, orange, green, purple, gray = (COLORS["blue"], COLORS["orange"], COLORS["green"],
                                        COLORS["purple"], COLORS["gray"])

    ax.annotate("", xy=(98, 8.6), xytext=(2, 8.6),
                arrowprops=dict(arrowstyle="->", color=gray, lw=1.0))
    ax.text(98.5, 8.6, "时间", fontsize=9, color=gray, va="center")

    # BEACON
    ax.add_patch(Rectangle((2, 7.4), 6, 0.9, facecolor=blue, edgecolor="none"))
    ax.text(5, 7.85, "BEACON", ha="center", va="center", color="white", fontsize=8.5)
    ax.text(5, 7.1, "节点 0 (协调者)", ha="center", fontsize=8, color=blue)

    # TO timer windows
    for i, (x0, w) in enumerate([(8, 10), (30, 10), (52, 10), (74, 10)]):
        ax.add_patch(Rectangle((x0, 6.6), w, 0.5, facecolor="#F6F8FA", edgecolor=gray,
                               linestyle="--", linewidth=0.7))
        ax.text(x0 + w / 2, 6.85, "TO 定时器 (to_tmr)", ha="center", va="center",
                fontsize=7.8, color=gray)

    # node 1..3 transmission opportunities
    slots = [(12, 6, "节点 1"), (34, 6, "节点 2"), (56, 6, "节点 3"), (78, 8, "节点 1 (burst)")]
    for x0, w, name in slots:
        ax.add_patch(Rectangle((x0, 4.9), w, 0.9, facecolor=green, alpha=0.85, edgecolor="none"))
        ax.text(x0 + w / 2, 5.35, name, ha="center", va="center", color="white", fontsize=8.5)

    # burst mode explanation
    ax.add_patch(Rectangle((78, 4.9), 4, 0.9, facecolor=purple, edgecolor="none"))
    ax.add_patch(Rectangle((82, 4.9), 4, 0.9, facecolor=purple, edgecolor="none"))
    ax.text(84, 4.35, "burst_cnt>0: 同一节点连续发多个帧\n(burst_tmr 控制相邻帧间隔)",
            ha="center", fontsize=8, color=purple)

    ax.text(2, 3.55, "规则（本仓库实现的寄存器语义）", fontsize=10, fontweight="bold", color=COLORS["dark"])
    lines = [
        "1. 节点 0 = 协调者，每个 PLCA 循环开始时广播 BEACON，所有节点以它为同步基准；",
        "2. 循环开始后各节点按 node_id 顺序获得发送机会，等待一个 to_tmr 后才能发；",
        "3. node_cnt 必须两端一致（本仓库默认 8），node_id 不能冲突；",
        "4. burst_cnt>0 时同一节点可连续发送多个帧，burst_tmr 为帧间隔（单位 20 bit 时间）；",
        "5. 无 PLCA（CSMA/CD）时多点段冲突概率高，实测吞吐会明显低于 PLCA（见第 7 节）。",
    ]
    y = 3.0
    for t in lines:
        ax.text(2, y, t, fontsize=8.6, color="#24292F", va="top")
        y -= 0.52

    ax.text(2, 9.55, "PLCA (Physical Layer Collision Avoidance) 循环结构", fontsize=12,
            fontweight="bold", color=COLORS["dark"])
    save(fig, 11, "fig-11-1-plca-cycle")


if __name__ == "__main__":
    main()
