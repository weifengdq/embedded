#!/usr/bin/env python3
"""CANopen node-1 conformance smoke test (CiA 301) over SocketCAN.

在 vcan0（由 host/can_bridge 的 cannelloni 网关映射到板端 CAN 总线）上，
用 python-canopen 对节点 1 做 NMT / SDO / 心跳 / EMCY 全流程测试，输出 PASS/FAIL 表。

用法:
    python3 co_node_test.py                       # 默认 vcan0, node 1
    python3 co_node_test.py --channel vcan0 --node-id 1 --eds node1.eds
    python3 co_node_test.py --no-nmt-reset        # 跳过 NMT 复位测试（会重启节点）

依赖: python-can, python-canopen（已装在 $HOME/pyenv）
"""
from __future__ import annotations

import argparse
import os
import sys
import time

import can
import canopen

HERE = os.path.dirname(os.path.abspath(__file__))


class Report:
    def __init__(self) -> None:
        self.rows: list[tuple[str, bool, str]] = []

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        self.rows.append((name, ok, detail))
        print("  %-38s %s  %s" % (name, "PASS" if ok else "FAIL", detail))
        return ok

    def dump(self) -> int:
        passed = sum(1 for r in self.rows if r[1])
        failed = len(self.rows) - passed
        print()
        print("=== RESULT: %d PASS / %d FAIL ===" % (passed, failed))
        return 0 if failed == 0 else 1


class FrameRecorder(can.Listener):
    """记录指定 COB-ID 的帧（心跳 / EMCY）。"""

    def __init__(self, hb_id: int, emcy_id: int) -> None:
        self.hb_id = hb_id
        self.emcy_id = emcy_id
        self.heartbeats: list[tuple[float, int]] = []
        self.emcys: list[tuple[float, bytes]] = []

    def on_message_received(self, msg: can.Message) -> None:  # noqa: D102
        if msg.arbitration_id == self.hb_id and msg.dlc >= 1:
            self.heartbeats.append((msg.timestamp, msg.data[0]))
        elif msg.arbitration_id == self.emcy_id and msg.dlc >= 3:
            self.emcys.append((msg.timestamp, bytes(msg.data)))

    def wait_hb_state(self, state: int, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(s == state for _t, s in self.heartbeats):
                return True
            time.sleep(0.01)
        return False


def sdo_read_u32(node: canopen.RemoteNode, index: int, sub: int = 0) -> int:
    data = node.sdo.upload(index, sub)
    return int.from_bytes(data, "little")


def main() -> int:
    ap = argparse.ArgumentParser(description="CANopen node conformance smoke test")
    ap.add_argument("--interface", default="socketcan", help="python-can interface (default socketcan)")
    ap.add_argument("--channel", default="vcan0", help="CAN channel (default vcan0)")
    ap.add_argument("--node-id", type=int, default=1)
    ap.add_argument("--eds", default=os.path.join(HERE, "node1.eds"))
    ap.add_argument("--timeout", type=float, default=1.5, help="SDO timeout s")
    ap.add_argument("--no-nmt-reset", action="store_true", help="skip NMT reset communication test")
    args = ap.parse_args()

    rep = Report()
    bus = can.Bus(interface=args.interface, channel=args.channel)
    rec = FrameRecorder(0x700 + args.node_id, 0x080 + args.node_id)
    notifier = can.Notifier(bus, [rec], timeout=0.2)

    network = canopen.Network()
    network.connect(interface=args.interface, channel=args.channel)
    node = network.add_node(args.node_id, args.eds)
    node.sdo.RESPONSE_TIMEOUT = args.timeout
    print("=== CANopen node %d test on %s (%s) ===" % (args.node_id, args.channel, args.interface))

    try:
        # 1) NMT 启动
        node.nmt.state = "OPERATIONAL"
        ok = rec.wait_hb_state(5, 3.0)
        rep.check("NMT start -> heartbeat state 5", ok,
                  "hb=%d" % len(rec.heartbeats))

        # 2) SDO expedited 读
        v = sdo_read_u32(node, 0x1000)
        rep.check("SDO read 0x1000 deviceType", v == 0x00000191, "0x%08X" % v)
        v = sdo_read_u32(node, 0x1018, 1)
        rep.check("SDO read 0x1018:1 vendor-ID", v == 0x0000A531, "0x%08X" % v)
        v = sdo_read_u32(node, 0x1017)
        rep.check("SDO read 0x1017 heartbeat", v == 1000, "%d ms" % v)

        # 3) SDO segmented 上传（VISIBLE_STRING 32 B）
        name = node.sdo.upload(0x1008, 0)
        text = name.split(b"\x00")[0].decode("ascii", "replace")
        rep.check("SDO segmented upload 0x1008", text.startswith("HPM5E31"), "%d B %r" % (len(name), text))

        # 4) SDO 写 + 读回
        node.sdo.download(0x1017, 0, (200).to_bytes(2, "little"))
        v = sdo_read_u32(node, 0x1017)
        rep.check("SDO write 0x1017 = 200", v == 200, "read back %d" % v)

        # 5) 心跳周期实测（200 ms）
        hb0 = len(rec.heartbeats)
        t0 = time.monotonic()
        time.sleep(2.2)
        hb = rec.heartbeats[hb0:]
        stamps = [t for t, _s in hb]
        intervals = [b - a for a, b in zip(stamps, stamps[1:])]
        avg = (sum(intervals) / len(intervals)) if intervals else 0.0
        jitter = max((abs(i - 0.2) for i in intervals), default=0.0)
        rep.check("heartbeat 200 ms (2.2 s)", 8 <= len(hb) <= 13,
                  "%d frames, avg=%.1f ms, max|dev|=%.1f ms" % (len(hb), avg * 1000.0, jitter * 1000.0))

        # 恢复默认 1000 ms
        node.sdo.download(0x1017, 0, (1000).to_bytes(2, "little"))
        v = sdo_read_u32(node, 0x1017)
        rep.check("SDO write 0x1017 restore 1000", v == 1000, "read back %d" % v)

        # 6) EMCY 注入（写 0x2106 触发）
        n0 = len(rec.emcys)
        node.sdo.download(0x2106, 0, (0x5A5A0001).to_bytes(4, "little"))
        time.sleep(0.3)
        emcys = rec.emcys[n0:]
        code = (emcys[0][1][0] | (emcys[0][1][1] << 8)) if emcys else -1
        rep.check("EMCY 0x081 on 0x2106 write", len(emcys) >= 1 and code == 0x0001,
                  "count=%d code=0x%04X" % (len(emcys), code & 0xFFFF))
        v = sdo_read_u32(node, 0x2107)
        rep.check("EMCY counter 0x2107", v >= 1, "%d" % v)
        node.sdo.download(0x2106, 0, (0).to_bytes(4, "little"))

        # 7) NMT stop / start
        node.nmt.state = "STOPPED"
        ok = rec.wait_hb_state(4, 2.0)
        rep.check("NMT stop -> heartbeat state 4", ok)
        node.nmt.state = "OPERATIONAL"
        ok = rec.wait_hb_state(5, 2.0)
        rep.check("NMT start -> heartbeat state 5", ok)

        # 8) NMT reset communication -> boot-up 0x00
        if not args.no_nmt_reset:
            n0 = len(rec.heartbeats)
            node.nmt.state = "RESET COMMUNICATION"
            deadline = time.monotonic() + 3.0
            bootup = False
            while time.monotonic() < deadline:
                if any(s == 0 for _t, s in rec.heartbeats[n0:]):
                    bootup = True
                    break
                time.sleep(0.01)
            rep.check("NMT reset communication -> boot-up", bootup)
            node.nmt.state = "OPERATIONAL"
            rep.check("NMT start after reset", rec.wait_hb_state(5, 3.0))
    finally:
        notifier.stop()
        network.disconnect()
        bus.shutdown()

    return rep.dump()


if __name__ == "__main__":
    sys.exit(main())
