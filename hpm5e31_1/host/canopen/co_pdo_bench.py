#!/usr/bin/env python3
"""CANopen PDO 同步/事件传输实测（周期抖动、端到端延迟、丢帧）。

Bench B: 上位机 vcan0 <-- cannelleni/UDP --> 板端 CAN1 网关 <--> CAN 总线 <--> DUT(CAN0, 节点 1)
本脚本用 python-can 直接发 SYNC / 收 TPDO，用 python-canopen 做 SDO 配置与校验。

用法:
    python3 co_pdo_bench.py                     # 10 ms SYNC, 5 s
    python3 co_pdo_bench.py --period-ms 5 --duration 3
    python3 co_pdo_bench.py --csv pdo.csv

依赖: python-can, python-canopen
"""
from __future__ import annotations

import argparse
import os
import statistics
import sys
import time

import can
import canopen

HERE = os.path.dirname(os.path.abspath(__file__))
TPDO1_ID = 0x180
TPDO2_ID = 0x280
RPDO1_ID = 0x200
SYNC_ID = 0x080


def sdo_u32(node: canopen.RemoteNode, index: int, sub: int = 0) -> int:
    return int.from_bytes(node.sdo.upload(index, sub), "little")


def sdo_write(node: canopen.RemoteNode, index: int, sub: int, value: int, size: int) -> None:
    node.sdo.download(index, sub, value.to_bytes(size, "little"))


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    data = sorted(values)
    k = (len(data) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(data) - 1)
    return data[lo] + (data[hi] - data[lo]) * (k - lo)


def bench_sync_tpdo(bus: can.BusABC, node: canopen.RemoteNode, node_id: int,
                    period_ms: float, duration_s: float) -> dict:
    """按周期发 SYNC，收 TPDO1，统计延迟/间隔/丢帧。"""
    tpdo1 = TPDO1_ID + node_id
    latencies: list[float] = []
    arrivals: list[float] = []
    sync_times: list[float] = []
    n_sync = 0
    t_end = time.perf_counter() + duration_s

    while time.perf_counter() < t_end:
        t0 = time.perf_counter()
        bus.send(can.Message(arbitration_id=SYNC_ID, data=b"", is_extended_id=False))
        n_sync += 1
        sync_times.append(t0)
        # 在下一个 SYNC 之前收集 TPDO1（最多等到周期结束 + 2 ms 余量）
        deadline = t0 + period_ms / 1000.0 + 0.002
        while True:
            remain = deadline - time.perf_counter()
            if remain <= 0:
                break
            msg = bus.recv(timeout=remain)
            if msg is None:
                break
            if msg.arbitration_id == tpdo1 and msg.dlc >= 6:
                t1 = time.perf_counter()
                latencies.append(t1 - t0)
                arrivals.append(t1)
                break
        # 周期补齐
        remain = period_ms / 1000.0 - (time.perf_counter() - t0)
        if remain > 0:
            time.sleep(remain)

    intervals = [b - a for a, b in zip(arrivals, arrivals[1:])]
    jitter_dev = [abs(i * 1000.0 - period_ms) for i in intervals]
    return {
        "n_sync": n_sync,
        "n_tpdo": len(arrivals),
        "loss_pct": 0.0 if n_sync == 0 else (n_sync - len(arrivals)) * 100.0 / n_sync,
        "lat_min_ms": min(latencies) * 1000.0 if latencies else 0.0,
        "lat_avg_ms": statistics.fmean(latencies) * 1000.0 if latencies else 0.0,
        "lat_p99_ms": percentile(latencies, 99) * 1000.0 if latencies else 0.0,
        "lat_max_ms": max(latencies) * 1000.0 if latencies else 0.0,
        "jit_std_ms": statistics.pstdev(intervals) * 1000.0 if len(intervals) > 1 else 0.0,
        "jit_max_ms": max(jitter_dev) if jitter_dev else 0.0,
        "period_avg_ms": statistics.fmean(intervals) * 1000.0 if intervals else 0.0,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="CANopen PDO benchmark")
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--node-id", type=int, default=1)
    ap.add_argument("--eds", default=os.path.join(HERE, "node1.eds"))
    ap.add_argument("--period-ms", type=float, default=10.0)
    ap.add_argument("--duration", type=float, default=5.0)
    ap.add_argument("--csv", default=None)
    args = ap.parse_args()

    bus = can.Bus(interface=args.interface, channel=args.channel)
    network = canopen.Network()
    network.connect(interface=args.interface, channel=args.channel)
    node = network.add_node(args.node_id, args.eds)
    node.sdo.RESPONSE_TIMEOUT = 1.5

    rows: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        rows.append((name, ok, detail))
        print("  %-40s %s  %s" % (name, "PASS" if ok else "FAIL", detail))

    print("=== CANopen PDO benchmark on %s (node %d, SYNC %.1f ms) ===" % (args.channel, args.node_id,
                                                                           args.period_ms))
    try:
        node.nmt.state = "OPERATIONAL"
        time.sleep(0.2)

        # 配置：TPDO1 同步型（transmission type=1）、TPDO2 事件型 1000 ms
        sdo_write(node, 0x1800, 2, 1, 1)
        sdo_write(node, 0x1801, 2, 254, 1)
        sdo_write(node, 0x1801, 5, 1000, 2)
        sdo_write(node, 0x1019, 0, 0, 1)
        map_cnt = sdo_u32(node, 0x1A00, 0)
        map1 = sdo_u32(node, 0x1A00, 1)
        map2 = sdo_u32(node, 0x1A00, 2)
        check("TPDO1 mapping (2 objects)", map_cnt == 2, "cnt=%d 0x%08X 0x%08X" % (map_cnt, map1, map2))
        check("TPDO1 mapping entry1 = 0x2101:0/16",
              (map1 >> 16) == 0x2101 and ((map1 >> 8) & 0xFF) == 0 and (map1 & 0xFF) == 16, "0x%08X" % map1)
        check("TPDO1 mapping entry2 = 0x2102:0/32",
              (map2 >> 16) == 0x2102 and (map2 & 0xFF) == 32, "0x%08X" % map2)

        # 1) TPDO1 同步
        print("[bench] TPDO1 sync: SYNC %.1f ms x %.1f s" % (args.period_ms, args.duration))
        r1 = bench_sync_tpdo(bus, node, args.node_id, args.period_ms, args.duration)
        check("TPDO1 同步: 丢帧", r1["loss_pct"] <= 0.1, "%.2f%% (%d/%d)" % (r1["loss_pct"], r1["n_tpdo"],
                                                                             r1["n_sync"]))
        check("TPDO1 同步: 平均延迟", r1["lat_avg_ms"] < 20.0, "avg=%.2f ms p99=%.2f max=%.2f" %
              (r1["lat_avg_ms"], r1["lat_p99_ms"], r1["lat_max_ms"]))
        check("TPDO1 同步: 周期抖动 σ", r1["jit_std_ms"] < 2.0, "σ=%.2f ms max|dev|=%.2f ms avg=%.2f ms" %
              (r1["jit_std_ms"], r1["jit_max_ms"], r1["period_avg_ms"]))

        # 2) TPDO2 事件型 1000 ms：5 s 内约 5 帧
        n0 = 0
        t_end = time.perf_counter() + 5.0
        while time.perf_counter() < t_end:
            msg = bus.recv(timeout=0.2)
            if msg is not None and msg.arbitration_id == (TPDO2_ID + args.node_id) and msg.dlc >= 6:
                n0 += 1
        check("TPDO2 事件型 1000 ms (5 s)", 3 <= n0 <= 7, "%d frames" % n0)

        # 3) RPDO1 控制回环：写 0x2100 后 SDO 读回
        ok = True
        detail = []
        for val in (0x0001, 0x0002, 0x0000):
            bus.send(can.Message(arbitration_id=RPDO1_ID + args.node_id,
                                 data=val.to_bytes(2, "little"), is_extended_id=False))
            time.sleep(0.05)
            got = sdo_u32(node, 0x2100)
            detail.append("%04X->%04X" % (val, got))
            if got != val:
                ok = False
        check("RPDO1 -> 0x2100 回环", ok, " ".join(detail))

        # 汇总表
        print()
        print("  指标                     数值")
        print("  ------------------------ ------------------------------")
        for key, label, fmt in [
            ("n_sync", "SYNC 帧数", "%d"),
            ("n_tpdo", "TPDO1 帧数", "%d"),
            ("loss_pct", "丢帧率", "%.2f %%"),
            ("period_avg_ms", "TPDO1 平均周期", "%.2f ms"),
            ("jit_std_ms", "周期抖动 σ", "%.2f ms"),
            ("jit_max_ms", "周期最大偏差", "%.2f ms"),
            ("lat_min_ms", "端到端延迟 min", "%.2f ms"),
            ("lat_avg_ms", "端到端延迟 avg", "%.2f ms"),
            ("lat_p99_ms", "端到端延迟 p99", "%.2f ms"),
            ("lat_max_ms", "端到端延迟 max", "%.2f ms"),
        ]:
            print("  %-24s " % label + fmt % r1[key])

        if args.csv:
            with open(args.csv, "w", encoding="utf-8") as fp:
                fp.write("metric,value\n")
                for key, value in r1.items():
                    fp.write("%s,%s\n" % (key, value))
            print("  csv -> %s" % args.csv)
    finally:
        network.disconnect()
        bus.shutdown()

    passed = sum(1 for r in rows if r[1])
    failed = len(rows) - passed
    print()
    print("=== RESULT: %d PASS / %d FAIL ===" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
