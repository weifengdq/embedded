#!/usr/bin/env python3
"""CANopen SDO 分段传输吞吐实测：1 KB vs 4 KB（CiA 301 segmented transfer）。

用 python-canopen 的 SDO client 对节点 1 的 0x2001（1024 B domain）与
0x2002（4096 B domain）做分段 upload/download，测量总耗时与吞吐（B/s），
并校验数据图案；另测一次 expedited 小对象读取速率。

用法:
    python3 co_sdo_seg.py                    # 5 轮, vcan0
    python3 co_sdo_seg.py --iterations 10 --channel vcan0
    python3 co_sdo_seg.py --csv sdo.csv

依赖: python-can, python-canopen
"""
from __future__ import annotations

import argparse
import os
import statistics
import sys
import time

import canopen

HERE = os.path.dirname(os.path.abspath(__file__))


def pattern1k() -> bytes:
    return bytes((i & 0xFF) for i in range(1024))


def pattern4k() -> bytes:
    return bytes(((i * 7) & 0xFF) for i in range(4096))


def bench_upload(node: canopen.RemoteNode, index: int, iterations: int, retries: int = 0) -> tuple[list[float], bytes, int]:
    times: list[float] = []
    data = b""
    attempts = 0
    for _ in range(iterations):
        for attempt in range(retries + 1):
            attempts += 1
            try:
                t0 = time.perf_counter()
                data = node.sdo.upload(index, 0)
                times.append(time.perf_counter() - t0)
                break
            except Exception:  # noqa: BLE001 - SdoAbortedError / timeout 都重试
                if attempt >= retries:
                    raise
                time.sleep(0.2)
    return times, data, attempts


def bench_download(node: canopen.RemoteNode, index: int, payload: bytes, iterations: int,
                   retries: int = 0) -> tuple[list[float], int]:
    times: list[float] = []
    attempts = 0
    for _ in range(iterations):
        for attempt in range(retries + 1):
            attempts += 1
            try:
                t0 = time.perf_counter()
                node.sdo.download(index, 0, payload)
                times.append(time.perf_counter() - t0)
                break
            except Exception:  # noqa: BLE001
                if attempt >= retries:
                    raise
                time.sleep(0.2)
    return times, attempts


def summary(times: list[float], nbytes: int) -> dict:
    total = sum(times)
    return {
        "n": len(times),
        "min_ms": min(times) * 1000.0,
        "avg_ms": statistics.fmean(times) * 1000.0,
        "max_ms": max(times) * 1000.0,
        "throughput_min": nbytes / max(times) if max(times) > 0 else 0.0,
        "throughput_avg": (nbytes * len(times)) / total if total > 0 else 0.0,
        "segments": (nbytes + 6) // 7,
        "frames": ((nbytes + 6) // 7) * 2 + 2,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="CANopen SDO segmented throughput benchmark")
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--node-id", type=int, default=1)
    ap.add_argument("--eds", default=os.path.join(HERE, "node1.eds"))
    ap.add_argument("--iterations", type=int, default=5)
    ap.add_argument("--csv", default=None)
    ap.add_argument("--retries", type=int, default=3,
                    help="每次传输失败后的重试次数（cannelloni UDP 无重传，CiA301 的 SDO 也不带重传）")
    args = ap.parse_args()

    network = canopen.Network()
    network.connect(interface=args.interface, channel=args.channel)
    node = network.add_node(args.node_id, args.eds)
    node.sdo.RESPONSE_TIMEOUT = 3.0

    rows: list[tuple[str, bool, str]] = []
    results: dict[str, dict] = {}

    def check(name: str, ok: bool, detail: str = "") -> None:
        rows.append((name, ok, detail))
        print("  %-42s %s  %s" % (name, "PASS" if ok else "FAIL", detail))

    print("=== CANopen SDO segmented benchmark on %s (node %d, %d iterations) ===" %
          (args.channel, args.node_id, args.iterations))
    try:
        node.nmt.state = "OPERATIONAL"
        time.sleep(0.2)

        # 1 KB 上传
        t_up1k, data1k, att1k = bench_upload(node, 0x2001, args.iterations, args.retries)
        r = summary(t_up1k, 1024)
        results["upload_1k"] = r
        check("upload 0x2001 1024 B: 数据图案", data1k == pattern1k(), "len=%d 头 8B=%s" %
              (len(data1k), data1k[:8].hex()))
        check("upload 0x2001 1024 B: 吞吐", r["throughput_avg"] > 1000.0,
              "avg=%.0f B/s (%.1f ms)" % (r["throughput_avg"], r["avg_ms"]))

        # 4 KB 上传
        t_up4k, data4k, att4k = bench_upload(node, 0x2002, args.iterations, args.retries)
        r = summary(t_up4k, 4096)
        results["upload_4k"] = r
        check("upload 0x2002 4096 B: 数据图案", data4k == pattern4k(), "len=%d 头 8B=%s" %
              (len(data4k), data4k[:8].hex()))
        check("upload 0x2002 4096 B: 吞吐", r["throughput_avg"] > 1000.0,
              "avg=%.0f B/s (%.1f ms)" % (r["throughput_avg"], r["avg_ms"]))

        # 1 KB 下载（写入递增图案后读回校验）
        payload = bytes(((i * 3 + 1) & 0xFF) for i in range(1024))
        t_dn1k, attdn1k = bench_download(node, 0x2001, payload, args.iterations, args.retries)
        r = summary(t_dn1k, 1024)
        results["download_1k"] = r
        back = node.sdo.upload(0x2001, 0)
        check("download 0x2001 1024 B: 回读一致", back == payload, "len=%d" % len(back))
        check("download 0x2001 1024 B: 吞吐", r["throughput_avg"] > 1000.0,
              "avg=%.0f B/s (%.1f ms)" % (r["throughput_avg"], r["avg_ms"]))

        # 4 KB 下载
        payload4k = bytes(((i * 5 + 3) & 0xFF) for i in range(4096))
        t_dn4k, attdn4k = bench_download(node, 0x2002, payload4k, args.iterations, args.retries)
        r = summary(t_dn4k, 4096)
        results["download_4k"] = r
        back4k = node.sdo.upload(0x2002, 0)
        check("download 0x2002 4096 B: 回读一致", back4k == payload4k, "len=%d" % len(back4k))
        check("download 0x2002 4096 B: 吞吐", r["throughput_avg"] > 1000.0,
              "avg=%.0f B/s (%.1f ms)" % (r["throughput_avg"], r["avg_ms"]))

        # expedited 小对象读取速率（0x1017，2 B）
        n = 500
        t0 = time.perf_counter()
        for _ in range(n):
            node.sdo.upload(0x1017, 0)
        dt = time.perf_counter() - t0
        rate = n / dt
        check("expedited read 0x1017 x500", rate > 200.0, "%.0f 次/s (%.2f ms/次)" % (rate, dt * 1000.0 / n))

        # 对比表
        print()
        print("  对象        大小     方向     段数   帧数   平均耗时      吞吐        最小耗时")
        print("  ---------- -------- -------- ------ ------ ------------- ------------ ------------")
        for key, label, nbytes in [("upload_1k", "0x2001", 1024), ("upload_4k", "0x2002", 4096),
                                    ("download_1k", "0x2001", 1024), ("download_4k", "0x2002", 4096)]:
            r = results[key]
            direction = "upload" if key.startswith("upload") else "download"
            print("  %-10s %6d B %-8s %6d %6d %8.1f ms %8.0f B/s %8.1f ms" %
                  (label, nbytes, direction, r["segments"], r["frames"], r["avg_ms"],
                   r["throughput_avg"], r["min_ms"]))
        print("  （段数按 7 B/段估算；帧数含请求+响应两方向）")
        print("  传输尝试次数: 1KB-up=%d/%d, 4KB-up=%d/%d, 1KB-down=%d/%d, 4KB-down=%d/%d（成功次数/尝试次数）"
              % (args.iterations, att1k, args.iterations, att4k, args.iterations, attdn1k,
                 args.iterations, attdn4k))

        if args.csv:
            with open(args.csv, "w", encoding="utf-8") as fp:
                fp.write("case,nbytes,min_ms,avg_ms,max_ms,throughput_avg_Bps\n")
                for key, nbytes in [("upload_1k", 1024), ("upload_4k", 4096),
                                    ("download_1k", 1024), ("download_4k", 4096)]:
                    r = results[key]
                    fp.write("%s,%d,%.3f,%.3f,%.3f,%.1f\n" %
                             (key, nbytes, r["min_ms"], r["avg_ms"], r["max_ms"], r["throughput_avg"]))
            print("  csv -> %s" % args.csv)
    finally:
        network.disconnect()

    passed = sum(1 for r in rows if r[1])
    failed = len(rows) - passed
    print()
    print("=== RESULT: %d PASS / %d FAIL ===" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
