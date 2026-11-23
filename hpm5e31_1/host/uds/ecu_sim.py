#!/usr/bin/env python3
"""ecu_sim.py - 主机侧 UDS ECU 模拟器（vcan0），用于在没有板卡时验证上位机工具

用途：
  1) 上位机工具（uds_tester.py / mini_flasher.py / obd_scan.py）先对模拟器跑通，
     再接到真实 ECU（Bench B 的 vcan0 或直连 CAN 卡），排除"工具本身有问题"；
  2) 书里作为"对等实现"对照：C 端 app_uds.c 与本文件行为一致（同样的 DID 表、
     同样的 27 算法、同样的 34/36/37 流程），可以用来交叉验证。

注意：这是**模拟器**，不是实测对象；第 31 章里它的输出会明确标注"模拟器"。

用法：
  python3 ecu_sim.py --channel vcan0                 # UDS ECU（0x7E0/0x7E8）
  python3 ecu_sim.py --channel vcan0 --mode echo     # ISO-TP 回显（0x700/0x708）
"""
import argparse
import struct
import sys
import time

import common


class UdsSim:
    def __init__(self):
        self.session = 0x01
        self.security = 0
        self.seed_pending = 0
        self.seed = 0
        self.attempts = 0
        self.lock_until = 0.0
        self.p2_ms = 50
        self.p2star_ms = 5000
        self.dids = {
            0xF190: bytearray(b"HPM5E31TESTVIN001"),
            0xF195: bytearray(b"APP-V1.02.03"),
            0xF187: bytearray(b"5E31-1A0001"),
            0xF18C: bytearray(b"SN20261030001"),
            0xF191: bytearray(b"HPM5E31_1-REV.A"),
            0xF010: bytearray(b"\x00"),
        }
        self.dtc = {
            0xC10087: [0x0F, 3, 0],
            0xC10187: [0x08, 1, 0],
            0xC0DE01: [0x04, 1, 0],
        }
        self.flash = bytearray(32768)
        self.dl = None
        self.routines = {0x0203: "idle", 0x0301: "idle", 0xFF00: "idle", 0xFF01: "idle"}
        self.seed_lfsr = 0x1D2C3B4A

    # ---- 工具 ----
    def next_seed(self):
        x = self.seed_lfsr
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= (x >> 17)
        x ^= (x << 5) & 0xFFFFFFFF
        x &= 0xFFFFFFFF
        if x == 0:
            x = 0xA5A5A5A5
        self.seed_lfsr = x
        self.seed = x
        return x

    @staticmethod
    def nrc(sid, code):
        return bytes([0x7F, sid, code])

    # ---- 服务 ----
    def handle(self, req):
        sid = req[0]
        now = time.time()
        if sid == 0x10:
            if len(req) < 2:
                return self.nrc(sid, 0x13)
            sub = req[1] & 0x7F
            if sub not in (1, 2, 3):
                return self.nrc(sid, 0x12)
            self.session = sub
            self.security = 0
            self.seed_pending = 0
            p2star = self.p2star_ms // 10
            return bytes([0x50, sub, self.p2_ms >> 8, self.p2_ms & 0xFF,
                          p2star >> 8, p2star & 0xFF])
        if sid == 0x11:
            return bytes([0x51, req[1] & 0x7F])
        if sid == 0x14:
            if len(req) != 4 or req[1:4] != b"\xff\xff\xff":
                return self.nrc(sid, 0x31)
            self.dtc.clear()
            return b"\x54"
        if sid == 0x19:
            return self.read_dtc(req)
        if sid == 0x22:
            out = bytearray([0x62])
            i = 1
            while i + 1 < len(req):
                did = (req[i] << 8) | req[i + 1]
                if did not in self.dids:
                    return self.nrc(sid, 0x31)
                out += bytes([req[i], req[i + 1]]) + self.dids[did]
                i += 2
            return bytes(out)
        if sid == 0x27:
            return self.security_access(req, now)
        if sid == 0x28:
            if self.session == 1:
                return self.nrc(sid, 0x7F)
            return bytes([0x68, req[1] & 0x7F])
        if sid == 0x2E:
            did = (req[1] << 8) | req[2]
            if did not in self.dids:
                return self.nrc(sid, 0x31)
            if did != 0xF190:
                return self.nrc(sid, 0x7F)
            if self.session != 3:
                return self.nrc(sid, 0x7F)
            if self.security < 1:
                return self.nrc(sid, 0x33)
            if len(req) - 3 != len(self.dids[did]):
                return self.nrc(sid, 0x13)
            self.dids[did] = bytearray(req[3:])
            return bytes([0x6E, req[1], req[2]])
        if sid == 0x2F:
            if self.session == 1:
                return self.nrc(sid, 0x7F)
            sub = req[1] & 0x7F
            did = (req[2] << 8) | req[3]
            if did not in self.dids:
                return self.nrc(sid, 0x31)
            if sub == 3 and len(req) >= 5:
                self.dids[did][0] = req[4]
            return bytes([0x6F, sub, req[2], req[3], self.dids[did][0]])
        if sid == 0x31:
            return self.routine_control(req)
        if sid == 0x34:
            return self.request_download(req)
        if sid == 0x36:
            return self.transfer_data(req)
        if sid == 0x37:
            if self.dl is None:
                return self.nrc(sid, 0x24)
            self.dl = None
            return b"\x77"
        if sid == 0x3E:
            return bytes([0x7E, 0x00])
        if sid == 0x85:
            if self.session == 1:
                return self.nrc(sid, 0x7F)
            return bytes([0xC5, req[1] & 0x7F])
        if sid == 0x01:
            return self.obd_mode01(req)
        if sid == 0x09:
            # 09 02 VIN：49 02 01 + 17 字节 ASCII
            return bytes([0x49, 0x02, 0x01]) + bytes(self.dids[0xF190])
        return self.nrc(sid, 0x11)

    def obd_mode01(self, req):
        """OBD 服务 0x01：本模拟器给的是演示值（无真实传感器）。"""
        pid = req[1] if len(req) > 1 else 0
        table = {
            0x00: bytes([0x41, 0x00, 0x18, 0x18, 0x00, 0x01]),   # 支持 05/0C/0D/1C
            0x05: bytes([0x41, 0x05, 90]),                       # 冷却液 50 ℃
            0x0C: bytes([0x41, 0x0C, 0x1A, 0xF8]),               # 1726 rpm
            0x0D: bytes([0x41, 0x0D, 0x2D]),                     # 45 km/h
            0x1C: bytes([0x41, 0x1C, 0x01]),                     # OBD-II (California)
            0x20: bytes([0x41, 0x20, 0x00, 0x00, 0x00, 0x00]),
        }
        return table.get(pid, self.nrc(0x01, 0x12))

    def read_dtc(self, req):
        sub = req[1]
        if sub == 0x01:
            mask = req[2]
            cnt = sum(1 for v in self.dtc.values() if v[0] & mask)
            return bytes([0x59, 0x01, 0x09, 0x01, cnt >> 8, cnt & 0xFF])
        if sub == 0x02:
            mask = req[2]
            out = bytearray([0x59, 0x02, 0x09])
            for dtc, v in sorted(self.dtc.items()):
                if v[0] & mask:
                    out += bytes([(dtc >> 16) & 0xFF, (dtc >> 8) & 0xFF, dtc & 0xFF, v[0]])
            return bytes(out)
        if sub == 0x04:
            code = (req[2] << 16) | (req[3] << 8) | req[4]
            out = bytearray([0x59, 0x04, req[2], req[3], req[4], 0x09])
            if code in self.dtc:
                v = self.dtc[code]
                out += bytes([req[2], req[3], req[4], v[0], 0x01, 3,
                              0x01, 0x00, 0x01, 0x00, 0x2F, 0x12, 0x01, 0x02, 0x01, 82])
            return bytes(out)
        if sub == 0x06:
            code = (req[2] << 16) | (req[3] << 8) | req[4]
            out = bytearray([0x59, 0x06, req[2], req[3], req[4], 0x09])
            if code in self.dtc:
                v = self.dtc[code]
                out += bytes([req[2], req[3], req[4], v[0], 0x02, v[1], v[2]])
            return bytes(out)
        return self.nrc(0x19, 0x12)

    def security_access(self, req, now):
        sub = req[1] & 0x7F
        if sub not in (1, 2, 3, 4):
            return self.nrc(0x27, 0x12)
        level = (sub + 1) // 2
        if level == 2 and self.session == 1:
            return self.nrc(0x27, 0x7E)
        if now < self.lock_until:
            return self.nrc(0x27, 0x37)
        if sub & 1:
            if self.security >= level:
                return bytes([0x67, sub, 0, 0, 0, 0])
            seed = self.next_seed()
            self.seed_pending = level
            return bytes([0x67, sub]) + struct.pack(">I", seed)
        if len(req) != 6:
            return self.nrc(0x27, 0x13)
        if self.seed_pending != level:
            return self.nrc(0x27, 0x24)
        key = struct.unpack(">I", req[2:6])[0]
        if key == common.security_key(self.seed, level):
            self.security = level
            self.seed_pending = 0
            self.attempts = 0
            return bytes([0x67, sub])
        self.seed_pending = 0
        self.attempts += 1
        if self.attempts >= 3:
            self.lock_until = now + 10.0
            self.attempts = 0
            return self.nrc(0x27, 0x36)
        return self.nrc(0x27, 0x35)

    def routine_control(self, req):
        sub = req[1] & 0x7F
        rid = (req[2] << 8) | req[3]
        if rid not in self.routines:
            return self.nrc(0x31, 0x31)
        if rid in (0xFF00, 0xFF01):
            if self.session != 2:
                return self.nrc(0x31, 0x7E)
            if self.security < 1:
                return self.nrc(0x31, 0x33)
        if rid == 0x0301 and sub == 1:
            return "PENDING"      # 由上层发 0x78 后延时
        if rid == 0xFF00:
            crc = common.crc32(self.flash[256:])   # 镜像头占 256 B（与固件 uds_flash 一致）
            return bytes([0x71, sub, req[2], req[3], 0x00]) + struct.pack(">I", crc)
        if rid == 0xFF01:
            ok = self.flash[0:8] == b"HPMIMG01" and struct.unpack("<I", self.flash[8:12])[0] == len(self.flash) \
                 and struct.unpack("<I", self.flash[12:16])[0] == common.crc32(self.flash[256:])
            return bytes([0x71, sub, req[2], req[3], 0x00 if ok else 0x04])
        self.routines[rid] = "running" if sub == 1 else self.routines[rid]
        return bytes([0x71, sub, req[2], req[3], 0x00, 0x01])

    def request_download(self, req):
        if self.session != 2:
            return self.nrc(0x34, 0x7F)
        if self.security < 1:
            return self.nrc(0x34, 0x33)
        dfi, alfi = req[1], req[2]
        alen, slen = alfi & 0x0F, (alfi >> 4) & 0x0F
        if dfi != 0 or len(req) != 3 + alen + slen:
            return self.nrc(0x34, 0x13)
        addr = int.from_bytes(req[3:3 + alen], "big")
        size = int.from_bytes(req[3 + alen:3 + alen + slen], "big")
        if addr < 0x800F0000 or (addr + size) > 0x80100000 or size == 0:
            return self.nrc(0x34, 0x31)
        self.flash = bytearray(size)
        self.dl = {"addr": addr, "size": size, "got": 0, "seq": 1, "t0": time.time()}
        return bytes([0x74, 0x20, 0x01, 0x02])      # maxNumberOfBlockLength = 258

    def transfer_data(self, req):
        if self.dl is None:
            return self.nrc(0x36, 0x24)
        seq = req[1]
        if seq != self.dl["seq"]:
            return self.nrc(0x36, 0x73)
        data = req[2:]
        if self.dl["got"] + len(data) > self.dl["size"]:
            return self.nrc(0x36, 0x13)
        self.flash[self.dl["got"]:self.dl["got"] + len(data)] = data
        self.dl["got"] += len(data)
        self.dl["seq"] = (seq + 1) & 0xFF or 1
        return bytes([0x76, seq])


def run_echo(args):
    bus = common.RawCan(args.channel)
    tp = common.IsoTpServer(bus, rx_id=0x700, tx_id=0x708, pad=0xCC, verbose=args.verbose)
    print("[sim] ISO-TP echo on 0x700 -> 0x708 (%s), Ctrl-C 退出" % args.channel)
    while True:
        try:
            req = tp.recv(timeout=0.5)
        except common.IsoTpError:
            continue
        if req:
            print("[sim] echo %d B" % len(req))
            tp.send(req)


def run_uds(args):
    bus = common.RawCan(args.channel)
    tp = common.IsoTpServer(bus, rx_id=0x7E0, tx_id=0x7E8, pad=0xCC, verbose=args.verbose)
    sim = UdsSim()
    print("[sim] UDS ECU on 0x7E0/0x7E8 (%s)  P2=%d ms P2*=%d ms" % (args.channel, sim.p2_ms, sim.p2star_ms))
    while True:
        try:
            req = tp.recv(timeout=0.5)
        except common.IsoTpError:
            continue
        if not req:
            continue
        resp = sim.handle(req)
        if resp == "PENDING":
            tp.send(bytes([0x7F, req[0], 0x78]))
            time.sleep(0.15)
            resp = bytes([0x71, 0x01, 0x03, 0x01, 0x00, 0x01])
        if resp:
            tp.send(resp)
            if args.verbose:
                print("[sim] %s -> %s" % (req.hex(" ").upper(), resp.hex(" ").upper()))


def main():
    ap = argparse.ArgumentParser(description="主机侧 UDS/ISO-TP ECU 模拟器（vcan0）")
    ap.add_argument("--channel", default="vcan0", help="SocketCAN 接口名（默认 vcan0）")
    ap.add_argument("--mode", default="uds", choices=["uds", "echo"], help="uds=UDS ECU，echo=ISO-TP 回显")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    try:
        if args.mode == "echo":
            run_echo(args)
        else:
            run_uds(args)
    except KeyboardInterrupt:
        print("\n[sim] bye")


if __name__ == "__main__":
    main()
