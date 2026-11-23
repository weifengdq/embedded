#!/usr/bin/env python3
"""host/mqtt/pcap_latency.py - 从 tcpdump -tttt -nn 文本里拆 MQTT 往返的分段时延

用来回答："单条 MQTT 往返 41 ms，时间花在哪一跳？"
tcpdump 采集： sudo tcpdump -i any -s 0 -U -w x.pcap 'tcp port 1883'
转文本：      tcpdump -r x.pcap -tttt -nn > x.txt
本脚本读 x.txt，按方向配对：
  host->broker (lo)   : 上位机发布命令
  broker->board (enp6s0 Out)
  board->broker (enp6s0 In)
  broker->host (lo)   : 命令回执/回声回到上位机
输出各跳的 Δt 分位数。

用法：python3 host/mqtt/pcap_latency.py logs/xxx.txt [--board 192.168.2.100]
"""
import argparse
import re
import statistics

LINE = re.compile(
    r"^(?P<ts>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\s+(?P<if>\S+)\s+(?P<dir>In|Out)\s+IP\s+"
    r"(?P<src>[\d.]+)\.(?P<sp>\d+)\s+>\s+(?P<dst>[\d.]+)\.(?P<dp>\d+):.*length (?P<len>\d+)")


def parse(path):
    pkts = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            d = m.groupdict()
            pkts.append({
                "t": d["ts"],
                "sec": float(d["ts"].split(" ")[1][:6]) + float(d["ts"].split(".")[1]) / 1e6
                if False else None,
                "if": d["if"], "dir": d["dir"],
                "src": d["src"], "sp": int(d["sp"]),
                "dst": d["dst"], "dp": int(d["dp"]), "len": int(d["len"]),
            })
    return pkts


def ts2s(ts):
    hh, mm, ss = ts.split(" ")[1].split(":")
    return int(hh) * 3600 + int(mm) * 60 + float(ss)


def pct(v, p):
    if not v:
        return 0.0
    k = sorted(v)
    return k[min(len(k) - 1, int(round((p / 100.0) * (len(k) - 1))))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("txt")
    ap.add_argument("--board", default="192.168.2.100")
    args = ap.parse_args()
    pkts = parse(args.txt)
    for p in pkts:
        p["s"] = ts2s(p["t"])

    # 分类
    up = []      # 上位机 -> broker（lo 上非 board 的 PUBLISH，看长度>2）
    to_board = []  # broker -> board
    from_board = []  # board -> broker
    to_host = []  # broker -> 上位机（lo）
    for p in pkts:
        if p["dp"] == 1883 and p["src"] not in (args.board,) and p["if"] == "lo":
            if p["len"] > 20:
                up.append(p)
        elif p["sp"] == 1883 and p["dst"] == args.board:
            to_board.append(p)
        elif p["src"] == args.board and p["dp"] == 1883:
            from_board.append(p)
        elif p["sp"] == 1883 and p["dst"] not in (args.board,) and p["if"] == "lo":
            if p["len"] > 20:
                to_host.append(p)

    print("packets: host->broker=%d broker->board=%d board->broker=%d broker->host=%d"
          % (len(up), len(to_board), len(from_board), len(to_host)))

    def nearest(lst, t):
        best = None
        for p in lst:
            if best is None or abs(p["s"] - t) < abs(best["s"] - t):
                best = p
        return best

    # 每一笔：host 发命令 -> broker 转给板 -> 板回 -> broker 转回 host
    d_h2b, d_b, d_b2h = [], [], []
    for p in up:
        tb = None
        for q in to_board:
            if q["s"] >= p["s"] - 1e-4:
                tb = q
                break
        if tb is None:
            continue
        fb = None
        for q in from_board:
            if q["s"] >= tb["s"] - 1e-4:
                fb = q
                break
        if fb is None:
            continue
        th = None
        for q in to_host:
            if q["s"] >= fb["s"] - 1e-4:
                th = q
                break
        if th is None:
            continue
        if (th["s"] - p["s"]) > 1.0:
            continue
        d_h2b.append((tb["s"] - p["s"]) * 1000.0)
        d_b.append((fb["s"] - tb["s"]) * 1000.0)
        d_b2h.append((th["s"] - fb["s"]) * 1000.0)

    print("配对数 n=%d" % len(d_h2b))
    for name, v in (("上位机->broker(lo)", d_h2b), ("broker->板(线上)", d_b),
                    ("板->broker(线上)", [0]), ("broker->上位机(lo)", d_b2h)):
        if name.startswith("板->broker"):
            continue
        if not v:
            continue
        print("  %-18s avg=%.2f p50=%.2f p95=%.2f max=%.2f ms"
              % (name, statistics.mean(v), pct(v, 50), pct(v, 95), max(v)))


if __name__ == "__main__":
    main()
