#!/usr/bin/env python3
"""tools/figs/mcal_eth_irq.py —— 第 49 章 §49.7.5 中断模式量化插图

图 49-10：中断 vs 轮询：板端实收帧率（相对主机发送）+ 主循环 RX 取帧路径 CPU 占比
图 49-11：中断模式的 ISR→消费延迟 / 唤醒延迟 + 延迟分布（分桶）

数据来源：logs/*-eth_irq-host.log（samples/mcal/eth_irq/host/run_irq_matrix.sh 生成）
用法：
  python3 tools/figs/mcal_eth_irq.py --log logs/20261102-184310-eth_irq-host.log
不给 --log 时取 logs/.last_eth_irq-host，再退化为最近一个 *-eth_irq-host.log。
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SCEN = ["idle", "low", "mid", "high"]
LABEL = {"idle": "空闲", "low": "1000 帧/s", "mid": "20 000 帧/s", "high": "全速灌包"}


def pick_log(cli):
    if cli:
        return Path(cli)
    last = ROOT / "logs" / ".last_eth_irq-host"
    if last.exists():
        return last
    cands = sorted((ROOT / "logs").glob("*-eth_irq-host.log"))
    if not cands:
        sys.exit("找不到 eth_irq 主机日志，请用 --log 指定")
    return cands[-1]


def val(r, key):
    """缺失行/缺失字段返回 NaN（log 轴上 0 会被变换成 -inf，撑爆 bbox_inches=tight）"""
    if r is None:
        return float("nan")
    v = fnum(r, key)
    return v if v > 0 else float("nan")


def save_checked(fig, name):
    """保存前后自检画布尺寸：tight bbox 遇到离群 artist 会把画布撑到几万像素"""
    print("[fig] %s figsize=%s in" % (name, tuple(round(v, 2) for v in fig.get_size_inches())))
    out = save(fig, 49, name)
    try:
        from PIL import Image
        with Image.open(out) as im:
            w, h = im.size
        print("[fig] %s png=%dx%d px" % (name, w, h))
        if not (1500 <= w <= 4000 and 700 <= h <= 2500):
            print("[fig][WARN] %s 尺寸异常（tight bbox 可能被离群 artist 撑大）" % name)
    except Exception as e:  # PIL 不是硬依赖
        print("[fig][WARN] PIL 校验失败: %s" % e)
    return out


def parse(path):
    scen, cur = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.search(r"===== \[(\w+)\] (BOARD|HOST) =====", line)
        if m:
            cur = m.group(1)
            scen.setdefault(cur, {"board": [], "host": []})
            continue
        if cur is None:
            continue
        if "IRQX " in line:
            scen[cur]["board"].append(dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line[line.index("IRQX "):])))
        elif "HOST flood" in line:
            scen[cur]["host"].append(dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line[line.index("HOST flood"):])))
    return scen


def row(scen, name, mode):
    for r in scen.get(name, {}).get("board", []):
        if r.get("mode") == mode:
            return r
    return None


def fnum(r, k, default=0.0):
    try:
        return float(r[k])
    except (KeyError, TypeError, ValueError):
        return default


def host_fps(scen, name):
    hs = scen.get(name, {}).get("host", [])
    return fnum(hs[-1], "fps") if hs else 0.0


def fig_vs_poll(scen):
    names = [n for n in ("low", "mid", "high") if row(scen, n, "polling") or row(scen, n, "interrupt")]
    labels = [LABEL[n] for n in names]
    host = [host_fps(scen, n) for n in names]
    poll = [val(row(scen, n, "polling"), "fps") for n in names]
    irq = [val(row(scen, n, "interrupt"), "fps") for n in names]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.8, 3.7))
    x = list(range(len(names)))
    w = 0.26
    b0 = ax1.bar([i - w for i in x], host, w, label="主机发送", color=COLORS["gray"])
    b1 = ax1.bar([i for i in x], poll, w, label="板端收（轮询）", color=COLORS["orange"])
    b2 = ax1.bar([i + w for i in x], irq, w, label="板端收（中断）", color=COLORS["blue"])
    for bars, vals in ((b0, host), (b1, poll), (b2, irq)):
        for b, v in zip(bars, vals):
            if v != v or v <= 0:      # NaN：该档没有这种模式的样本（如中速只有中断档）
                continue
            ax1.text(b.get_x() + b.get_width() / 2, v * 1.04, "%.0f" % v,
                     ha="center", va="bottom", fontsize=6.8,
                     rotation=90 if v > 90000 else 0)
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels, fontsize=8.5)
    ax1.set_ylabel("帧率 (帧/s)")
    ax1.set_yscale("log")
    finite = [v for v in host + poll + irq if v == v]
    ax1.set_ylim(100, max(finite + [1]) * 4)
    ax1.grid(axis="y", alpha=0.25)
    ax1.legend(fontsize=7.5, loc="upper left", ncol=1)
    ax1.set_title("(a) 板端实收帧率 vs 主机发送", fontsize=9.5)

    cpu_names = [n for n in ("idle", "low", "high") if row(scen, n, "polling") or row(scen, n, "interrupt")]
    cpu_p = [val(row(scen, n, "polling"), "rx_cpu_x100") / 100.0 for n in cpu_names]
    cpu_i = [val(row(scen, n, "interrupt"), "rx_cpu_x100") / 100.0 for n in cpu_names]
    x2 = list(range(len(cpu_names)))
    b3 = ax2.bar([i - 0.18 for i in x2], cpu_p, 0.36, label="轮询", color=COLORS["orange"])
    b4 = ax2.bar([i + 0.18 for i in x2], cpu_i, 0.36, label="RX 中断", color=COLORS["blue"])
    for bars, vals in ((b3, cpu_p), (b4, cpu_i)):
        for b, v in zip(bars, vals):
            if v != v:
                continue
            ax2.text(b.get_x() + b.get_width() / 2, v + 0.6, "%.2f" % v,
                     ha="center", va="bottom", fontsize=7.5)
    ax2.set_xticks(x2)
    ax2.set_xticklabels([LABEL[n] for n in cpu_names], fontsize=8.5)
    ax2.set_ylabel("主循环 RX 取帧路径 CPU 占比 (%)")
    cpu_fin = [v for v in cpu_p + cpu_i if v == v]
    ax2.set_ylim(0, max(cpu_fin + [1]) * 1.25)
    ax2.grid(axis="y", alpha=0.25)
    ax2.legend(fontsize=8, loc="upper left")
    ax2.set_title("(b) 取帧路径 CPU 占比（空闲差距最大）", fontsize=9.5)
    fig.tight_layout()
    save_checked(fig, "fig-49-10-irq-vs-poll")


def fig_latency(scen):
    names = [n for n in ("low", "mid", "high") if row(scen, n, "interrupt")]
    labels = [LABEL[n] for n in names]
    avg = [val(row(scen, n, "interrupt"), "lat_avg_us") for n in names]
    mx = [val(row(scen, n, "interrupt"), "lat_max_us") for n in names]
    wake = [val(row(scen, n, "interrupt"), "wake_avg_us") for n in names]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.8, 3.7))
    x = list(range(len(names)))
    w = 0.26
    b1 = ax1.bar([i - w for i in x], avg, w, label="ISR→消费 avg", color=COLORS["green"])
    b2 = ax1.bar([i for i in x], mx, w, label="ISR→消费 max", color=COLORS["red"])
    b3 = ax1.bar([i + w for i in x], wake, w, label="ISR→主循环发现 avg", color=COLORS["purple"])
    for bars, vals in ((b1, avg), (b2, mx), (b3, wake)):
        for b, v in zip(bars, vals):
            if v != v or v <= 0:
                continue
            ax1.text(b.get_x() + b.get_width() / 2, v * 1.10, "%.1f" % v,
                     ha="center", va="bottom", fontsize=7.2)
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels, fontsize=8.5)
    ax1.set_ylabel("延迟 (µs)")
    ax1.set_yscale("log")
    fin = [v for v in avg + mx + wake if v == v]
    ax1.set_ylim(0.1, max(fin + [1]) * 5)
    ax1.grid(axis="y", alpha=0.25)
    ax1.legend(fontsize=7.5)
    ax1.set_title("(a) 中断模式延迟（1000 帧/s 最差）", fontsize=9.5)

    keys = ["lb_lt5", "lb_5_20", "lb_20_100", "lb_100_1m"]
    klabel = ["<5 µs", "5–20 µs", "20–100 µs", "0.1–1 ms"]
    colors = [COLORS["blue"], COLORS["green"], COLORS["orange"], COLORS["red"]]
    bottoms = [0.0] * len(names)
    for k, kl, c in zip(keys, klabel, colors):
        vals = []
        for n in names:
            r = row(scen, n, "interrupt")
            tot = sum(fnum(r, kk) for kk in keys) or 1.0
            vals.append(fnum(r, k) / tot * 100.0)
        ax2.bar(labels, vals, bottom=bottoms, color=c, label=kl, width=0.55)
        bottoms = [b + v for b, v in zip(bottoms, vals)]
    for i, b in enumerate(bottoms):
        ax2.text(i, b + 1.5, "%.1f%%" % b, ha="center", fontsize=7.5)
    ax2.set_ylabel("有效延迟样本占比 (%)")
    ax2.set_ylim(0, 115)
    ax2.grid(axis="y", alpha=0.25)
    ax2.legend(fontsize=7.5, ncol=2, loc="lower center")
    ax2.set_title("(b) 延迟分布（分桶）", fontsize=9.5)
    fig.tight_layout()
    save_checked(fig, "fig-49-11-irq-latency")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default=None)
    a = ap.parse_args()
    path = pick_log(a.log)
    scen = parse(path)
    if not scen:
        sys.exit("日志里没有解析到场景段: %s" % path)
    setup()
    fig_vs_poll(scen)
    fig_latency(scen)
    print("FIG 50-10/50-11 OK  <- %s" % path)
    for n in SCEN:
        for r in scen.get(n, {}).get("board", []):
            print("  [%s] %-9s fps=%s isr_pf=%s lat=%s/%s wake=%s cpu=%s"
                  % (n, r.get("mode"), r.get("fps"), r.get("isr_pf_x1000"),
                     r.get("lat_avg_us"), r.get("lat_max_us"), r.get("wake_avg_us"),
                     r.get("rx_cpu_x100")))


if __name__ == "__main__":
    main()