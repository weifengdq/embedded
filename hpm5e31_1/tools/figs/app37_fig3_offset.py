#!/usr/bin/env python3
"""第 38 章 图 38-3：从钟 offset 收敛与单向路径延迟（实测，E2E two-step，主机 ptp4l 作主钟）。

数据来源：板端串口日志（PTPd 的 updateClock 调试行 + setTime 行）；主图数据取 2026-11-02 的 60 s 连续窗口
logs/20261102-101345-ptp_slave.log，脚本默认取最新一份 logs/*-ptp_slave.log。
运行：cd hpm5e31_1
      python3 tools/figs/app37_fig3_offset.py                 # 自动取最新一份 ptp_slave 日志（当前=60 s 窗口）
      python3 tools/figs/app37_fig3_offset.py --log logs/xxx-ptp_slave.log
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
RE_D = re.compile(r"^\(D (\d+)\.(\d+)\)")
RE_OFF = re.compile(r"updateClock: offset from master:\s*(-?\d+)s\s*(-?\d+)ns")
RE_DEL = re.compile(r"updateClock: one-way delay averaged \(E2E\):\s*(-?\d+)s\s*(-?\d+)ns")
RE_SET = re.compile(r"setTime: resetting system clock to (\d+)s")
RE_STATE = re.compile(r"state (PTP_SLAVE)")

MAX_PLOT_OFFSET_NS = 1e6      # 首次粗调前的偏差是 1e8 s 量级，画图时丢弃


def parse(path):
    t_ref = t_slave = None
    offs, dels = [], []
    for line in path.read_text(errors="replace").splitlines():
        m = RE_D.match(line)
        if not m:
            continue
        t = int(m.group(1)) + int(m.group(2)) / 1e9
        if RE_SET.search(line) and t_ref is None:
            t_ref = t
        if RE_STATE.search(line) and t_slave is None:
            t_slave = t
        m = RE_OFF.search(line)
        if m:
            offs.append((t, int(m.group(1)) * 10 ** 9 + int(m.group(2))))
            continue
        m = RE_DEL.search(line)
        if m:
            d = int(m.group(1)) * 10 ** 9 + int(m.group(2))
            if d > 0:
                dels.append((t, d))
    return t_ref, t_slave, offs, dels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default=None)
    args = ap.parse_args()

    if args.log:
        log = Path(args.log)
        if not log.is_absolute():
            log = ROOT / log
    else:
        cands = sorted(ROOT.glob("logs/*-ptp_slave.log"), key=lambda p: p.stat().st_mtime)
        if not cands:
            sys.exit("no logs/*-ptp_slave.log found")
        log = cands[-1]

    t_ref, t_slave, offs, dels = parse(log)
    if not offs:
        sys.exit("no updateClock line in %s" % log)
    t0 = t_ref if t_ref else offs[0][0]

    setup()
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7.8, 5.2), sharex=True,
                                   gridspec_kw={"height_ratios": [2.0, 1.0]})

    pts = [(t - t0, v / 1000.0) for t, v in offs if abs(v) < MAX_PLOT_OFFSET_NS]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    ax1.plot(xs, ys, color=COLORS["blue"], lw=1.1, marker="o", ms=2.6, label="offset（updateClock 输出）")
    ax1.axhline(0, color=COLORS["gray"], lw=0.8, ls=":")

    # 首次 Delay_Resp 造成的 13.8 µs 台阶：标注出来（第 38 章易错点 3）
    if -13.6 <= min(ys) <= -12.6:
        ax1.axhspan(-14.5, -12.5, color=COLORS["red"], alpha=0.07)
        ax1.text(0.98, 0.12, "第一次 Delay_Resp 带来的 13.8 µs 台阶（正常收敛过程）",
                 transform=ax1.transAxes, ha="right", fontsize=7.8, color=COLORS["red"])

    if t_slave:
        ax1.axvline(t_slave - t0, color=COLORS["green"], lw=1.3, ls="--",
                    label="进入 PTP_SLAVE（第 %.0f s）" % (t_slave - t0))

    # 稳态统计：与 host/ptp/ptp_stats.py 同口径（进入 SLAVE 后，剔除前 10 个采样）
    locked = [v * 1000.0 for x, v in pts if (not t_slave or x >= t_slave - t0)]
    steady = locked[10:] if len(locked) > 10 else locked
    if steady:
        mean = sum(steady) / len(steady)
        sigma = (sum((v - mean) ** 2 for v in steady) / len(steady)) ** 0.5
        ax1.text(0.015, 0.92,
                 "锁定后稳态（剔除前 10 个采样，n=%d）：均值 %.1f ns，σ %.1f ns" % (len(steady), mean, sigma),
                 transform=ax1.transAxes, fontsize=8.2, color=COLORS["green"],
                 bbox=dict(facecolor="white", edgecolor="none", alpha=0.85, pad=1.5))

    ax1.set_ylabel("offset (µs)")
    ax1.legend(loc="upper right", fontsize=8.2, framealpha=0.9)
    style_axes(ax1)

    if dels:
        dx = [t - t0 for t, _ in dels]
        dy = [v / 1000.0 for _, v in dels]
        ax2.plot(dx, dy, color=COLORS["orange"], lw=1.1, marker="s", ms=2.4, label="单向路径延迟（E2E）")
        ax2.set_ylabel("路径延迟 (µs)")
        ax2.ticklabel_format(useOffset=False, axis="y")
        ax2.legend(loc="lower right", fontsize=8.2)
        style_axes(ax2)

    ax2.set_xlabel("时间 (s)（相对 setTime 粗调时刻）")
    fig.tight_layout()
    save(fig, 38, "fig-38-3-offset-convergence")


if __name__ == "__main__":
    main()