#!/usr/bin/env python3
"""dm1_decode.py - J1939-73 DM1/DM2 解码器：灯状态 / SPN / FMI / OC 表 + 时间线

两种输入：
  * 实时：python3 dm1_decode.py -i vcan0 --duration 10
  * 离线：python3 dm1_decode.py --file logs/20261030-172059-j1939_dm.log
          （支持 candump 默认格式与 candump -L 的 "(ts) if id#data" 格式）

输出：
  1) 每条 DM1/DM2 的灯状态（MIL/RSL/AWL/PL）与 DTC 表；
  2) 灯状态变化时间线（只在变化时打印，相对时间）；
  3) 汇总：DM1 帧数、DTC 出现次数。
"""
from __future__ import annotations

import argparse
import re
import sys
import time

import j1939_core as j

FMI_TEXT = {
    0: "数据有效但高于正常范围(最严重)", 1: "数据有效但低于正常范围(最严重)",
    2: "数据不稳定", 3: "电压高于正常", 4: "电压低于正常", 5: "电流低于正常",
    6: "电流高于正常", 7: "机械系统无响应", 8: "频率异常", 9: "更新速率异常",
    10: "变化率异常", 11: "根本原因未知", 12: "设备/部件不良", 13: "超出校准",
    14: "特殊指令", 15: "数据有效但高于正常(最轻)", 16: "数据有效但高于正常(中等)",
    17: "数据有效但低于正常(最轻)", 18: "数据有效但低于正常(中等)",
    19: "接收网络数据错误", 31: "条件存在",
}

NULL_DTC = (0x7FFFF, 31, 127)


def decode_line(line: str):
    """解析一行 candump / candump -L 日志，返回 (ts, can_id, data) 或 None。"""
    m = re.match(r"^\((?P<ts>[\d.]+)\)\s+\S+\s+(?P<id>[0-9A-Fa-f]{8})#(?P<data>[0-9A-Fa-f]*)$",
                 line.strip())
    if not m:
        m = re.match(r"^\s*\S+\s+(?P<id>[0-9A-Fa-f]{8})\s+\[\d+\]\s+(?P<data>[0-9A-Fa-f ]+)$",
                     line.strip())
    if not m:
        return None
    d = m.groupdict()
    can_id = int(d["id"], 16)
    data = bytes.fromhex(d["data"].replace(" ", ""))
    return float(d.get("ts") or 0.0), can_id, data


def fmt_dtc(d):
    return (f"SPN={d['spn']:<7d} FMI={d['fmi']:<2d} OC={d['oc']:<3d}"
            f" | {FMI_TEXT.get(d['fmi'], 'FMI 保留/厂商自定义')}")


class Decoder:
    def __init__(self):
        self.t0 = None
        self.n = 0
        self.hits: dict[tuple[int, int], int] = {}
        self.last_lamp = None
        self.dm2_seen = 0

    def feed(self, ts, can_id, data):
        if self.t0 is None:
            self.t0 = ts
        pgn = j.pgn_from_id(can_id)
        if pgn not in (j.PGN_DM1, j.PGN_DM2):
            return
        if len(data) < 2:
            return
        dm = j.decode_dm(data)
        # 过滤全 0xFF 的未使用 DTC 槽
        dm["dtcs"] = [d for d in dm["dtcs"] if (d["spn"], d["fmi"], d["oc"]) != NULL_DTC]
        self.n += 1
        if pgn == j.PGN_DM2:
            self.dm2_seen += 1
        tag = "DM1" if pgn == j.PGN_DM1 else "DM2"
        t = ts - self.t0
        print(f"\n[{tag}] PGN=0x{pgn:05X} SA=0x{j.sa_of(can_id):02X} "
              f"lamp=0x{dm['lamp']:02X} [{j.lamp_text(dm)}] flash={dm['flash']} t={t:.3f}s")
        if not dm["dtcs"]:
            print("   (无 DTC：仅灯状态)")
        for d in dm["dtcs"]:
            print("   " + fmt_dtc(d))
            self.hits[(d["spn"], d["fmi"])] = self.hits.get((d["spn"], d["fmi"]), 0) + 1
        key = (dm["lamp"], tuple((d["spn"], d["fmi"]) for d in dm["dtcs"]))
        if key != self.last_lamp:
            self.last_lamp = key
            print(f"   >> lamp change @ t={t:.3f}s: 0x{dm['lamp']:02X} [{j.lamp_text(dm)}]")

    def summary(self):
        print("\n---- 汇总 ----")
        print(f"DM1+DM2 frames={self.n} (DM2={self.dm2_seen}) distinct DTC={len(self.hits)}")
        for (spn, fmi), n in sorted(self.hits.items()):
            print(f"  SPN={spn:<7d} FMI={fmi:<2d} 出现 {n} 次")
        return self.n > 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="DM1/DM2 解码（灯状态 + SPN/FMI/OC）")
    ap.add_argument("-i", "--interface", default="", help="实时 CAN 接口，如 vcan0")
    ap.add_argument("-f", "--file", default="", help="candump 日志文件")
    ap.add_argument("-d", "--duration", type=float, default=8.0, help="实时监听秒数")
    args = ap.parse_args(argv)

    dec = Decoder()
    if args.file:
        print(f"[dm1_decode] file={args.file}")
        with open(args.file, "r", encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                rec = decode_line(line)
                if rec:
                    dec.feed(*rec)
    elif args.interface:
        import can
        print(f"[dm1_decode] if={args.interface} duration={args.duration}s")
        try:
            bus = can.Bus(interface="socketcan", channel=args.interface)
        except Exception as exc:  # noqa: BLE001
            print(f"[FAIL] 打开 {args.interface} 失败: {exc}")
            return 2
        end = time.time() + args.duration
        try:
            while time.time() < end:
                msg = bus.recv(timeout=0.2)
                if msg is None or not msg.is_extended_id:
                    continue
                dec.feed(msg.timestamp or time.time(), msg.arbitration_id, bytes(msg.data))
        finally:
            bus.shutdown()
    else:
        ap.error("需要 -i <接口> 或 --file <日志>")

    ok = dec.summary()
    print("\nRESULT: PASS" if ok else "\nRESULT: FAIL (no DM1/DM2 frames)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
