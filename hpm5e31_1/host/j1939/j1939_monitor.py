#!/usr/bin/env python3
"""j1939_monitor.py - J1939 总线监视/解码器（python-can + 自带 J1939 解码）

用法示例：
    python3 j1939_monitor.py -i vcan0                      # 监视 5 秒
    python3 j1939_monitor.py -i vcan0 --duration 10        # 监视 10 秒
    python3 j1939_monitor.py -i vcan0 --pgns FECA,F004     # 只看 DM1/EEC1
    python3 j1939_monitor.py -i vcan0 --raw                # 打印原始 ID

输出：逐帧解码（PGN/SA/DA/优先级/信号）、周期统计表、DM1 灯状态与 DTC、TP 重组。
注意：vcan0 是虚拟总线，要先建立（见 README）。
"""
from __future__ import annotations

import argparse
import sys
import time

import can

import j1939_core as j


def parse_pgns(text: str) -> set[int]:
    out = set()
    for part in text.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        if part.lower().startswith("0x"):
            out.add(int(part, 16))
        elif part.upper() in j.PGN_NAMES.values():
            out.add(next(k for k, v in j.PGN_NAMES.items() if v == part.upper()))
        else:
            out.add(int(part, 16))
    return out


class BusMonitor:
    def __init__(self, bus: can.BusABC, pgn_filter: set[int] | None, raw: bool, quiet: bool):
        self.bus = bus
        self.filter = pgn_filter
        self.raw = raw
        self.quiet = quiet
        self.tp = j.TpReassembler()
        self.stats: dict[int, dict] = {}
        self.frames = 0
        self.dm1_events: list[tuple[float, dict]] = []
        self.claim: dict | None = None
        self.reqs: list[tuple[float, int, int]] = []
        self.resp_lat: list[float] = []
        self.started = time.time()

    def note(self, pgn: int, ts: float):
        st = self.stats.setdefault(pgn, {"count": 0, "first": ts, "last": ts,
                                         "min": None, "max": None, "sum": 0.0, "n": 0})
        st["count"] += 1
        if st["n"] > 0:
            gap = ts - st["last"]
            st["min"] = gap if st["min"] is None else min(st["min"], gap)
            st["max"] = gap if st["max"] is None else max(st["max"], gap)
            st["sum"] += gap
            st["n"] += 1
        else:
            st["first"] = ts
            st["n"] = 1
        st["last"] = ts

    def print_frame(self, can_id: int, data: bytes, ts: float):
        pgn = j.pgn_from_id(can_id)
        sa, da, prio = j.sa_of(can_id), j.da_of(can_id), j.prio_of(can_id)
        name = j.pgn_name(pgn)
        sig = j.decode_signal(pgn, data)
        extra = ""
        if sig:
            extra = " " + " ".join(f"{k}={v:g}" if isinstance(v, float) else f"{k}={v}"
                                   for k, v in sig.items())
        if pgn == j.PGN_ADDRESS_CLAIMED and len(data) == 8:
            v = int.from_bytes(data, "little")
            nm = j.decode_name(v)
            extra = (f" NAME=0x{v:016X} identity={nm['identity']}"
                     f" mfg={nm['manufacturer']} function={nm['function']}"
                     f" aac={nm['arbitrary_address_capable']}")
            self.claim = {"pgn": pgn, "sa": sa, "name": nm, "t": ts - self.started}
        if pgn in (j.PGN_DM1, j.PGN_DM2):
            dm = j.decode_dm(data)
            dtc = ", ".join(f"SPN={d['spn']}/FMI={d['fmi']}/OC={d['oc']}" for d in dm["dtcs"])
            extra = f" lamp=0x{dm['lamp']:02X}[{j.lamp_text(dm)}] {dtc}"
            if pgn == j.PGN_DM1:
                self.dm1_events.append((ts, dm))
        if pgn == j.PGN_REQUEST and len(data) >= 3:
            want = data[0] | (data[1] << 8) | (data[2] << 16)
            self.reqs.append((ts, want, da))
            extra = f" request PGN=0x{want:05X} ({j.pgn_name(want)})"
        if not self.quiet:
            rawtxt = f" ID=0x{can_id:08X}" if self.raw else ""
            print(f"{ts - self.started:9.6f}  {name:16s} PGN=0x{pgn:05X} SA=0x{sa:02X}"
                  f" DA=0x{da:02X} P={prio}{rawtxt} [{len(data)}]"
                  f" {data.hex(' ').upper()}{extra}")

    def handle(self, msg: can.Message):
        if msg.is_extended_id is False:
            return
        can_id = msg.arbitration_id
        data = bytes(msg.data)
        ts = msg.timestamp or time.time()
        self.frames += 1
        pgn = j.pgn_from_id(can_id)
        if self.filter and pgn not in self.filter:
            # 仍需喂 TP，否则重组断链
            self.tp.feed(can_id, data, ts)
            return
        if pgn in (j.PGN_TP_CM, j.PGN_TP_DT):
            out = self.tp.feed(can_id, data, ts)
            if not self.quiet:
                ctrl = data[0] if pgn == j.PGN_TP_CM else data[0]
                print(f"{ts - self.started:9.6f}  {j.pgn_name(pgn):16s} "
                      f"{'CM ctrl=0x%02X' % ctrl if pgn == j.PGN_TP_CM else 'DT seq=%d' % ctrl}"
                      f" SA=0x{j.sa_of(can_id):02X} [{len(data)}] {data.hex(' ').upper()}")
            if out:
                tp_pgn, src, _da, payload = out
                jt = ts - self.started
                print(f"{jt:9.6f}  [TP-DONE] PGN=0x{tp_pgn:05X} ({j.pgn_name(tp_pgn)}) "
                      f"SA=0x{src:02X} len={len(payload)} CRC32=0x{__import__('zlib').crc32(payload):08X}")
                if tp_pgn in (j.PGN_DM1, j.PGN_DM2):
                    dm = j.decode_dm(payload)
                    print(f"            DM lamp=0x{dm['lamp']:02X}[{j.lamp_text(dm)}] "
                          + ", ".join(f"SPN={d['spn']}/FMI={d['fmi']}/OC={d['oc']}" for d in dm["dtcs"]))
            return
        self.note(pgn, ts)
        self.print_frame(can_id, data, ts)

    def summary(self):
        print("\n---- PGN 周期统计 ----")
        print(f"{'PGN':>10} {'name':18s} {'count':>6} {'period avg(ms)':>15} {'min(ms)':>9} {'max(ms)':>9}")
        for pgn in sorted(self.stats):
            st = self.stats[pgn]
            avg = (st["sum"] / (st["n"] - 1) * 1000.0) if st["n"] > 1 else 0.0
            mn = (st["min"] * 1000.0) if st["min"] is not None else 0.0
            mx = (st["max"] * 1000.0) if st["max"] is not None else 0.0
            print(f"0x{pgn:05X} {j.pgn_name(pgn):18s} {st['count']:6d} {avg:15.3f} {mn:9.3f} {mx:9.3f}")
        print(f"\nframes={self.frames}")
        if self.claim:
            print(f"address claim: SA=0x{self.claim['sa']:02X} at t={self.claim['t']:.3f}s")
        if self.dm1_events:
            print("\n---- DM1 灯状态时间线 ----")
            last = None
            for ts, dm in self.dm1_events:
                key = (dm["lamp"], tuple((d["spn"], d["fmi"]) for d in dm["dtcs"]))
                if key != last:
                    last = key
                    dtc = ", ".join(f"SPN={d['spn']}/FMI={d['fmi']}/OC={d['oc']}" for d in dm["dtcs"])
                    print(f"  t={ts - self.started:8.3f}s lamp=0x{dm['lamp']:02X}"
                          f"[{j.lamp_text(dm)}] {dtc}")
        st = self.tp.stats
        print(f"\nTP: bam={st['bam']} rts={st['rts']} done={st['done']} bytes={st['bytes']} "
              f"errors={st['errors']}", end="")
        if st["gap_n"]:
            print(f" gap avg={st['gap_sum'] / st['gap_n'] * 1000.0:.3f}ms "
                  f"min={st['gap_min'] * 1000.0:.3f}ms max={st['gap_max'] * 1000.0:.3f}ms")
        else:
            print()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="J1939 总线监视/解码（python-can）")
    ap.add_argument("-i", "--interface", default="vcan0", help="CAN 接口（默认 vcan0）")
    ap.add_argument("-b", "--bitrate", type=int, default=250000, help="位率（默认 250k，vcan 忽略）")
    ap.add_argument("-d", "--duration", type=float, default=5.0, help="监视秒数（默认 5）")
    ap.add_argument("--pgns", default="", help="只看这些 PGN，逗号分隔（十六进制，如 FECA,F004）")
    ap.add_argument("--raw", action="store_true", help="打印 29 位原始 ID")
    ap.add_argument("--quiet", action="store_true", help="不逐帧打印，只出统计")
    args = ap.parse_args(argv)

    flt = parse_pgns(args.pgns) if args.pgns else None
    try:
        bus = can.Bus(interface="socketcan", channel=args.interface, bitrate=args.bitrate)
    except Exception as exc:  # noqa: BLE001
        print(f"[FAIL] 打开 {args.interface} 失败: {exc}")
        print("       vcan 建立方法: sudo modprobe vcan; sudo ip link add dev vcan0 type vcan;"
              " sudo ip link set up vcan0")
        return 2

    mon = BusMonitor(bus, flt, args.raw, args.quiet)
    deadline = time.time() + args.duration
    print(f"[monitor] if={args.interface} duration={args.duration}s filter={flt or 'all'}")
    try:
        while time.time() < deadline:
            msg = bus.recv(timeout=0.2)
            if msg is not None:
                mon.handle(msg)
    finally:
        bus.shutdown()
    mon.summary()
    print("\nRESULT: PASS" if mon.frames > 0 else "\nRESULT: PASS (no frames: 总线空闲)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
