#!/usr/bin/env python3
"""candump 文本解析 / 统计 / 顺序校验（hpm5e31_1 can_logger 例程配套）

输入：板端 can_logger 的 `log dump` 串口输出（或任意 candump -L/-l 文本），格式：
    (1793000001.234567) can0 300#01000000A5A5A5A5
    (1793000001.234567) can0 181#0102AABB
也接受 candump 的扩展帧写法 `can0 18FEF100#...`（8 位十六进制 ID）。

功能：
  1) 帧数、ID 直方图、平均/最大帧间隔、总线负载估算（经典帧 8B ≈ 124 bit）；
  2) 对 0x300 的 4 字节小端 seq 做**顺序/丢帧校验**（can_logger replay 的验收判据）；
  3) --csv 导出逐帧 (t_rel_ms, id, dlc, data)，--png 画 seq-时间散点（matplotlib，可选）。

用法：
    python3 candump_parse.py <dump.txt>                 # 统计 + seq 校验
    python3 candump_parse.py <dump.txt> --csv out.csv
    python3 candump_parse.py <dump.txt> --png out.png --expected-seq 20

依赖：numpy（0.1 以上功能仅用标准库，--png 才需要 matplotlib）
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter

LINE_RE = re.compile(r"^\(([0-9]+)\.([0-9]+)\)\s+can\S*\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)$")


def parse(path: str):
    frames = []
    bad = 0
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = LINE_RE.match(line)
        if not m:
            bad += 1
            continue
        sec, usec, cid, data = m.groups()
        t = int(sec) + int(usec.ljust(6, "0")[:6]) / 1e6
        idn = int(cid, 16)
        db = bytes.fromhex(data) if data and len(data) % 2 == 0 else b""
        frames.append((t, idn, db))
    return frames, bad


def main() -> int:
    ap = argparse.ArgumentParser(description="candump text parser / stats / seq verify")
    ap.add_argument("file", help="candump 文本（板端 log dump 输出）")
    ap.add_argument("--csv", default=None, help="导出逐帧 CSV")
    ap.add_argument("--png", default=None, help="画 0x300 seq 时间散点（需要 matplotlib）")
    ap.add_argument("--seq-id", default="0x300", help="seq 帧 ID（默认 0x300）")
    ap.add_argument("--expected-seq", type=int, default=0, help="期望的 seq 帧数（0=按实际计数）")
    ap.add_argument("--bitrate", type=int, default=500000, help="波特率（用于负载估算）")
    args = ap.parse_args()

    frames, bad = parse(args.file)
    if not frames:
        print("FAIL: 没有解析到任何帧（bad_lines=%d）" % bad)
        return 1

    t0 = frames[0][0]
    dur = frames[-1][0] - t0
    ids = Counter(f[1] for f in frames)
    bytes_total = sum(len(f[2]) for f in frames)
    bit_total = sum(47 + 8 * len(f[2]) + max(0, len(f[2]) - 1) * 8 for f in frames)  # 经典帧近似
    load = (bit_total / (dur * args.bitrate) * 100.0) if dur > 0 else 0.0

    print("=== candump 统计 ===")
    print("帧数           : %d（坏行 %d）" % (len(frames), bad))
    print("时间跨度       : %.3f s" % dur)
    print("平均帧率       : %.1f 帧/s" % (len(frames) / dur if dur > 0 else 0))
    print("数据字节       : %d B" % bytes_total)
    print("总线负载(估算) : %.1f %% @ %d kbit/s" % (load, args.bitrate // 1000))
    print("ID 直方图      : " + ", ".join("0x%03X:%d" % (i, n) for i, n in ids.most_common(8)))

    gaps = [frames[i + 1][0] - frames[i][0] for i in range(len(frames) - 1)]
    neg = [g for g in gaps if g < 0]
    if gaps:
        print("帧间隔         : avg=%.3f ms max=%.3f ms" % (sum(gaps) / len(gaps) * 1000, max(gaps) * 1000))
    print("时间戳乱序     : %d 处%s" % (len(neg), "" if not neg else "  ← 时间戳不可用于周期分析（见 U-38-7）"))

    # seq 校验
    seq_id = int(args.seq_id, 0)
    seqs = []
    for t, idn, db in frames:
        if idn == seq_id and len(db) >= 4:
            seqs.append((t - t0, int.from_bytes(db[:4], "little")))
    ok = True
    if seqs:
        bad_order = [i for i in range(1, len(seqs)) if seqs[i][1] != seqs[i - 1][1] + 1]
        expected = args.expected_seq if args.expected_seq > 0 else len(seqs)
        lost = expected - len(seqs)
        ok = (len(bad_order) == 0) and (lost <= 0) and (len(neg) == 0)
        print("seq 帧         : id=0x%03X 收到 %d/%d, 顺序错误 %d, 丢帧 %d"
              % (seq_id, len(seqs), expected, len(bad_order), max(lost, 0)))
        if seqs:
            print("seq 范围       : %d..%d" % (seqs[0][1], seqs[-1][1]))
    else:
        print("seq 帧         : 未找到 0x%03X 帧" % seq_id)

    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as fp:
            fp.write("t_rel_s,id,dlc,data_hex\n")
            for t, idn, db in frames:
                fp.write("%.6f,0x%03X,%d,%s\n" % (t - t0, idn, len(db), db.hex().upper()))
        print("csv            : %s" % args.csv)

    if args.png:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            if seqs:
                xs = [s[0] * 1000 for s in seqs]
                ys = [s[1] for s in seqs]
                plt.figure(figsize=(6.4, 3.2))
                plt.plot(xs, ys, "o-", ms=3)
                plt.xlabel("相对时间 (ms)")
                plt.ylabel("seq")
                plt.title("0x300 replay seq vs time")
                plt.tight_layout()
                plt.savefig(args.png, dpi=200)
                print("png            : %s" % args.png)
            else:
                print("png            : 无 seq 帧，跳过")
        except ImportError:
            print("png            : matplotlib 未安装，跳过")

    print("=== RESULT: %s ===" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
