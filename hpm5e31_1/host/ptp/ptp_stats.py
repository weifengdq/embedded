#!/usr/bin/env python3
"""ptp_stats.py - 解析 PTP 同步日志，输出 offset / 路径延迟 / 频率漂移统计表。

支持两种输入（自动识别，也可用 --kind 指定）：

  board : 板端串口日志（PTPd 2.0.1 的 updateClock 调试行）
            (D 1793351183.273238081) updateClock: one-way delay averaged (E2E): 0s 13841ns
            (D 1793351183.274378141) updateClock: offset from master: 0s -9ns
            (D 1793351183.275431401) updateClock: observed drift: -18814
  host  : 主机 ptp4l -m 日志
            ptp4l[1234.567]: master offset         -9 s2 freq  -18860 path delay    13841

统计口径（重要）：
  * board 日志默认只统计 **进入 PTP_SLAVE 之后** 的采样（之前的 setTime/UNCALIBRATED 是暂态，
    其 offset 可达 -1.4e8 s 量级，混入均值会完全失真）；首次 setTime 的初始偏差单独列出；
  * --skip-first N 再额外丢弃锁定后的前 N 个采样（默认 0）；
  * drift 单位是 2^-16 ppm（PTPd 内部 scaledPpm），换算 ppm 时除以 65536。

用法：
  python3 host/ptp/ptp_stats.py --log logs/xxxx-ptp_slave.log
  python3 host/ptp/ptp_stats.py --log /tmp/ptp4l-slave.log --kind host --json
  python3 host/ptp/ptp_stats.py --log <log> --skip-first 10 --expect-mean-ns 200 --expect-std-ns 100

判据（默认，可用参数改）：|mean(offset)| <= 1000 ns 且 std(offset) <= 500 ns → PASS。
退出码：0 PASS，1 FAIL，2 无法解析。
"""

import argparse
import json
import math
import re
import sys

RE_BOARD_OFFSET = re.compile(r"updateClock: offset from master:\s*(-?\d+)s\s*(-?\d+)ns")
RE_BOARD_DELAY = re.compile(r"updateClock: one-way delay averaged \((E2E|P2P)\):\s*(-?\d+)s\s*(-?\d+)ns")
RE_BOARD_DRIFT = re.compile(r"updateClock: observed drift:\s*(-?\d+)")
RE_BOARD_STATE = re.compile(r"state (PTP_[A-Z_]+)")
RE_BOARD_SETTIME = re.compile(r"setTime: resetting system clock to (\d+)s")

# ptp4l -m 的 freq 字段带正负号（如 +18581 / -0），offset/path delay 不带
RE_HOST = re.compile(
    r"master offset\s+([+-]?\d+)\s+s(\d)\s+freq\s+([+-]?\d+)\s*(?:\+/-\s*(\d+)\s*)?path delay\s+(\d+)")

DRIFT_SCALE_PPM = 65536.0  # PTPd observedDrift 单位 = ppm * 2^16
MAX_SANE_OFFSET_NS = 10 ** 12


def _blank():
    return {"offset_ns": [], "delay_ns": [], "drift": [], "states": [],
            "settime_s": None, "initial_offset_ns": None, "mechanism": None}


def parse_board(text):
    out = _blank()
    locked = False
    for line in text.splitlines():
        m = RE_BOARD_SETTIME.search(line)
        if m and out["settime_s"] is None:
            out["settime_s"] = int(m.group(1))
        m = RE_BOARD_STATE.search(line)
        if m:
            out["states"].append(m.group(1))
            if m.group(1) == "PTP_SLAVE":
                locked = True
        m = RE_BOARD_DELAY.search(line)
        if m:
            out["mechanism"] = m.group(1)
            d = int(m.group(2)) * 10 ** 9 + int(m.group(3))
            if locked and d > 0:
                out["delay_ns"].append(d)
            continue
        m = RE_BOARD_OFFSET.search(line)
        if m:
            v = int(m.group(1)) * 10 ** 9 + int(m.group(2))
            if not locked:
                if out["initial_offset_ns"] is None:
                    out["initial_offset_ns"] = v
            elif abs(v) < MAX_SANE_OFFSET_NS:
                out["offset_ns"].append(v)
            continue
        m = RE_BOARD_DRIFT.search(line)
        if m and locked:
            out["drift"].append(int(m.group(1)))
    return out


def parse_host(text):
    out = _blank()
    for line in text.splitlines():
        m = RE_HOST.search(line)
        if not m:
            continue
        frac = int(m.group(2))
        v = int(m.group(1)) + frac / 10.0
        if abs(v) > MAX_SANE_OFFSET_NS:
            if out["initial_offset_ns"] is None:
                out["initial_offset_ns"] = v     # 主机从钟第一次锁定前的巨量偏差（板端时钟初值 2022 年）
            continue
        out["offset_ns"].append(v)
        out["drift"].append(int(m.group(3)))
        out["delay_ns"].append(int(m.group(5)))
        out["states"].append("SERVO")
    out["mechanism"] = None
    return out


def summarize(vals):
    if not vals:
        return None
    n = len(vals)
    mean = sum(vals) / n
    var = sum((v - mean) ** 2 for v in vals) / n
    s = sorted(vals)
    return {"n": n, "mean": mean, "std": math.sqrt(var), "min": s[0], "max": s[-1],
            "p50": s[n // 2], "p99": s[min(n - 1, int(0.99 * n))],
            "absmax": max(abs(v) for v in vals)}


def convergence_s(offset_ns, limit_ns=1000, hold=5):
    run = 0
    for i, v in enumerate(offset_ns):
        if abs(v) < limit_ns:
            run += 1
            if run >= hold:
                return i - hold + 1
        else:
            run = 0
    return None


def main():
    ap = argparse.ArgumentParser(description="PTP synced-log statistics")
    ap.add_argument("--log", required=True, help="日志文件路径")
    ap.add_argument("--kind", choices=["auto", "board", "host"], default="auto")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--skip-first", type=int, default=0,
                    help="锁定后再丢弃开头 N 个 offset 采样")
    ap.add_argument("--expect-mean-ns", type=float, default=1000.0)
    ap.add_argument("--expect-std-ns", type=float, default=500.0)
    ap.add_argument("--expect-delay-us", type=float, default=0.0,
                    help=">0 时额外检查路径延迟均值（us），容差 ±50%%")
    args = ap.parse_args()

    try:
        with open(args.log, "r", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print("cannot read %s: %s" % (args.log, e), file=sys.stderr)
        return 2

    kind = args.kind
    if kind == "auto":
        kind = "host" if RE_HOST.search(text) else "board"
    data = parse_host(text) if kind == "host" else parse_board(text)
    if not data["offset_ns"]:
        print("FAIL: no PTP offset sample found in %s (kind=%s); "
              "板端日志需要含 'state PTP_SLAVE' 之后的 updateClock 行" % (args.log, kind), file=sys.stderr)
        return 2

    skip = max(0, args.skip_first)
    def trim(v):
        return v[skip:] if len(v) > skip else v

    off = summarize(trim(data["offset_ns"]))
    delay = summarize(trim(data["delay_ns"]))
    drift = summarize(trim(data["drift"]))
    conv = convergence_s(data["offset_ns"])

    ok_mean = abs(off["mean"]) <= args.expect_mean_ns
    ok_std = off["std"] <= args.expect_std_ns
    ok = ok_mean and ok_std
    if args.expect_delay_us > 0 and delay:
        ok = ok and abs(delay["mean"] / 1000.0 - args.expect_delay_us) < 0.5 * args.expect_delay_us

    result = {
        "log": args.log, "kind": kind, "mechanism": data["mechanism"],
        "initial_offset_ns": data["initial_offset_ns"],
        "settime_s": data["settime_s"],
        "offset_ns": off, "path_delay_ns": delay,
        "drift_scaled": drift,
        "drift_ppm": (drift["mean"] / DRIFT_SCALE_PPM) if drift else None,
        "convergence_samples": conv, "states": data["states"],
        "pass": ok,
        "thresholds": {"mean_ns": args.expect_mean_ns, "std_ns": args.expect_std_ns,
                       "skip_first": skip},
    }

    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=1))
    else:
        print("== PTP 同步统计（%s, kind=%s, mech=%s）==" % (args.log, kind, data["mechanism"]))
        if data["initial_offset_ns"] is not None:
            print("首次设初值前的偏差: %.3f s（暂态，不计入统计）" % (data["initial_offset_ns"] / 1e9))
        if conv is not None:
            print("锁定过程: 进入 SLAVE 后连续 5 点 |offset|<1us 出现在第 %d 个采样" % conv)
        print("%-20s %12s %12s %12s %12s" % ("指标", "均值", "标准差", "最小", "最大"))
        print("-" * 72)
        print("%-20s %12.1f %12.1f %12.0f %12.0f" %
              ("offset (ns)", off["mean"], off["std"], off["min"], off["max"]))
        if delay:
            print("%-20s %12.1f %12.1f %12.0f %12.0f" %
                  ("单向路径延迟 (ns)", delay["mean"], delay["std"], delay["min"], delay["max"]))
        if drift:
            print("%-20s %12.1f %12.1f %12.0f %12.0f" %
                  ("drift(2^-16 ppm)", drift["mean"], drift["std"], drift["min"], drift["max"]))
            print("%-20s %12.3f ppm（负值=本地钟偏快）" % ("drift 换算", result["drift_ppm"]))
        print("%-20s %12.0f %12.0f" % ("offset p50/p99 (ns)", off["p50"], off["p99"]))
        print("%-20s %12d" % ("样本数", off["n"]))
        print("-" * 72)
        print("判据: |mean| <= %.0f ns (实测 %.1f) %s ; std <= %.0f ns (实测 %.1f) %s" % (
            args.expect_mean_ns, off["mean"], "OK" if ok_mean else "NG",
            args.expect_std_ns, off["std"], "OK" if ok_std else "NG"))
        print("结论: %s" % ("PASS" if ok else "FAIL"))

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())