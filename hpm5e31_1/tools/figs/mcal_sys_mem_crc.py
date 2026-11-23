#!/usr/bin/env python3
"""tools/figs/mcal_sys_mem_crc.py —— 第 48 章（Mem/Crc）插图

生成 4 张图到 docs/figures/48-mcal-mem-crc/（300 dpi、白底、统一配色）：

  fig-48-1-mem-job-fsm      Mem 异步作业状态机与 MainFunction 分块推进
  fig-48-2-flash-window     片内 1MB Flash 布局与 Mem 保留窗口
  fig-48-3-crc-throughput   8 个 CRC 算法的 SW/HW 吞吐（实测）
  fig-48-4-module-footprint 13 个 MCAL 模块代码/静态 RAM 占用（.obj 统计）

数据来源：samples/mcal/mem_flash_rw、mem_bench、crc_bench、crc_integrity 的实测日志
（logs/20261102-140231-mem_flash_rw.log 等），模块尺寸来自 build/*/ 下 .obj 的
riscv32-unknown-elf-size 统计（脚本见第 48 章 §47.11）。

用法：
  cd hpm5e31_1 && python3 tools/figs/mcal_sys_mem_crc.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

CH = "48-mcal-mem-crc"


def fig_job_fsm():
    """Mem 作业状态机：API 受理 -> PENDING -> MainFunction 按单元推进 -> 结果。"""
    setup()
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.axis("off")

    def box(x, y, w, h, text, color):
        p = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.06,rounding_size=0.12",
                           linewidth=1.2, edgecolor=color, facecolor="white")
        ax.add_patch(p)
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=8.6, color=COLORS["dark"])

    def arrow(x1, y1, x2, y2, text, dy=0.18):
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=11,
                                     linewidth=1.1, color=COLORS["gray"]))
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + dy, text, ha="center", fontsize=7.6, color=COLORS["dark"])

    box(0.3, 4.4, 2.3, 1.1, "MEM_JOB_IDLE\n(无在途作业)", COLORS["gray"])
    box(3.4, 4.4, 3.0, 1.1, "MEM_JOB_PENDING\nMem_GetJobResult 返回 1", COLORS["orange"])
    box(7.2, 4.4, 2.4, 1.1, "MEM_JOB_OK (0)", COLORS["green"])
    box(7.2, 2.5, 2.4, 1.0, "MEM_JOB_FAILED (2)", COLORS["red"])
    box(7.2, 1.0, 2.4, 1.0, "MEM_INCONSISTENT (3)", COLORS["purple"])
    box(0.3, 2.3, 2.6, 1.5,
        "Mem_Read / Write /\nErase / BlankCheck\n只做参数检查 + 受理", COLORS["blue"])
    box(3.4, 2.3, 3.0, 1.5,
        "Mem_MainFunction()\n每调用推进 1 个单元：\n擦 1 扇区 / 编 1 页 /\n读 512B / 查 512B", COLORS["teal"])

    arrow(1.45, 4.4, 1.45, 3.8, "")
    arrow(2.9, 3.05, 3.4, 3.05, "E_OK 受理")
    arrow(4.9, 3.8, 4.9, 4.4, "作业未完成")
    arrow(6.4, 4.95, 7.2, 4.95, "成功")
    arrow(6.4, 4.6, 7.2, 3.05, "ROM API 失败")
    arrow(6.4, 2.9, 7.2, 1.55, "空白检查发现非空", dy=-0.22)
    ax.text(5.0, 5.85, "Mem 作业状态机（SWS_Mem_00066：硬件动作只在 MainFunction 里做）",
            ha="center", fontsize=9.4, color=COLORS["dark"], weight="bold")
    ax.text(0.3, 0.35,
            "实测单次 MainFunction 阻塞：擦 1.908 ms / 编程 0.978 ms / 读 0.020 ms（mem_bench）",
            fontsize=7.8, color=COLORS["gray"])
    save(fig, CH, "fig-48-1-mem-job-fsm")


def fig_flash_window():
    """片内 1MB Flash 布局：应用镜像 + Mem 保留窗口。"""
    setup()
    fig, ax = plt.subplots(figsize=(7.2, 2.9))
    ax.set_xlim(0, 16)
    ax.set_ylim(0, 4.6)
    ax.axis("off")

    # 整片 1MB：0x80000000 .. 0x80100000，按 64KB 一块共 16 块
    for i in range(16):
        ax.add_patch(plt.Rectangle((i, 2.4), 0.96, 1.1, facecolor=COLORS["light"],
                                   edgecolor=COLORS["gray"], linewidth=0.6))
    # 应用镜像 0x80000000 .. 0x80016718（约 92KB，占 2 块）
    ax.add_patch(plt.Rectangle((0, 2.4), 1.96, 1.1, facecolor=COLORS["blue"], alpha=0.75,
                               edgecolor=COLORS["blue"], linewidth=0.8))
    ax.text(0.98, 2.95, "应用镜像\n0x80000000..0x80016718\n(91928 B)", ha="center", va="center",
            fontsize=7.6, color="white", weight="bold")
    # 保留窗口：最后 64KB
    ax.add_patch(plt.Rectangle((15.0, 2.4), 0.96, 1.1, facecolor=COLORS["orange"], alpha=0.85,
                               edgecolor=COLORS["orange"], linewidth=0.8))
    ax.text(15.48, 3.55, "Mem 保留窗口\n0x800F0000..0x80100000 (64KB)", ha="center", fontsize=7.4,
            color=COLORS["orange"], weight="bold")
    ax.annotate("", xy=(15.5, 3.0), xytext=(15.0, 3.45),
                arrowprops=dict(arrowstyle="-|>", color=COLORS["orange"], linewidth=1.0))
    ax.text(0, 2.05, "0x80000000", fontsize=7.4, color=COLORS["gray"])
    ax.text(14.6, 2.05, "0x800F0000", fontsize=7.4, color=COLORS["gray"])
    ax.text(15.3, 2.05, "0x80100000", fontsize=7.4, color=COLORS["gray"])
    ax.text(7.5, 4.25, "片内 1MB XPI NOR 映射布局（Mem_PBcfg.c 配置的安全窗口）",
            ha="center", fontsize=9.4, color=COLORS["dark"], weight="bold")
    ax.text(7.5, 0.75,
            "Mem_Write / Mem_Erase / Mem_BlankCheck 只允许落在橙色窗口内；\n"
            "越界地址返回 E_NOT_OK 并报 MEM_E_PARAM_ADDRESS（memdet 实测 det+1）",
            ha="center", fontsize=8.0, color=COLORS["dark"])
    save(fig, CH, "fig-48-2-flash-window")


def fig_crc_throughput():
    """8 个 CRC 算法 SW/HW 吞吐（crc_bench 实测，4096B x 64）。"""
    setup()
    names = ["CRC8\nJ1850", "CRC8\nH2F", "CRC16\nCCITT", "CRC16\nARC",
             "CRC32\nEthernet", "CRC32\nP4", "CRC32\nP76", "CRC64\nECMA"]
    sw = [22.81, 22.81, 17.11, 19.16, 20.83, 20.83, 19.96, 17.75]
    hw = [60.20, 60.20, 60.20, 60.20, 60.21, 60.21, 60.21, 17.75]

    fig, ax = plt.subplots(figsize=(7.2, 3.9))
    x = range(len(names))
    w = 0.38
    b1 = ax.bar([i - w / 2 for i in x], sw, w, label="软件查表 (SW)", color=COLORS["blue"])
    b2 = ax.bar([i + w / 2 for i in x], hw, w, label="CRC 外设 (HW)", color=COLORS["orange"])
    for rect in list(b1) + list(b2):
        ax.text(rect.get_x() + rect.get_width() / 2, rect.get_height() + 0.9,
                f"{rect.get_height():.1f}", ha="center", fontsize=7.0, color=COLORS["dark"])
    ax.set_xticks(list(x))
    ax.set_xticklabels(names, fontsize=7.6)
    ax.set_ylabel("吞吐 (MB/s)", fontsize=8.6)
    ax.set_ylim(0, 72)
    ax.grid(axis="y", linestyle=":", linewidth=0.6, alpha=0.6)
    ax.legend(fontsize=8.2, frameon=False, loc="upper right")
    ax.set_title("CRC 吞吐：HW 约 60 MB/s（MMIO 受限），SW 17~23 MB/s；CRC64 无 HW",
                 fontsize=8.8, color=COLORS["dark"])
    ax.text(0.0, -0.30, "数据：logs/20261102-135730-crc_bench.log，CPU 480 MHz，4KB x 64 次，mcycle 计时",
            transform=ax.transAxes, fontsize=7.0, color=COLORS["gray"])
    fig.tight_layout()
    save(fig, CH, "fig-48-3-crc-throughput")


def fig_module_footprint():
    """15 个模块的 .text 与 data+bss（.obj 统计，15/15 已落地；与 mcal-modules.md 同源同口径）。

    口径：Flash = text + data，RAM = data + bss；合计 text 87134 B / data 51973 B / bss 2008 B，
    即 Flash 139107 B、RAM 53981 B。Eth 的 37 632 B 是 DMA 收发缓冲（.noncacheable），
    计入 .data 因而同时进 Flash 镜像与 RAM——这是"缓冲放非缓存段"的代价，正文有讨论。"""
    setup()
    mods = ["Mcu", "Port", "Dio", "Gpt", "Pwm", "Icu", "Adc", "Spi", "I2c",
            "Can", "Wdg", "Mem", "Crc", "Eth", "EthTrcv"]
    text = [3492, 2020, 1564, 5482, 6392, 6666, 4870, 9792, 4114, 11584, 1142, 4027, 12019, 9850, 4120]
    ram = [12, 5, 0, 53, 45, 93, 69, 4299, 93, 10728, 6, 553, 95, 37885, 45]

    fig, ax = plt.subplots(figsize=(7.6, 4.0))
    x = range(len(mods))
    w = 0.40
    ax.bar([i - w / 2 for i in x], text, w, label=".text（Flash，B）", color=COLORS["blue"])
    ax.bar([i + w / 2 for i in x], ram, w, label=".data+.bss（RAM，B）", color=COLORS["green"])
    for i, (t, r) in enumerate(zip(text, ram)):
        if t:
            ax.text(i - w / 2, t + 400, str(t), ha="center", fontsize=5.8, rotation=90,
                    color=COLORS["dark"])
        if r:
            ax.text(i + w / 2, r + 400, str(r), ha="center", fontsize=5.8, rotation=90,
                    color=COLORS["dark"])
    ax.set_xticks(list(x))
    ax.set_xticklabels(mods, fontsize=6.8, rotation=30, ha="right")
    ax.set_ylabel("字节", fontsize=8.6)
    ax.set_ylim(0, 44000)
    ax.grid(axis="y", linestyle=":", linewidth=0.6, alpha=0.6)
    ax.legend(fontsize=8.0, frameon=False, loc="upper left")
    ax.set_title("15 个 MCAL 模块代码/静态 RAM（.obj 统计）：text 合计 87134 B，RAM(data+bss) 合计 53981 B",
                 fontsize=8.0, color=COLORS["dark"])
    fig.tight_layout()
    save(fig, CH, "fig-48-4-module-footprint")


if __name__ == "__main__":
    fig_job_fsm()
    fig_flash_window()
    fig_crc_throughput()
    fig_module_footprint()
    print("figures written to docs/figures/48-mcal-mem-crc/")