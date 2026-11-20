#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_images.py -- 生成 docs/images/ 下的所有示意图 (PNG, 200 dpi).

用法 (在仓库根目录执行):
    python3 docs/images/gen_images.py            # 生成全部
    python3 docs/images/gen_images.py topology   # 只生成某一张

依赖: matplotlib >= 3.6 (需要逐字形字体回退), graphviz (dot) 用于流程图。
字体: 拉丁 DejaVu Sans + 中文 Noto Sans CJK (系统 .ttc), 无需联网。
所有图中数字均取自 docs/results/*.md，不在此脚本内编造实测值。
"""
import os
import shutil
import subprocess
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager, rcParams
from matplotlib.patches import FancyBboxPatch, Rectangle

# ---------------------------------------------------------------- 全局样式
LATIN = "DejaVu Sans"
CJK_CANDIDATES = [
    "Noto Sans CJK SC", "Noto Sans CJK JP", "Source Han Sans SC",
    "Droid Sans Fallback", "AR PL SungtiL GB", "WenQuanYi Zen Hei",
]
_avail = {f.name for f in font_manager.fontManager.ttflist}
CJK = next((c for c in CJK_CANDIDATES if c in _avail), LATIN)
if CJK == LATIN:
    print("WARN: 未找到中文字体, 图中中文可能显示为方块", file=sys.stderr)

# mpl>=3.6: font.family 给列表 = 逐字形回退 (拉丁用 DejaVu, 缺字落到 CJK 字体)
rcParams["font.family"] = [LATIN, CJK]
rcParams["figure.dpi"] = 200
rcParams["savefig.dpi"] = 200
rcParams["savefig.bbox"] = "tight"
rcParams["savefig.pad_inches"] = 0.10
rcParams["axes.unicode_minus"] = False

C_BG = "#ffffff"
C_FRAME = "#2b3a55"
C_MCU = "#dce9f7"
C_PHY = "#fde6c4"
C_HOST = "#e4f2e0"
C_BUS = "#b5241f"
C_TEXT = "#1b1b1b"
C_MUTED = "#5a6472"
C_ACC = "#0b6ea8"
C_WARN = "#b03000"
C_OK = "#1e6b32"

OUT = os.path.dirname(os.path.abspath(__file__))

# 实测数字 (来源: docs/results/2026-11-19_*.md 与 docs/07_performance.md §8)
PERF = [
    ("HPM5361   SPI1 25MHz+HDMA",  8.80, 10.00, "iperf2 下行 / bench 833fps@1500B"),
    ("TC387     QSPI2 25MHz+DMA",  8.80, 9.82, "iperf2 下行 / bench 9.72–9.85"),
    ("RK3506    SPI 25MHz+突发",   8.83, 9.43, "iperf2 下行 / UDP 灌包 9.43(丢包0%)"),
    ("S32K312  LPSPI0 15.14MHz",   6.26, None, "iperf2 下行 6.26 / 上行 7.45"),
    ("STM32H723 SPI1 25MHz+DMA",   9.07, 9.71, "iperf2 下行 9.07 / bench 9.71"),
    ("LAN8651 参考(旧工程)",        8.86, None, "非本工程，仅作量级对比"),
]

# 优化前后 TCP 下行（Mbit/s）：(平台, 优化前, 优化后, 增益说明)
BEFORE_AFTER = [
    ("HPM5361", 7.53, 8.80, "+17%"),
    ("TC387", 6.45, 8.80, "+36%"),
    ("RK3506", 3.63, 8.83, "+143%"),
    ("S32K312", 1.16, 6.26, "+440%"),
    ("STM32H723", 7.44, 9.07, "+22%"),
]


def _save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, facecolor=fig.get_facecolor())
    plt.close(fig)
    print("  -> %-26s %7.1f KB" % (name, os.path.getsize(path) / 1024.0))


def _box(ax, x, y, w, h, fc, ec=C_FRAME, lw=1.2, ls="-", r=0.05, z=2):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0,rounding_size=%s" % r,
        linewidth=lw, edgecolor=ec, facecolor=fc, linestyle=ls, zorder=z))


def _t(ax, x, y, s, size=10, color=C_TEXT, ha="center", va="center",
       weight="normal", style="normal", rot=0, z=6):
    return ax.text(x, y, s, fontsize=size, color=color, ha=ha, va=va,
                   fontweight=weight, style=style, rotation=rot, zorder=z)


def _canvas(w, h):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_xlim(0, w)
    ax.set_ylim(0, h)
    ax.axis("off")
    fig.patch.set_facecolor(C_BG)
    return fig, ax


# ============================================================ 1. 系统拓扑
def fig_topology():
    W, H = 14.0, 7.9
    fig, ax = _canvas(W, H)
    _t(ax, W / 2, 7.60, "图 1  s2500 单段 10BASE-T1S 总线拓扑：1 个 PLCA 协调者 + 5 个跟随者",
       13.5, C_FRAME, weight="bold")

    # name, chip, iface, ip, plca, phy1, phy2, sw, fill, linestyle
    nodes = [
        ("Ubuntu 主机", "22.04 / 24.04 LTS", "USB → enx001ec0d1c337",
         "192.168.10.10", "PLCA ID 0  (协调者)",
         "EVB-LAN8670-USB", "10BASE-T1S (LAN8670)", "iperf / ethtool PLCA", C_HOST, "--"),
        ("STM32H723", "Cortex-M7  550 MHz", "SPI1 / USART1",
         "192.168.10.11", "PLCA ID 1  (跟随者)",
         "T30HM1TS2500", "10BASE-T1S MAC-PHY", "letter-shell + lwIP", C_MCU, "-"),
        ("HPM5361", "RISC-V  480 MHz", "SPI1 / UART2",
         "192.168.10.12", "PLCA ID 2  (跟随者)",
         "T30HM1TS2500", "10BASE-T1S MAC-PHY", "letter-shell + lwIP", C_MCU, "-"),
        ("S32K312", "Cortex-M7  120 MHz", "LPSPI0 / LPUART0",
         "192.168.10.13", "PLCA ID 3  (跟随者)",
         "T30HM1TS2500", "10BASE-T1S MAC-PHY", "letter-shell + lwIP", C_MCU, "-"),
        ("TC387", "TriCore  300 MHz", "QSPI2 / ASCLIN0",
         "192.168.10.14", "PLCA ID 4  (跟随者)",
         "T30HM1TS2500", "10BASE-T1S MAC-PHY", "letter-shell + lwIP", C_MCU, "-"),
        ("RK3506", "Linux 6.1 / Cortex-A7", "SPI / UART 1.5 Mbit",
         "192.168.10.15", "PLCA ID 5  (跟随者)",
         "T30HM1TS2500", "10BASE-T1S MAC-PHY", "netdev + iperf", C_MCU, "-"),
    ]

    bw, bh, ytop = 1.98, 3.20, 3.30
    x0, step = 0.42, 2.235
    bus_y = 2.32

    # ---- 总线
    ax.plot([0.20, W - 0.20], [bus_y, bus_y], color=C_BUS, lw=2.6, zorder=3)
    ax.plot([0.20, W - 0.20], [bus_y - 0.13, bus_y - 0.13], color=C_BUS, lw=2.6, zorder=3)
    for xe in (0.20, W - 0.20):
        ax.plot([xe, xe], [bus_y - 0.13, bus_y], color=C_BUS, lw=2.6, zorder=3)

    for i, (name, chip, iface, ip, plca, phy1, phy2, sw, fc, ds) in enumerate(nodes):
        x = x0 + i * step
        _box(ax, x, ytop, bw, bh, fc, ls=ds, lw=1.8 if ds != "-" else 1.2)
        _t(ax, x + bw / 2, ytop + 2.88, name, 11.0, C_FRAME, weight="bold")
        _t(ax, x + bw / 2, ytop + 2.52, chip, 7.8, C_MUTED)
        _t(ax, x + bw / 2, ytop + 2.12, iface, 7.6, C_ACC)
        _t(ax, x + bw / 2, ytop + 1.75, ip, 8.4, C_TEXT)
        _t(ax, x + bw / 2, ytop + 1.38, plca, 7.6,
           C_WARN if i == 0 else C_ACC, weight="bold")
        _box(ax, x + 0.14, ytop + 0.14, bw - 0.28, 0.92, C_PHY, lw=1.0)
        _t(ax, x + bw / 2, ytop + 0.80, phy1, 8.2, C_FRAME, weight="bold")
        _t(ax, x + bw / 2, ytop + 0.42, phy2, 7.0, C_MUTED)
        # 引线
        ax.plot([x + bw / 2, x + bw / 2], [ytop, bus_y], color=C_BUS, lw=1.4, zorder=1)
        _t(ax, x + bw / 2, 2.96, sw, 7.4, C_MUTED)

    # 总线说明
    _t(ax, W / 2, 1.86,
       "10BASE-T1S 混合段 (mixing segment) — 10 Mbit/s 半双工 · PLCA 无冲突轮询 · NCNT=8 · TOTMR=32 BT",
       10.2, C_BUS, weight="bold")
    _t(ax, 0.24, 1.50, "◄ 100 Ω 端接（2×49.9 Ω + 4.7 nF）", 8.0, C_BUS, ha="left")
    _t(ax, W - 0.24, 1.50, "100 Ω 端接 ►", 8.0, C_BUS, ha="right")

    notes = [
        ("端接规则", "只有物理两端的两块板保留 R12/R13；中间节点必须拆除，否则 N 个 100 Ω 并联后差分阻抗过低。"),
        ("PLCA 规则", "全网只能有一个 ID=0 协调者；NCNT / TOTMR / 优先级模式必须全网一致；跟随者收不到 BEACON 时 PST=0 不能发送。"),
        ("地址规则", "板端优先使用 S2500 出厂唯一 MAC（MACID0/1）；MAC 过滤只比较 MAC 低 32 位（MADDRFILT.L）。"),
    ]
    for i, (k, v) in enumerate(notes):
        y = 1.08 - i * 0.34
        _t(ax, 0.30, y, "● " + k, 8.8, C_FRAME, ha="left", weight="bold")
        _t(ax, 1.60, y, v, 8.4, C_TEXT, ha="left")
    _save(fig, "topology.png")


# ============================================================ 2. 软件分层
def fig_sw_stack():
    W, H = 11.8, 7.2
    fig, ax = _canvas(W, H)
    _t(ax, W / 2, 6.95,
       "图 2  软件分层：同一套 common/s2500 驱动被 5 个平台复用", 13.0, C_FRAME, weight="bold")

    layers = [
        ("应用与服务", "ping / iperf(lwiperf) / udpecho / bench tx", "common/s2500/s2500_app.c", "#eaf3fb"),
        ("命 令 行", "letter-shell 3.2.4 + 统一命令集", "s2500 info|status|reg|plca|hdd|td|sqi|counters", "#e8f0e4"),
        ("网络协议栈", "lwIP 2.2.0  (NO_SYS=1)", "ARP / ICMP / UDP / TCP，胶水层 s2500_netif.c", "#fdf3e0"),
        ("S2500 驱动核心", "平台无关，只依赖 s2500_hal_t",
         "OA-SPI 事务 · chunk 重组 · FCS · PLCA · SQI · HDD · TC14/TC10/TC6 · DIO", "#fbe6e6"),
        ("移 植 层", "s2500_hal_t { spi_xfer, delay_us, millis, log }", "+ s2500_port_uart_* / reboot", "#efe8f6"),
        ("平台 HAL / 内核", "每平台各写一份",
         "STM32 HAL · HPM SDK · NXP RTD · iLLD · Linux SPI", "#e6e9ee"),
        ("硬 件", "SPI 模式 0 / MSB first / ≤25 MHz", "T30HM1TS2500 ← CS_N MISO SCLK MOSI IRQ_N RST_N", "#f2f2f2"),
    ]
    h, gap = 0.62, 0.14
    top = 6.52
    for i, (title, mid, right, color) in enumerate(layers):
        y = top - i * (h + gap) - h
        _box(ax, 0.40, y, W - 0.80, h, color, r=0.045)
        _t(ax, 0.62, y + h / 2, title, 9.8, C_FRAME, ha="left", weight="bold")
        _t(ax, 2.86, y + h / 2, mid, 7.9, C_MUTED, ha="left")
        _t(ax, 6.15, y + h / 2, right, 7.9, C_TEXT, ha="left")
        if i < len(layers) - 1:
            ax.annotate("", xy=(W / 2, y - gap + 0.015), xytext=(W / 2, y - 0.015),
                        arrowprops=dict(arrowstyle="-|>", color=C_MUTED, lw=1.3))

    _box(ax, 0.40, 0.52, W - 0.80, 0.50, "#fff7d6", lw=1.0, r=0.04)
    _t(ax, W / 2, 0.77,
       "铁律：改 common/s2500/ 必须保持平台无关（不 include 任何平台头文件），改完要确认 5 个平台都能编译",
       8.6, C_WARN, weight="bold")
    _t(ax, W / 2, 0.24,
       "上层（应用/命令行/协议栈）与下层（驱动核心/移植层）之间只有 s2500_hal_t 一个接缝 → 换平台只需实现 3~5 个函数",
       8.0, C_MUTED)
    _save(fig, "sw_stack.png")


# ============================================================ 3. 控制事务
def fig_spi_control():
    W, H = 12.6, 5.0
    fig, ax = _canvas(W, H)
    _t(ax, W / 2, 4.74,
       "图 3  OA-SPI 控制事务：一次 CS_N 内固定 12 字节（以读寄存器为例）", 12.6, C_FRAME, weight="bold")

    xl, bw_ = 1.72, 3.44
    y_mosi, y_miso, hh = 3.42, 2.62, 0.58

    mosi = [("控制头 HDR  4B", "#d6e6f6"), ("写数据  4B", "#eaf3e4"), ("填充  4B", "#f0f0f0")]
    miso = [("无效  4B (0x00000000)", "#ffffff"), ("回显控制头  4B", "#f6ecd6"), ("读回数据  4B", "#e6f2e6")]
    for i in range(3):
        x = xl + i * bw_
        ax.add_patch(Rectangle((x, y_mosi), bw_, hh, facecolor=mosi[i][1],
                               edgecolor=C_FRAME, lw=1.0, zorder=3))
        _t(ax, x + bw_ / 2, y_mosi + hh / 2, mosi[i][0], 8.2, C_TEXT)
        ax.add_patch(Rectangle((x, y_miso), bw_, hh, facecolor=miso[i][1],
                               edgecolor=C_FRAME, lw=1.0, zorder=3))
        _t(ax, x + bw_ / 2, y_miso + hh / 2, miso[i][0], 8.2, C_TEXT)
        _t(ax, x + bw_ / 2, y_mosi + hh + 0.20, "字节 %d-%d" % (i * 4, i * 4 + 3), 7.2, C_MUTED)

    _t(ax, xl - 0.12, y_mosi + hh / 2, "MOSI", 9.0, C_ACC, ha="right", weight="bold")
    _t(ax, xl - 0.12, y_miso + hh / 2, "MISO", 9.0, C_ACC, ha="right", weight="bold")

    # CS_N 波形
    xcs0, xcs1 = xl - 0.18, xl + 3 * bw_ + 0.18
    ax.plot([xcs0, xl, xl + 3 * bw_, xcs1],
            [2.14, 1.94, 1.94, 2.14], color=C_FRAME, lw=1.8, zorder=4)
    _t(ax, xl - 0.26, 2.14, "CS_N", 9.0, C_ACC, ha="right", weight="bold")
    _t(ax, (xl + xl + 3 * bw_) / 2, 1.70,
       "整个 12 字节事务期间 CS_N 必须保持低电平（每个事务单独一次拉低/释放）", 8.2, C_MUTED)

    notes = [
        "控制头 32 bit：bit31 DNC=0（控制事务）· bit29 WNR（1=写 / 0=读）· bit28 AID（1=地址不自增）· bit27:24 MMS（0/1/2/3/4/12）",
        "bit23:8 ADDR 寄存器地址 · bit7:1 LEN-1 连续寄存器数 · bit0 P 奇校验（覆盖 bit31:1）；LEN 最大 127",
        "MISO 第 2 组 4B 回显控制头 → 可自检奇校验错与 HDRB；第 3 组 4B 才是真正的读回数据",
    ]
    for i, n in enumerate(notes):
        _t(ax, 0.34, 1.20 - i * 0.31, "· " + n, 7.9, C_TEXT, ha="left")
    _save(fig, "spi_control_txn.png")


# ============================================================ 4. 数据事务
def fig_spi_data():
    W, H = 12.6, 5.2
    fig, ax = _canvas(W, H)
    _t(ax, W / 2, 4.94,
       "图 4  OA-SPI 数据块事务：4B 头 + 64B 负载  ⇄  64B 负载 + 4B 尾（单块 68 字节）",
       12.6, C_FRAME, weight="bold")

    x0 = 1.85
    wh, wp, wt, hh = 1.32, 7.60, 1.32, 0.62
    y_mosi, y_miso = 3.62, 2.62

    ax.add_patch(Rectangle((x0, y_mosi), wh, hh, facecolor="#d6e6f6", edgecolor=C_FRAME, lw=1.0, zorder=3))
    _t(ax, x0 + wh / 2, y_mosi + hh / 2, "数据头 4B", 8.2)
    ax.add_patch(Rectangle((x0 + wh, y_mosi), wp, hh, facecolor="#eaf3e4", edgecolor=C_FRAME, lw=1.0, zorder=3))
    _t(ax, x0 + wh + wp / 2, y_mosi + hh / 2, "负载 64 B（本块要发送的字节，SWO/EBO 标出有效范围）", 8.2)

    ax.add_patch(Rectangle((x0, y_miso), wp, hh, facecolor="#ffffff", edgecolor=C_FRAME, lw=1.0, zorder=3))
    _t(ax, x0 + wp / 2, y_miso + hh / 2, "负载 64 B（芯片回送本块收到的字节）", 8.2)
    ax.add_patch(Rectangle((x0 + wp, y_miso), wt, hh, facecolor="#fdf3e0", edgecolor=C_FRAME, lw=1.0, zorder=3))
    _t(ax, x0 + wp + wt / 2, y_miso + hh / 2, "数据尾 4B", 8.2)

    _t(ax, x0 - 0.14, y_mosi + hh / 2, "MOSI", 9.0, C_ACC, ha="right", weight="bold")
    _t(ax, x0 - 0.14, y_miso + hh / 2, "MISO", 9.0, C_ACC, ha="right", weight="bold")

    ax.annotate("", xy=(x0, 2.18), xytext=(x0 + wh + wp, 2.18),
                arrowprops=dict(arrowstyle="<->", color=C_MUTED, lw=1.0))
    _t(ax, x0 + (wh + wp) / 2, 1.98,
       "一次 CS_N = 68 字节  ≈ 32 µs @ 21 MHz；1500B 帧需 24 块 ≈ 770 µs（10BASE-T1S 线时间 1230 µs，余量 1.6×）",
       8.2, C_MUTED)

    notes = [
        "数据头：bit31 DNC=1 · bit29 NORX（本次不接收）· bit21 DV（数据有效）· bit20 SV（帧起始）· bit19:16 SWO（块内起始字偏移）",
        "　　　　bit14 EV（帧结束）· bit13:8 EBO（帧末字节在块内下标）· bit7:6 TSC（时间戳捕获 A/B/C）· bit0 P（奇校验）",
        "数据尾：bit28:24 RBA（还能读几块）· bit21 DV · bit20 SV · bit15 FD（FCS 错/该帧被丢弃）· bit5:1 TXC（剩余发送 credit）",
        "★ 陷阱：EV=0 的非结束块 EBO 无效（芯片回 0）→ 必须整块 64B 拷贝；否则 104B 帧只能重组出 41B（本项目最关键的 bug）",
    ]
    for i, n in enumerate(notes):
        _t(ax, 0.34, 1.52 - i * 0.32, n, 7.9, C_TEXT, ha="left")
    _save(fig, "spi_data_txn.png")


# ============================================================ 5. PLCA 轮次
def fig_plca_cycle():
    W, H = 12.4, 6.2
    fig, ax = _canvas(W, H)
    _t(ax, W / 2, 5.94,
       "图 5  PLCA 一个轮次 (cycle)：BEACON 之后各节点按 ID 顺序获得发送机会 TO",
       12.8, C_FRAME, weight="bold")

    # 时间轴（在槽位上方）
    yax = 5.42
    ax.annotate("", xy=(12.10, yax), xytext=(0.42, yax),
                arrowprops=dict(arrowstyle="-|>", color=C_FRAME, lw=1.2))
    _t(ax, 12.22, yax, "t", 10.0, C_FRAME)

    slots = [("BEACON\nID 0 协调者", 1.62, "#fbe6e6"),
             ("TO 1\nSTM32H723", 1.06, "#dce9f7"),
             ("TO 2\nHPM", 1.06, "#dce9f7"),
             ("TO 3\nS32K", 1.06, "#dce9f7"),
             ("TO 4\nTC387", 1.06, "#dce9f7"),
             ("TO 5\nRK3506", 1.20, "#dce9f7"),
             ("TO 6\n空闲", 1.06, "#f2f2f2"),
             ("TO 7\n空闲", 1.06, "#f2f2f2"),
             ("下一轮\nBEACON", 1.24, "#fbe6e6")]
    xs = 0.42
    ytop, hh = 4.05, 1.02
    edges = []
    for label, w, col in slots:
        ax.add_patch(Rectangle((xs, ytop), w, hh, facecolor=col, edgecolor=C_FRAME, lw=1.0, zorder=3))
        _t(ax, xs + w / 2, ytop + hh / 2, label, 7.8)
        edges.append((xs, xs + w))
        xs += w
    _t(ax, W / 2, 5.16, "1 轮 = NCNT (=8) 个发送机会，轮次之间无缝衔接", 8.0, C_ACC)

    # TOTMR 标注（在槽位下方）
    a, b = edges[1]
    ax.annotate("", xy=(a + 0.03, 3.86), xytext=(b - 0.03, 3.86),
                arrowprops=dict(arrowstyle="<->", color=C_WARN, lw=1.1))
    _t(ax, (a + b) / 2, 3.64, "TOTMR = 32 BT = 3.2 µs", 7.8, C_WARN, weight="bold")
    _t(ax, (a + b) / 2, 3.40, "本 TO 内无帧 → 超时让给下一个 ID", 7.6, C_WARN)

    rules = [
        ("协调者 (ID=0)", "每轮发 BEACON，所有跟随者用它做时间基准（PST=1）。全网只能有一个，重复会出现 UNEXPB。"),
        ("跟随者 (ID 1..7)", "只在自己的 TO 内发送；TO 内无帧则由 TOTMR 超时自动让给下一个节点，因此不会冲突。"),
        ("NCNT 必须一致", "跟随者 NCNT 与协调者不符 → BCNBFTO 置位、轮询错位。本仓库统一 NCNT=8。"),
        ("Burst 模式", "PLCABURST.MAXBC>0 时同一 TO 内可连发多帧，减少小包轮询开销。"),
        ("优先级模式", "PLCAEXT.PRECEDENCE=1 时低 ID 优先占位，适合有实时性要求的主节点。"),
        ("协调者冗余 CR", "跟随者 PLCAEXT.CREN=1 后，BEACON 连续丢失即接管代发（CRPS=1），避免单点失效。"),
    ]
    y = 2.92
    for k, v in rules:
        _t(ax, 0.36, y, "● " + k, 8.4, C_FRAME, ha="left", weight="bold")
        _t(ax, 3.00, y, v, 8.2, C_TEXT, ha="left")
        y -= 0.42
    _save(fig, "plca_cycle.png")


# ============================================================ 6. 吞吐对比
def fig_perf_bar():
    fig, ax = plt.subplots(figsize=(11.0, 5.7))
    fig.patch.set_facecolor(C_BG)
    names = [p[0] for p in PERF]
    tcp = [p[1] if p[1] else 0 for p in PERF]
    bench = [p[2] if p[2] else 0 for p in PERF]
    y = list(range(len(names)))[::-1]
    bh = 0.33

    ax.barh([v + bh / 2 + 0.03 for v in y], tcp, height=bh, color="#2f6fa8",
            label="TCP 吞吐 (iperf2, 主机→板)")
    ax.barh([v - bh / 2 - 0.03 for v in y], [b if b else 0 for b in bench], height=bh,
            color="#d98b2b", label="纯发送 bench (MAC 直接发，不经协议栈)")

    for i, v, rec in zip(y, tcp, PERF):
        if v:
            ax.text(v + 0.14, i + bh / 2 + 0.03, "%.2f" % v, va="center",
                    fontsize=9, color="#2f6fa8")
        else:
            ax.text(0.14, i + bh / 2 + 0.03, rec[3], va="center", fontsize=8.4, color=C_WARN)
    for i, v, rec in zip(y, bench, PERF):
        if v:
            ax.text(v + 0.14, i - bh / 2 - 0.03, "%.2f" % v, va="center",
                    fontsize=9, color="#d98b2b")
        elif rec[1]:
            ax.text(0.14, i - bh / 2 - 0.03, "未测", va="center", fontsize=8.2, color=C_MUTED)

    ax.set_yticks(y)
    ax.set_yticklabels(names, fontsize=9.2)
    ax.set_xlim(0, 11.4)
    ax.set_xlabel("Mbit/s    10BASE-T1S 线速 = 10 Mbit/s，1500B 帧的理论 TCP 上限 ≈ 9.43 Mbit/s"
                  "（未画出的条目 = 该项未测）", fontsize=9)
    ax.axvline(10.0, color=C_BUS, ls="--", lw=1.2)
    ax.text(10.08, -0.62, "线速 10", color=C_BUS, fontsize=8.4)
    ax.axvline(9.43, color="#777", ls=":", lw=1.1)
    ax.text(9.30, len(names) - 0.62, "理论 TCP 上限 9.43", color="#555", fontsize=8.2, ha="right")
    ax.grid(axis="x", ls=":", color="#cccccc", lw=0.7)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(fontsize=8.8, loc="lower center", bbox_to_anchor=(0.5, 1.005),
              ncol=2, framealpha=0.97, borderaxespad=0.1)
    ax.set_title("图 6  各平台实测吞吐（2026-11-20，来源 docs/results/ 与各平台 README）",
                 fontsize=11.8, color=C_FRAME, fontweight="bold", pad=46)
    _save(fig, "perf_bar.png")


# ============================================================ 6b. 优化前后
def fig_before_after():
    fig, ax = plt.subplots(figsize=(9.6, 4.6))
    fig.patch.set_facecolor(C_BG)
    names = [b[0] for b in BEFORE_AFTER]
    before = [b[1] for b in BEFORE_AFTER]
    after = [b[2] for b in BEFORE_AFTER]
    x = list(range(len(names)))
    bw = 0.36

    b1 = ax.bar([v - bw / 2 - 0.01 for v in x], before, width=bw,
                color="#9fb6cc", label="优化前 TCP 下行")
    b2 = ax.bar([v + bw / 2 + 0.01 for v in x], after, width=bw,
                color="#2f6fa8", label="优化后 TCP 下行")
    for rect, v in list(zip(b1, before)) + list(zip(b2, after)):
        ax.text(rect.get_x() + rect.get_width() / 2, v + 0.12, "%.2f" % v,
                ha="center", fontsize=8.6, color=C_TEXT)
    for i, b in enumerate(BEFORE_AFTER):
        ax.annotate(b[3], xy=(i, max(before[i], after[i]) + 0.65), ha="center",
                    fontsize=8.8, color=C_OK, fontweight="bold")

    ax.set_xticks(x)
    ax.set_xticklabels(names, fontsize=9.4)
    ax.set_ylabel("Mbit/s", fontsize=9)
    ax.set_ylim(0, 11.0)
    ax.axhline(10.0, color=C_BUS, ls="--", lw=1.1)
    ax.text(len(names) - 0.42, 10.15, "10BASE-T1S 线速 10 Mbit/s",
            color=C_BUS, fontsize=8.4, ha="right")
    ax.grid(axis="y", ls=":", color="#cccccc", lw=0.7)
    ax.set_axisbelow(True)
    for sp in ("top", "right"):
        ax.spines[sp].set_visible(False)
    ax.legend(fontsize=8.8, loc="upper center", bbox_to_anchor=(0.5, -0.13),
              ncol=2, framealpha=0.95, borderaxespad=0.1)
    ax.set_title("图 9  各平台优化前后 TCP 吞吐（2026-11-20，flock 串行测量）",
                 fontsize=11.5, color=C_FRAME, fontweight="bold", pad=12)
    _save(fig, "perf_before_after.png")


# ============================================================ 7. 端接
def fig_termination():
    fig, axes = plt.subplots(1, 2, figsize=(12.0, 4.4))
    fig.patch.set_facecolor(C_BG)
    fig.suptitle("图 7  10BASE-T1S 端接：只有物理两端才能保留 100 Ω 终端",
                 fontsize=12.4, color=C_FRAME, fontweight="bold", y=1.03)

    for ax, ok in zip(axes, (True, False)):
        ax.set_xlim(0, 6.0)
        ax.set_ylim(0, 4.0)
        ax.axis("off")
        ax.plot([0.35, 5.65], [2.05, 2.05], color=C_BUS, lw=2.2)
        ax.plot([0.35, 5.65], [1.90, 1.90], color=C_BUS, lw=2.2)
        xs = [1.00 + i * 1.32 for i in range(4)]
        term = [True, False, False, True] if ok else [True, True, True, True]
        for i, x in enumerate(xs):
            ax.plot([x, x], [2.05, 2.62], color=C_BUS, lw=1.5)
            _box(ax, x - 0.38, 2.62, 0.76, 0.50, C_MCU, lw=1.0, r=0.04)
            ax.text(x, 2.87, "板%d" % (i + 1), ha="center", va="center", fontsize=8.0)
            if term[i]:
                ax.plot([x, x], [1.90, 1.44], color=C_FRAME, lw=1.2)
                ax.add_patch(Rectangle((x - 0.14, 1.06), 0.28, 0.38, facecolor=C_PHY,
                                       edgecolor=C_FRAME, lw=1.0, zorder=4))
                ax.text(x + 0.22, 1.25, "100 Ω", ha="left", va="center", fontsize=7.6, color=C_WARN)
            else:
                ax.text(x, 1.55, "拆\n除", ha="center", va="center", fontsize=7.4,
                        color="#9aa3ad", linespacing=1.3)

        if ok:
            ax.text(3.0, 3.42, "✔ 正确：两端端接", ha="center", fontsize=11.0,
                    color=C_OK, fontweight="bold")
            ax.text(3.0, 0.62, "2 个 100 Ω 并联 → 差分 ≈ 50 Ω\nSQI 高 · 眼图张开 · HDD = 0",
                    ha="center", fontsize=9.0, color=C_OK, fontweight="bold", linespacing=1.5)
            ax.text(3.0, 0.12, "R12/R13 保留（仅首末两块板）", ha="center", fontsize=8.2, color=C_MUTED)
        else:
            ax.text(3.0, 3.42, "✘ 错误：每块板都焊了终端", ha="center", fontsize=11.0,
                    color=C_WARN, fontweight="bold")
            ax.text(3.0, 0.62, "4 个 100 Ω 并联 → 差分 ≈ 25 Ω\nSQI 掉到 0-1/7 · HDD = SHORT_OPEN=2",
                    ha="center", fontsize=9.0, color=C_WARN, fontweight="bold", linespacing=1.5)
            ax.text(3.0, 0.12, "自制 S2500_1 每块都焊了 R12/R13 → 必须拆", ha="center", fontsize=8.2, color=C_MUTED)

    fig.text(0.5, -0.04,
             "断电用万用表量 T1S_P ↔ T1S_N：约 50 Ω = 正常（两端各 100 Ω 并联）｜ < 20 Ω = 端接过多｜ ∞ = 断线\n"
             "N 个节点各 100 Ω 并联 → 差分 ≈ 100/N Ω：2 节点 50 Ω · 3 节点 33 Ω · 6 节点 17 Ω · 7 节点 14 Ω",
             ha="center", fontsize=8.8, color=C_TEXT, linespacing=1.6)
    _save(fig, "termination.png")


# ============================================================ 8. 排障流程 (graphviz)
DOT_TROUBLE = r'''
digraph trouble {
  graph [rankdir=TB, splines=true, nodesep=0.30, ranksep=0.36, bgcolor="white",
         fontname="Noto Sans CJK JP", fontsize=11, pad=0.25];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK JP", fontsize=10,
         color="#2b3a55", fillcolor="#dce9f7", penwidth=1.1, margin="0.14,0.08"];
  edge  [fontname="Noto Sans CJK JP", fontsize=9, color="#4a5568", arrowsize=0.75];

  start [label="上电：串口无输出 / 板子行为异常", fillcolor="#e4f2e0"];
  q1    [label="串口有 shell 提示符?\n(s2500> )", shape=diamond, fillcolor="#fff7d6", height=0.85];
  a1    [label="查串口本身\n· 波特率 115200（RK3506 = 1.5 Mbit/s）\n· TX/RX 交叉、GND 共地\n· ./tools/s25.py --auto <board> -c \"help\"", fillcolor="#fbe6e6"];
  q2    [label="s2500 reg 0 0\n== 0x00000011 ?", shape=diamond, fillcolor="#fff7d6", height=0.85];
  a2    [label="SPI 通路不通\n1) SPI 模式 0 / MSB first\n2) CS_N 是否覆盖整个 68B 事务\n3) MISO 读到 0xFFFFFFFF → 未接/被上拉\n4) VIO 3.3V、SCLK ≤ 25MHz", fillcolor="#fbe6e6"];
  q3    [label="s2500 selftest\nPCS + PHY 环回 PASS ?", shape=diamond, fillcolor="#fff7d6", height=0.85];
  a3    [label="PHY CONTROL.LCTL = 0（PHY 收发关闭）\n—— 本项目最常见的根因\n置 LCTL=1，并确认 STATUS0.RESETC=1", fillcolor="#fbe6e6"];
  q4    [label="主机 ping 通?\n192.168.10.1x", shape=diamond, fillcolor="#fff7d6", height=0.85];
  a4    [label="查总线物理层\n1) 端接是否过多（量 50Ω，见 图7）\n2) MCONTROL0.BCSF/MCSF 误置 → ARP 收不到\n3) NCNT/TOTMR 全网是否一致", fillcolor="#fbe6e6"];
  q5    [label="PLCA 下 PST = 1 ?", shape=diamond, fillcolor="#fff7d6", height=0.85];
  a5    [label="跟随者收不到 BEACON 就不能发\n1) 主机 sudo ./host/plca.sh on\n2) NCNT / TOTMR 是否一致\n3) ID 是否 < NCNT 且全网唯一", fillcolor="#fbe6e6"];
  q6    [label="iperf 吞吐 < 2 Mbit/s ?", shape=diamond, fillcolor="#fff7d6", height=0.85];
  a6    [label="1) 用的是 iperf3？→ lwiperf 只兼容 iperf2\n2) SPI 时钟只有几 MHz（软件 SPI）\n3) TCP_WND / TCP_SND_BUF 太小\n4) 主循环阻塞 > 1.2ms → RX 缓冲溢出丢帧", fillcolor="#fbe6e6"];
  ok    [label="链路正常 → 继续做 PLCA / SQI / HDD / TC14 / TC10 / TC6 功能验证",
         fillcolor="#d9f2d9", color="#1e6b32", penwidth=1.6];

  start -> q1;
  q1 -> a1 [label=" 否"]; q1 -> q2 [label=" 是"];
  q2 -> a2 [label=" 否"]; q2 -> q3 [label=" 是"];
  q3 -> a3 [label=" 否"]; q3 -> q4 [label=" 是"];
  q4 -> a4 [label=" 否"]; q4 -> q5 [label=" 是"];
  q5 -> a5 [label=" 否"]; q5 -> q6 [label=" 是"];
  q6 -> a6 [label=" 是"]; q6 -> ok [label=" 否"];
}
'''


def fig_trouble():
    dot = shutil.which("dot")
    if not dot:
        print("  !! 未找到 graphviz dot, 跳过 troubleshooting_flow.png", file=sys.stderr)
        return
    src = os.path.join(OUT, ".troubleshooting_flow.dot")
    with open(src, "w", encoding="utf-8") as f:
        f.write(DOT_TROUBLE)
    out = os.path.join(OUT, "troubleshooting_flow.png")
    subprocess.check_call([dot, "-Tpng", "-Gdpi=150", src, "-o", out])
    os.unlink(src)
    print("  -> %-26s %7.1f KB" % ("troubleshooting_flow.png", os.path.getsize(out) / 1024.0))


FIGS = {
    "topology": fig_topology,
    "sw_stack": fig_sw_stack,
    "spi_control_txn": fig_spi_control,
    "spi_data_txn": fig_spi_data,
    "plca_cycle": fig_plca_cycle,
    "perf_bar": fig_perf_bar,
    "perf_before_after": fig_before_after,
    "termination": fig_termination,
    "troubleshooting_flow": fig_trouble,
}


def main(argv):
    want = argv[1:] or list(FIGS)
    print("中文字体: %s   输出目录: %s" % (CJK, OUT))
    for name in want:
        if name not in FIGS:
            print("未知图名: %s (可选: %s)" % (name, ", ".join(FIGS)), file=sys.stderr)
            continue
        print("[*] %s" % name)
        FIGS[name]()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
