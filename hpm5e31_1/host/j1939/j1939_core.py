#!/usr/bin/env python3
"""j1939_core.py - SAE J1939 帧编解码（无第三方 J1939 依赖）

python-can 4.x 已移除 can.j1939 模块，所以这里自带：
  * 29 位 ID <-> PGN 互转（PDU1/PDU2）
  * NAME 64 位解码
  * TP（BAM / RTS-CTS）重组与发送序列
  * DM1/DM2（灯 + SPN/FMI/OC）解码
  * 常用周期 PGN 信号换算（EEC1 / ET1 / CCVS1）

只依赖标准库，便于在任意 Linux 主机上跑。
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

# ---- PGN ---------------------------------------------------------------
PGN_ACK = 0xE800
PGN_REQUEST = 0xEA00
PGN_TP_DT = 0xEB00
PGN_TP_CM = 0xEC00
PGN_ADDRESS_CLAIMED = 0xEE00
PGN_EEC1 = 0xF004
PGN_CCVS1 = 0xFEF1
PGN_ET1 = 0xFEEE
PGN_LFE = 0xFEEF
PGN_DM1 = 0xFECA
PGN_DM2 = 0xFECB
PGN_DM3 = 0xFECC
PGN_DM11 = 0xFED3

PGN_NAMES = {
    PGN_ACK: "ACK",
    PGN_REQUEST: "REQUEST",
    PGN_TP_DT: "TP.DT",
    PGN_TP_CM: "TP.CM",
    PGN_ADDRESS_CLAIMED: "ADDRESS_CLAIMED",
    PGN_EEC1: "EEC1",
    PGN_CCVS1: "CCVS1",
    PGN_ET1: "ET1",
    PGN_LFE: "LFE",
    PGN_DM1: "DM1",
    PGN_DM2: "DM2",
    PGN_DM3: "DM3",
    PGN_DM11: "DM11",
}

PGN_PRIORITY = {
    PGN_EEC1: 3, PGN_ET1: 6, PGN_CCVS1: 6, PGN_LFE: 6,
    PGN_DM1: 6, PGN_DM2: 6, PGN_ADDRESS_CLAIMED: 6,
    PGN_REQUEST: 6, PGN_ACK: 6, PGN_TP_CM: 7, PGN_TP_DT: 7,
}

TP_CM_RTS = 0x10
TP_CM_CTS = 0x11
TP_CM_EOMA = 0x13
TP_CM_BAM = 0x20
TP_CM_ABORT = 0xFF

TP_MAX_LEN = 1785


def pgn_from_id(can_id: int) -> int:
    """29 位 CAN ID -> PGN（PDU1 的 PS 是 DA，不参与 PGN）。"""
    pf = (can_id >> 16) & 0xFF
    ps = (can_id >> 8) & 0xFF
    edp = (can_id >> 25) & 0x1
    dp = (can_id >> 24) & 0x1
    if pf < 240:
        return (edp << 17) | (dp << 16) | (pf << 8)
    return (edp << 17) | (dp << 16) | (pf << 8) | ps


def id_from_pgn(pgn: int, priority: int, da: int, sa: int) -> int:
    pf = (pgn >> 8) & 0xFF
    ps = (da & 0xFF) if pf < 240 else (pgn & 0xFF)
    return ((priority & 7) << 26) | (((pgn >> 17) & 1) << 25) | (((pgn >> 16) & 1) << 24) \
        | (pf << 16) | (ps << 8) | (sa & 0xFF)


def is_pdu1(pgn: int) -> bool:
    return ((pgn >> 8) & 0xFF) < 240


def prio_of(can_id: int) -> int:
    return (can_id >> 26) & 7


def sa_of(can_id: int) -> int:
    return can_id & 0xFF


def da_of(can_id: int) -> int:
    pgn = pgn_from_id(can_id)
    return ((can_id >> 8) & 0xFF) if is_pdu1(pgn) else 0xFF


def pgn_name(pgn: int) -> str:
    return PGN_NAMES.get(pgn, f"PGN_0x{pgn:05X}")


# ---- NAME --------------------------------------------------------------
def decode_name(value: int) -> dict:
    return {
        "identity": (value >> 43) & 0x1FFFFF,
        "manufacturer": (value >> 32) & 0x7FF,
        "ecu_instance": (value >> 29) & 0x7,
        "function_instance": (value >> 24) & 0x1F,
        "function": (value >> 16) & 0xFF,
        "reserved": (value >> 15) & 0x1,
        "vehicle_system": (value >> 8) & 0x7F,
        "vehicle_system_instance": (value >> 4) & 0xF,
        "industry_group": (value >> 1) & 0x7,
        "arbitrary_address_capable": value & 1,
    }


# ---- DM1/DM2 -----------------------------------------------------------
def decode_dm(data: bytes) -> dict:
    """解码 DM1/DM2：灯状态 + DTC 列表（SPN/FMI/OC）。"""
    if len(data) < 2:
        return {"lamp": 0, "mil": 0, "rsl": 0, "awl": 0, "pl": 0, "flash": 0, "dtcs": []}
    lamp = data[0]
    out = {
        "lamp": lamp,
        "mil": lamp & 0x3,
        "rsl": (lamp >> 2) & 0x3,
        "awl": (lamp >> 4) & 0x3,
        "pl": (lamp >> 6) & 0x3,
        "flash": data[1],
        "dtcs": [],
    }
    n = (len(data) - 2) // 4
    for i in range(n):
        b = data[2 + 4 * i: 6 + 4 * i]
        spn = (b[0] & 0x07) | (b[2] << 3) | (b[3] << 11) | (((b[1] >> 7) & 1) << 19)
        d = {"spn": spn, "fmi": b[0] >> 3, "oc": b[1] & 0x7F}
        if (d["spn"], d["fmi"], d["oc"]) == (0x7FFFF, 31, 127):
            continue  # 未使用的 DTC 槽
        out["dtcs"].append(d)
    return out


LAMP_BITS = ((0, "MIL"), (2, "RSL"), (4, "AWL"), (6, "PL"))


def lamp_text(dm: dict) -> str:
    names = []
    for shift, name in LAMP_BITS:
        v = (dm["lamp"] >> shift) & 3
        if v == 1:
            names.append(name)
        elif v == 2:
            names.append(name + "?")
    return "+".join(names) if names else "off"


# ---- 周期信号换算 -------------------------------------------------------
def decode_eec1(data: bytes) -> dict:
    if len(data) < 8:
        return {}
    return {
        "driver_demand_pct": data[1] - 125,
        "actual_pct": data[2] - 125,
        "rpm": ((data[4] << 8) | data[3]) * 0.125,
        "starter_mode": data[6],
    }


def decode_et1(data: bytes) -> dict:
    if len(data) < 8:
        return {}
    return {
        "coolant_c": data[0] - 40,
        "fuel_c": data[1] - 40,
        "oil_c": data[2] - 40,
        "oil_kpa": data[3] * 4,
        "coolant_kpa": data[5] * 2,
    }


def decode_ccvs1(data: bytes) -> dict:
    if len(data) < 8:
        return {}
    return {
        "speed_kmh": ((data[1] << 8) | data[0]) / 256.0,
        "brake": data[3],
        "clutch": data[2],
    }


SIGNAL_DECODERS = {PGN_EEC1: decode_eec1, PGN_ET1: decode_et1, PGN_CCVS1: decode_ccvs1}


def decode_signal(pgn: int, data: bytes) -> dict:
    fn = SIGNAL_DECODERS.get(pgn)
    return fn(data) if fn else {}


# ---- TP ----------------------------------------------------------------
@dataclass
class TpSession:
    pgn: int
    src: int
    total: int
    npackets: int
    bam: bool
    buf: bytearray = field(default_factory=bytearray)
    next_seq: int = 1
    started: float = 0.0
    gaps: list = field(default_factory=list)


class TpReassembler:
    """接收侧：BAM 与 RTS/CTS 重组。CM 报文返回 0 个完整包；DT 凑齐返回消息。"""

    def __init__(self) -> None:
        self.sessions: dict[tuple[int, int], TpSession] = {}
        self.stats = {"bam": 0, "rts": 0, "done": 0, "bytes": 0, "errors": 0,
                      "gap_min": None, "gap_max": None, "gap_sum": 0.0, "gap_n": 0}

    def feed(self, can_id: int, data: bytes, ts: float = 0.0):
        pgn = pgn_from_id(can_id)
        sa = sa_of(can_id)
        out = None
        if pgn == PGN_TP_CM:
            out = self._cm(can_id, data, ts, sa)
        elif pgn == PGN_TP_DT:
            out = self._dt(data, ts, sa)
        return out

    def _cm(self, can_id, data, ts, sa):
        if len(data) < 8:
            return None
        ctrl = data[0]
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        tp_pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        key = (tp_pgn, sa)
        if ctrl == TP_CM_BAM:
            self.sessions[key] = TpSession(tp_pgn, sa, total, npkts, True, started=ts)
            self.stats["bam"] += 1
        elif ctrl == TP_CM_RTS:
            self.sessions[key] = TpSession(tp_pgn, sa, total, npkts, False, started=ts)
            self.stats["rts"] += 1
        elif ctrl == TP_CM_ABORT:
            self.sessions.pop(key, None)
            self.stats["errors"] += 1
        return None

    def _dt(self, data, ts, sa):
        if len(data) < 2:
            return None
        for key, sess in list(self.sessions.items()):
            if sess.src != sa:
                continue
            if data[0] != sess.next_seq:
                self.stats["errors"] += 1
                del self.sessions[key]
                return None
            if sess.next_seq > 1:
                gap = ts - sess.started
                if gap > 0:
                    sess.gaps.append(gap)
                    self.stats["gap_n"] += 1
                    self.stats["gap_sum"] += gap
                    if self.stats["gap_min"] is None or gap < self.stats["gap_min"]:
                        self.stats["gap_min"] = gap
                    if self.stats["gap_max"] is None or gap > self.stats["gap_max"]:
                        self.stats["gap_max"] = gap
            sess.buf.extend(data[1:1 + 7])
            sess.next_seq += 1
            sess.started = ts
            if len(sess.buf) >= sess.total:
                payload = bytes(sess.buf[:sess.total])
                self.stats["done"] += 1
                self.stats["bytes"] += sess.total
                del self.sessions[key]
                return (sess.pgn, sess.src, 0xFF if sess.bam else 0, payload)
        return None


def build_bam_frames(pgn: int, data: bytes, sa: int, priority: int | None = None):
    """生成 BAM 帧序列：[(can_id, bytes), ...]（第 1 帧为 TP.CM）。"""
    assert 0 < len(data) <= TP_MAX_LEN
    prio = PGN_PRIORITY.get(pgn, 6) if priority is None else priority
    npkts = (len(data) + 6) // 7
    cm = bytes([TP_CM_BAM, len(data) & 0xFF, (len(data) >> 8) & 0xFF, npkts, 0xFF,
                pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF])
    frames = [(id_from_pgn(PGN_TP_CM, 7, 0xFF, sa), cm)]
    for seq in range(1, npkts + 1):
        off = (seq - 1) * 7
        chunk = data[off:off + 7]
        frames.append((id_from_pgn(PGN_TP_DT, 7, 0xFF, sa), bytes([seq]) + chunk))
    return frames