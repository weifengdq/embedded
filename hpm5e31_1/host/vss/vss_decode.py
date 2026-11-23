#!/usr/bin/env python3
"""VSS 信号解码：CAN 帧 -> COVESA VSS 路径 + 物理值（含范围检查）

数据源两种：
  1) `--file candump.txt`：candump 文本（板端 can_logger dump 或 candump -L 存档）；
  2) `--channel vcan0 --duration 3`：直接读 SocketCAN（Bench B，板端 gw on + cannelloni）。

信号映射表 MAP 与固件 `samples/app/vss_signals/src/main.c` 的发送端一一对应；
VSS 路径合法性用 `--vss-json`（ref2/vehicle_signal_specification 导出的 JSON 树）校验。

用法：
  python3 vss_decode.py --channel vcan0 --duration 3
  python3 vss_decode.py --file /tmp/candump.txt --json-out /tmp/vss.json
"""
from __future__ import annotations

import argparse
import json
import sys
import time

MAP = [
    # (can_id, name, start_bit, length, factor, offset, unit, vss_path, min, max)
    (0x400, "VehicleSpeed", 0, 16, 0.01, 0.0, "km/h", "Vehicle.Speed", 0.0, 250.0),
    (0x400, "EngineSpeed", 16, 16, 1.0, 0.0, "rpm", "Vehicle.Powertrain.CombustionEngine.Speed", 0.0, 8000.0),
    (0x401, "CoolantTemp", 0, 8, 1.0, -40.0, "celsius", "Vehicle.Powertrain.CombustionEngine.ECT", -40.0, 150.0),
    (0x401, "CurrentGear", 8, 8, 1.0, 0.0, "none", "Vehicle.Powertrain.Transmission.CurrentGear", -1.0, 10.0),
    (0x401, "DriverDoorOpen", 16, 8, 1.0, 0.0, "none",
     "Vehicle.Cabin.Door.Row1.DriverSide.IsOpen", 0.0, 1.0),
    (0x402, "BatterySOC", 0, 16, 0.1, 0.0, "percent",
     "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current", 0.0, 100.0),
]


def vss_leaf_names(path: str) -> set:
    """从 VSS JSON 树里提取所有叶子信号名（path 最后一段），用于校验 VSS 路径存在。"""
    names = set()

    def walk(node):
        if isinstance(node, dict):
            children = node.get("children")
            if children is None and "type" in node:
                names.add(str(node.get("name", "")))
            if isinstance(children, dict):
                for v in children.values():
                    walk(v)
            elif isinstance(children, list):
                for v in children:
                    walk(v)
            else:
                for k, v in node.items():
                    if k not in ("children",):
                        walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)

    walk(json.load(open(path, "r", encoding="utf-8")))
    return names


def parse_candump(path: str):
    frames = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            tpart, cid_part, data_part = line.split()
            t = float(tpart.strip("()"))
            cid = int(cid_part.split("#")[0], 16)
            data = bytes.fromhex(cid_part.split("#")[1]) if "#" in cid_part else b""
        except (ValueError, IndexError):
            continue
        frames.append((t, cid, data))
    return frames


def read_vcan(channel: str, duration: float, interface: str = "socketcan"):
    import can

    bus = can.Bus(interface=interface, channel=channel)
    frames = []
    t_end = time.time() + duration
    while time.time() < t_end:
        msg = bus.recv(timeout=0.2)
        if msg is not None:
            frames.append((time.time(), msg.arbitration_id, bytes(msg.data)))
    bus.shutdown()
    return frames


def extract(data: bytes, start_bit: int, length: int) -> int:
    """小端位域提取（位编号按 CAN 报文 DBC 的 sawtooth 约定；本映射均 8 位对齐，等价于字节偏移）。"""
    byte_off = start_bit // 8
    bit_off = start_bit % 8
    raw = int.from_bytes(data[byte_off:byte_off + 8], "little")
    return (raw >> bit_off) & ((1 << length) - 1)


def main() -> int:
    ap = argparse.ArgumentParser(description="VSS signal decoder")
    ap.add_argument("--file", default=None, help="candump 文本")
    ap.add_argument("--channel", default=None, help="SocketCAN 通道（如 vcan0）")
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--duration", type=float, default=3.0, help="读 vcan0 的时长 s")
    ap.add_argument("--vss-json", default="ref2/vehicle_signal_specification/"
                                          "docs-gen/static/data/outputplus.json")
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--min-frames", type=int, default=10)
    args = ap.parse_args()

    if args.file:
        frames = parse_candump(args.file)
        src = args.file
    elif args.channel:
        frames = read_vcan(args.channel, args.duration, args.interface)
        src = "%s (%.1f s)" % (args.channel, args.duration)
    else:
        print("usage: --file <candump> 或 --channel vcan0"); return 2

    print("=== VSS 解码：%s，共 %d 帧 ===" % (src, len(frames)))
    ids = sorted({f[1] for f in frames if f[1] in {m[0] for m in MAP}})
    print("命中 VSS 帧 ID：%s" % ", ".join("0x%03X" % i for i in ids))

    # VSS 路径校验
    leaf_ok = {}
    try:
        leaves = vss_leaf_names(args.vss_json)
    except OSError:
        leaves = None
        print("WARN: 未找到 VSS JSON（%s），跳过路径校验" % args.vss_json)
    rows = []
    for cid, name, st, ln, factor, off, unit, path, lo, hi in MAP:
        vals = []
        for t, idn, data in frames:
            if idn != cid or len(data) * 8 < st + ln:
                continue
            raw = extract(data, st, ln)
            phys = raw * factor + off
            # 有符号（8 位、偏移 -40 的水温）
            if ln == 8 and off == -40.0 and raw > 127:
                raw -= 256
                phys = raw * factor + off
            vals.append(phys)
        if not vals:
            continue
        leaf = path.split(".")[-1]
        exists = None if leaves is None else (leaf in leaves)
        leaf_ok[path] = exists
        rows.append({
            "path": path, "name": name, "id": "0x%03X" % cid, "unit": unit,
            "count": len(vals), "min": min(vals), "avg": sum(vals) / len(vals), "max": max(vals),
            "in_range": all(lo <= v <= hi for v in vals), "range": (lo, hi),
            "vss_leaf_found": exists,
        })

    print()
    print("  %-62s %-6s %-7s %5s %9s %9s %9s %s" %
          ("VSS 路径", "ID", "单位", "帧数", "min", "avg", "max", "范围"))
    print("  " + "-" * 118)
    for r in rows:
        print("  %-62s %-6s %-7s %5d %9.2f %9.2f %9.2f %s" %
              (r["path"], r["id"], r["unit"], r["count"], r["min"], r["avg"], r["max"],
               "OK" if r["in_range"] else "OUT"))

    total_expected = 6
    got = len(rows)
    in_range = all(r["in_range"] for r in rows) if rows else False
    path_ok = all(v is not False for v in leaf_ok.values())
    print()
    print("  信号覆盖     : %d/%d" % (got, total_expected))
    print("  范围检查     : %s" % ("PASS" if in_range else "FAIL"))
    print("  VSS 路径校验 : %s" % (("PASS" if path_ok else "FAIL") if leaves is not None else "SKIP"))
    print("  帧数         : %d（要求 >= %d）" % (len(frames), args.min_frames))

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fp:
            json.dump({"source": src, "rows": rows}, fp, ensure_ascii=False, indent=2)
        print("  json         : %s" % args.json_out)

    ok = (got >= total_expected - 1) and in_range and path_ok and len(frames) >= args.min_frames
    print("=== RESULT: %s ===" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
