#!/usr/bin/env python3
"""j1939_tp_test.py - J1939-21 传输协议主机侧测试（BAM / RTS-CTS）

四种模式：
  selftest      同一 vcan0 上开两个 socket，自发自收，验证 BAM/RTS-CTS 实现
                （无需板卡；用于主机侧回归）
  listen        被动监听总线，重组 BAM/CMDT 报文并校验（配合板端 j1939_tp 例程）
  send-bam      按 BAM 发一段 >8 字节数据（默认 50 ms 包间隔）
  send-rts      按 RTS/CTS 发一段点对点数据（等待 CTS 窗口、EOMA）

数据格式：payload[i] = (i*31+7) & 0xFF，末尾附长度；接收方用 CRC32 校验。
"""
from __future__ import annotations

import argparse
import struct
import sys
import threading
import time
import zlib

import can

import j1939_core as j


def make_payload(n: int) -> bytes:
    return bytes(((i * 31 + 7) & 0xFF) for i in range(n))


def show_tp_result(tag: str, ok: bool, nbytes: int, dt: float, gaps: list[float]):
    goodput = nbytes / dt if dt > 0 else 0.0
    gap_txt = ""
    if gaps:
        gap_txt = (f" gap avg={sum(gaps) / len(gaps) * 1000:.3f}ms"
                   f" min={min(gaps) * 1000:.3f}ms max={max(gaps) * 1000:.3f}ms")
    print(f"  {tag}: {'PASS' if ok else 'FAIL'} len={nbytes} time={dt * 1000:.1f}ms "
          f"goodput={goodput:.1f} B/s{gap_txt}")
    return ok


class TpPeer:
    """监听并自动应答 RTS/CTS 的对端（BAM 只重组）。"""

    def __init__(self, bus: can.BusABC, sa: int, pgn: int):
        self.bus = bus
        self.sa = sa
        self.pgn = pgn
        self.tp = j.TpReassembler()
        self.done: list[tuple[bytes, float, list[float]]] = []
        self.cts_sent = 0
        self.eoma_sent = 0
        self.rx_count = 0
        self.err = 0
        self._sessions: dict[int, dict] = {}

    def _send(self, can_id: int, data: bytes):
        msg = can.Message(arbitration_id=can_id, is_extended_id=True, data=data)
        self.bus.send(msg)

    def send_cts(self, src: int, total: int, npkts: int, window: int, pgn: int):
        self._send(j.id_from_pgn(j.PGN_TP_CM, 7, src, self.sa),
                   bytes([j.TP_CM_CTS, total & 0xFF, (total >> 8) & 0xFF, npkts, window,
                          pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF]))
        self.cts_sent += 1

    def send_eoma(self, src: int, total: int, npkts: int, pgn: int):
        self._send(j.id_from_pgn(j.PGN_TP_CM, 7, src, self.sa),
                   bytes([j.TP_CM_EOMA, total & 0xFF, (total >> 8) & 0xFF, npkts, 0xFF,
                          pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF]))
        self.eoma_sent += 1

    def loop(self, stop: threading.Event, duration: float):
        end = time.time() + duration
        sessions: dict[int, dict] = {}
        while not stop.is_set() and time.time() < end:
            msg = self.bus.recv(timeout=0.02)
            if msg is None or not msg.is_extended_id:
                continue
            cid, data = msg.arbitration_id, bytes(msg.data)
            pgn = j.pgn_from_id(cid)
            ts = msg.timestamp or time.time()
            self.rx_count += 1
            if pgn == j.PGN_TP_CM and len(data) == 8:
                ctrl = data[0]
                total = data[1] | (data[2] << 8)
                npkts = data[3]
                tp_pgn = data[5] | (data[6] << 8) | (data[7] << 16)
                src = j.sa_of(cid)
                if ctrl == j.TP_CM_BAM:
                    self._bam_start = ts
                if ctrl == j.TP_CM_RTS:
                    win = min(npkts, 16)
                    self._sessions[src] = {"total": total, "npkts": npkts, "pgn": tp_pgn,
                                           "next": 1, "buf": bytearray(), "t0": ts,
                                           "gaps": [], "last": ts, "win": win}
                    self.send_cts(src, total, npkts, win, tp_pgn)
                self.tp.feed(cid, data, ts)
            elif pgn == j.PGN_TP_DT and len(data) >= 2:
                src = j.sa_of(cid)
                sess = self._sessions.get(src)
                if sess is not None:
                    if data[0] == sess["next"]:
                        sess["buf"].extend(data[1:])
                        sess["next"] += 1
                        if sess["next"] > 2:
                            sess["gaps"].append(ts - sess["last"])
                        sess["last"] = ts
                        if len(sess["buf"]) >= sess["total"]:
                            payload = bytes(sess["buf"][:sess["total"]])
                            self.done.append((payload, ts - sess["t0"], sess["gaps"]))
                            if not (sess["pgn"] == j.PGN_TP_DT):
                                self.send_eoma(src, sess["total"], sess["npkts"], sess["pgn"])
                            del self._sessions[src]
                        else:
                            sess["win"] -= 1
                            if sess["win"] <= 0:
                                remain = sess["npkts"] - (sess["next"] - 1)
                                win = min(remain, 16)
                                sess["win"] = win
                                self.send_cts(src, sess["total"], sess["npkts"], win, sess["pgn"])
                    else:
                        self.err += 1
                        del self._sessions[src]
                out = self.tp.feed(cid, data, ts)
                if out:
                    tp_pgn, src, _da, payload = out
                    self.done.append((payload, ts - getattr(self, "_bam_start", ts), []))


def selftest(interface: str, dest_sa: int) -> int:
    print(f"[selftest] two sockets on {interface}")
    tx = can.Bus(interface="socketcan", channel=interface)
    rx = can.Bus(interface="socketcan", channel=interface)
    peer = TpPeer(rx, dest_sa, 0xEF00)
    stop = threading.Event()
    th = threading.Thread(target=peer.loop, args=(stop, 30.0), daemon=True)
    th.start()

    pass_n = fail_n = 0
    pgn = 0xEF00

    def run_bam(nbytes: int, gap_ms: float = 50.0):
        nonlocal pass_n, fail_n
        payload = make_payload(nbytes)
        frames = j.build_bam_frames(pgn, payload, 0x00)
        done_before = len(peer.done)
        t0 = time.time()
        for idx, (cid, data) in enumerate(frames):
            tx.send(can.Message(arbitration_id=cid, is_extended_id=True, data=data))
            if idx < len(frames) - 1:
                time.sleep(gap_ms / 1000.0)
        deadline = time.time() + 3.0
        while len(peer.done) == done_before and time.time() < deadline:
            time.sleep(0.005)
        dt = time.time() - t0
        if len(peer.done) > done_before:
            data, tdone, gaps = peer.done[-1]
            ok = (data == payload) and (zlib.crc32(data) == zlib.crc32(payload))
            ok = show_tp_result(f"BAM {nbytes}B", ok, nbytes, dt, gaps) and ok
        else:
            ok = show_tp_result(f"BAM {nbytes}B", False, nbytes, dt, [])
        pass_n += int(ok)
        fail_n += int(not ok)

    def flush(bus):
        while bus.recv(timeout=0.0) is not None:
            pass

    def run_rts(nbytes: int, gap_ms: float = 1.0):
        nonlocal pass_n, fail_n
        flush(tx)
        payload = make_payload(nbytes)
        npkts = (nbytes + 6) // 7
        cm = bytes([j.TP_CM_RTS, nbytes & 0xFF, (nbytes >> 8) & 0xFF, npkts, 0xFF,
                    pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF])
        done_before = len(peer.done)
        t0 = time.time()
        seq = 1
        deadline = time.time() + 8.0
        retries = 0
        tx.send(can.Message(arbitration_id=j.id_from_pgn(j.PGN_TP_CM, 7, dest_sa, 0x00),
                            is_extended_id=True, data=cm))
        got_cts = False
        while seq <= npkts and time.time() < deadline:
            msg = tx.recv(timeout=0.2)
            if msg is None or not msg.is_extended_id:
                if not got_cts and retries < 3 and time.time() - t0 > 1.0 * (retries + 1):
                    retries += 1
                    tx.send(can.Message(arbitration_id=j.id_from_pgn(j.PGN_TP_CM, 7, dest_sa, 0x00),
                                        is_extended_id=True, data=cm))
                continue
            mpgn = j.pgn_from_id(msg.arbitration_id)
            if mpgn == j.PGN_TP_CM and bytes(msg.data)[0] == j.TP_CM_CTS:
                got_cts = True
                win = bytes(msg.data)[4]
                for _ in range(win):
                    if seq > npkts:
                        break
                    off = (seq - 1) * 7
                    chunk = payload[off:off + 7]
                    tx.send(can.Message(
                        arbitration_id=j.id_from_pgn(j.PGN_TP_DT, 7, dest_sa, 0x00),
                        is_extended_id=True, data=bytes([seq]) + chunk))
                    seq += 1
                    time.sleep(gap_ms / 1000.0)
        # 等 EOMA
        eoma_deadline = time.time() + 3.0
        got_eoma = False
        while time.time() < eoma_deadline:
            msg = tx.recv(timeout=0.2)
            if msg is None or not msg.is_extended_id:
                continue
            if j.pgn_from_id(msg.arbitration_id) == j.PGN_TP_CM and bytes(msg.data)[0] == j.TP_CM_EOMA:
                got_eoma = True
                break
        dt = time.time() - t0
        if len(peer.done) > done_before:
            data, _, gaps = peer.done[-1]
            ok = (data == payload) and got_eoma
            ok = show_tp_result(f"RTS/CTS {nbytes}B", ok, nbytes, dt, gaps) and ok
        else:
            ok = show_tp_result(f"RTS/CTS {nbytes}B", False, nbytes, dt, [])
        pass_n += int(ok)
        fail_n += int(not ok)

    try:
        run_bam(100)
        run_bam(500)
        run_rts(100)
        run_rts(500)
    finally:
        stop.set()
        th.join(timeout=2.0)
        tx.shutdown()
        rx.shutdown()
    print(f"\nRESULT: {'PASS' if fail_n == 0 else 'FAIL'} ({pass_n} PASS / {fail_n} FAIL), "
          f"peer: rx={peer.rx_count} cts={peer.cts_sent} eoma={peer.eoma_sent} err={peer.err}")
    return 0 if fail_n == 0 else 1


def listen(interface: str, duration: float, sa: int) -> int:
    bus = can.Bus(interface="socketcan", channel=interface)
    peer = TpPeer(bus, sa, 0xEF00)
    stop = threading.Event()
    th = threading.Thread(target=peer.loop, args=(stop, duration), daemon=True)
    th.start()
    try:
        th.join(timeout=duration + 2.0)
    finally:
        stop.set()
        bus.shutdown()
    ok = bool(peer.done)
    for payload, dt, gaps in peer.done:
        good = payload == make_payload(len(payload))
        show_tp_result(f"TP {len(payload)}B", good, len(payload), dt, gaps)
        ok &= good
    print(f"received {len(peer.done)} TP messages, cts={peer.cts_sent} eoma={peer.eoma_sent} "
          f"err={peer.err}")
    print(f"RESULT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def send_bam(interface: str, nbytes: int, sa: int, pgn: int, gap_ms: float) -> int:
    bus = can.Bus(interface="socketcan", channel=interface)
    payload = make_payload(nbytes)
    frames = j.build_bam_frames(pgn, payload, sa)
    t0 = time.time()
    try:
        for idx, (cid, data) in enumerate(frames):
            bus.send(can.Message(arbitration_id=cid, is_extended_id=True, data=data))
            print(f"  tx {idx + 1}/{len(frames)} id=0x{cid:08X} {data.hex(' ').upper()}")
            if idx < len(frames) - 1:
                time.sleep(gap_ms / 1000.0)
    finally:
        bus.shutdown()
    dt = time.time() - t0
    print(f"BAM sent: {nbytes}B in {len(frames)} frames, {dt * 1000:.1f} ms, "
          f"{nbytes / dt:.1f} B/s")
    return 0


def send_rts(interface: str, nbytes: int, sa: int, dest: int, pgn: int, gap_ms: float) -> int:
    bus = can.Bus(interface="socketcan", channel=interface)
    payload = make_payload(nbytes)
    npkts = (nbytes + 6) // 7
    try:
        cm = bytes([j.TP_CM_RTS, nbytes & 0xFF, (nbytes >> 8) & 0xFF, npkts, 0xFF,
                    pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF])
        t0 = time.time()
        bus.send(can.Message(arbitration_id=j.id_from_pgn(j.PGN_TP_CM, 7, dest, sa),
                             is_extended_id=True, data=cm))
        seq = 1
        while seq <= npkts:
            msg = bus.recv(timeout=3.0)
            if msg is None or not msg.is_extended_id:
                print("timeout waiting CTS")
                return 1
            mpgn = j.pgn_from_id(msg.arbitration_id)
            d = bytes(msg.data)
            if mpgn == j.PGN_TP_CM and d[0] == j.TP_CM_CTS:
                win = d[4]
                for _ in range(min(win, npkts - seq + 1)):
                    off = (seq - 1) * 7
                    chunk = payload[off:off + 7]
                    bus.send(can.Message(arbitration_id=j.id_from_pgn(j.PGN_TP_DT, 7, dest, sa),
                                         is_extended_id=True, data=bytes([seq]) + chunk))
                    seq += 1
                    time.sleep(gap_ms / 1000.0)
        print(f"RTS/CTS sent: {nbytes}B, {npkts} pkts, {(time.time() - t0) * 1000:.1f} ms")
        return 0
    finally:
        bus.shutdown()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="J1939-21 BAM / RTS-CTS 测试")
    ap.add_argument("--mode", choices=["selftest", "listen", "send-bam", "send-rts"],
                    default="selftest")
    ap.add_argument("-i", "--interface", default="vcan0")
    ap.add_argument("--bytes", type=int, default=500, help="payload 字节数（<=1785）")
    ap.add_argument("--sa", type=lambda x: int(x, 0), default=0x00, help="源地址")
    ap.add_argument("--dest", type=lambda x: int(x, 0), default=0xF9, help="目标地址")
    ap.add_argument("--pgn", type=lambda x: int(x, 0), default=0xEF00, help="被传输的 PGN")
    ap.add_argument("--gap-ms", type=float, default=50.0, help="BAM 包间隔（默认 50 ms）")
    ap.add_argument("--duration", type=float, default=10.0, help="listen 秒数")
    args = ap.parse_args(argv)

    if args.bytes > j.TP_MAX_LEN:
        print(f"bytes 不能超过 {j.TP_MAX_LEN}")
        return 2
    if args.mode == "selftest":
        return selftest(args.interface, args.dest)
    if args.mode == "listen":
        return listen(args.interface, args.duration, args.dest)
    if args.mode == "send-bam":
        return send_bam(args.interface, args.bytes, args.sa, args.pgn, args.gap_ms)
    return send_rts(args.interface, args.bytes, args.sa, args.dest, args.pgn, 1.0)


if __name__ == "__main__":
    sys.exit(main())