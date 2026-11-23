#!/usr/bin/env python3
"""common.py - 不依赖第三方库的 ISO-TP(ISO 15765-2) + UDS(ISO 14229) 客户端/服务端骨架

只用 Python 标准库（socket AF_CAN / struct / select），因此在一台只装了
python3 的机器上也能跑；host/uds/isotp_test.py --impl raw、uds_tester.py --impl raw
以及 ecu_sim.py 都基于这个模块。

为什么要有裸实现：can-isotp / udsoncan 方便，但把"帧怎么拼、FC 怎么等"藏在库里；
书里要讲清 driver 行为，必须有一份能逐帧打印的对照实现（第 30 章 30-6 节）。
"""
from __future__ import annotations

import select
import socket
import struct
import time

# ----------------------------------------------------------------------------
# SocketCAN 裸封装
# ----------------------------------------------------------------------------

CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_SFF_MASK = 0x000007FF
CAN_EFF_MASK = 0x1FFFFFFF

CAN_RAW_FD_FRAMES = 5

# struct can_frame { can_id u32; can_dlc u8; __pad u8; __res0 u8; __res1 u8; data[8]; }
CAN_FRAME_FMT = "<IB3x8s"
CANFD_FRAME_FMT = "<IBB2x64s"


class RawCan:
    """最小的 SocketCAN 收发封装（支持经典帧与 CAN FD）。"""

    def __init__(self, channel="vcan0", fd=False):
        self.fd = fd
        self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        self.sock.bind((channel,))
        if fd:
            self.sock.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)

    def close(self):
        self.sock.close()

    def send(self, can_id, data):
        data = bytes(data)
        if self.fd:
            frame = struct.pack(CANFD_FRAME_FMT, can_id, len(data), 0, data.ljust(64, b"\x00"))
        else:
            frame = struct.pack(CAN_FRAME_FMT, can_id, len(data), data.ljust(8, b"\x00"))
        self.sock.send(frame)

    def recv(self, timeout=1.0):
        """返回 (can_id, data) 或 None。"""
        r, _, _ = select.select([self.sock], [], [], timeout)
        if not r:
            return None
        raw = self.sock.recv(72)
        if self.fd:
            cid, dlc, _flags, data = struct.unpack(CANFD_FRAME_FMT, raw)
            return cid, data[:dlc]
        cid, dlc, data = struct.unpack(CAN_FRAME_FMT, raw)
        return cid, data[:dlc]


# ----------------------------------------------------------------------------
# ISO-TP（客户端 = 发送方/接收方都用同一套状态机）
# ----------------------------------------------------------------------------

PCI_SF, PCI_FF, PCI_CF, PCI_FC = 0x0, 0x1, 0x2, 0x3
FS_CTS, FS_WAIT, FS_OVFLW = 0x0, 0x1, 0x2


class IsoTpError(Exception):
    pass


class IsoTp:
    """单通道 ISO-TP 客户端。

    pad: 经典帧固定补到 8 字节（默认 0xCC）；CAN FD 下补到合法 DLC。
    """

    def __init__(self, bus: RawCan, rx_id, tx_id, fd=False, bs=0, stmin=0,
                 pad=0xCC, n_bs=1.0, n_cr=1.0, verbose=False):
        self.bus = bus
        self.rx_id = rx_id
        self.tx_id = tx_id
        self.fd = fd
        self.bs = bs
        self.stmin = stmin
        self.pad = pad
        self.n_bs = n_bs
        self.n_cr = n_cr
        self.verbose = verbose
        self.stats = {"tx_msg": 0, "rx_msg": 0, "tx_frames": 0, "rx_frames": 0,
                      "fc_tx": 0, "fc_rx": 0, "segments": []}

    # ---- 帧构造 ----
    def _frame_len(self, n):
        if not self.fd:
            return 8 if self.pad is not None else n
        for dlc in (8, 12, 16, 20, 24, 32, 48, 64):
            if n <= dlc:
                return dlc
        return 64

    def _send_frame(self, payload):
        total = self._frame_len(len(payload))
        data = bytearray(payload)
        if total > len(data) and self.pad is not None:
            data += bytes([self.pad]) * (total - len(data))
        self.bus.send(self.tx_id, bytes(data))
        self.stats["segments"].append((bytes(data), time.perf_counter()))

    def _stmin_us(self, v):
        if v <= 0x7F:
            return v * 1000
        if 0xF1 <= v <= 0xF9:
            return (v - 0xF0) * 100
        return 127000

    # ---- 发送 ----
    def send(self, payload, timeout=None):
        """发送一条完整消息（SF/FF+CF，遵守对端 FC 的 BS/STmin）。"""
        timeout = timeout if timeout is not None else self.n_bs
        payload = bytes(payload)
        n = len(payload)
        if n == 0:
            raise IsoTpError("empty payload")
        if n > 4095 and not self.fd:
            raise IsoTpError("classic ISO-TP max 4095 bytes")

        # 单帧
        if n <= 7:
            self._send_frame(bytes([n]) + payload)
            self.stats["tx_msg"] += 1
            self.stats["tx_frames"] += 1
            return
        if self.fd and n <= 62:
            self._send_frame(bytes([0x00, n]) + payload)
            self.stats["tx_msg"] += 1
            self.stats["tx_frames"] += 1
            return

        # 首帧
        maxf = 64 if self.fd else 8
        if n <= 4095:
            hdr = bytes([0x10 | (n >> 8), n & 0xFF])
        else:
            hdr = bytes([0x10, 0x00]) + struct.pack(">I", n)
        avail = maxf - len(hdr)
        self._send_frame(hdr + payload[:avail])
        self.stats["tx_frames"] += 1
        off = avail
        sn = 1

        # 等 FC
        t0 = time.perf_counter()
        while True:
            fs, bs, stmin = self._wait_fc(timeout)
            break
        if fs == FS_OVFLW:
            raise IsoTpError("FC.OVFLW")
        if fs == FS_WAIT:
            raise IsoTpError("FC.WAIT (未在等待循环里处理)")
        stmin_us = self._stmin_us(stmin)
        block = bs if bs else 0xFFFF
        sent_in_block = 0
        while off < n:
            chunk = min(maxf - 1, n - off)
            self._send_frame(bytes([0x20 | sn]) + payload[off:off + chunk])
            self.stats["tx_frames"] += 1
            off += chunk
            sn = sn + 1 if sn < 15 else 1
            sent_in_block += 1
            if stmin_us:
                time.sleep(stmin_us / 1e6)
            if block != 0xFFFF and sent_in_block >= block and off < n:
                fs, bs, stmin = self._wait_fc(timeout)
                if fs != FS_CTS:
                    raise IsoTpError("FC fs=%d during block" % fs)
                stmin_us = self._stmin_us(stmin)
                block = bs if bs else 0xFFFF
                sent_in_block = 0
        self.stats["tx_msg"] += 1

    def _wait_fc(self, timeout):
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < timeout:
            got = self.bus.recv(min(0.05, max(0.001, timeout)))
            if got is None:
                continue
            cid, data = got
            if cid != self.rx_id or len(data) < 3:
                continue
            if (data[0] >> 4) != PCI_FC:
                continue
            self.stats["fc_rx"] += 1
            fs = data[0] & 0x0F
            if fs == FS_WAIT:
                t0 = time.perf_counter()      # WAIT 重置 N_Bs
                continue
            return fs, data[1], data[2]
        raise IsoTpError("N_Bs timeout waiting FC")

    # ---- 接收 ----
    def recv(self, timeout=None):
        timeout = timeout if timeout is not None else self.n_cr
        deadline = time.perf_counter() + timeout
        while True:
            remain = deadline - time.perf_counter()
            if remain <= 0:
                raise IsoTpError("N_Cr timeout waiting message")
            got = self.bus.recv(min(0.05, remain))
            if got is None:
                continue
            cid, data = got
            if cid != self.rx_id or len(data) < 1:
                continue
            self.stats["rx_frames"] += 1
            pci = data[0] >> 4
            if pci == PCI_SF:
                n = data[0] & 0x0F
                off = 1
                if n == 0 and self.fd:
                    n = data[1]
                    off = 2
                self.stats["rx_msg"] += 1
                return bytes(data[off:off + n])
            if pci == PCI_FF:
                n = ((data[0] & 0x0F) << 8) | data[1]
                off = 2
                if n == 0:
                    n = struct.unpack(">I", data[2:6])[0]
                    off = 6
                buf = bytearray(data[off:off + n])
                self._send_fc(FS_CTS)
                sn = 1
                cnt = 0
                while len(buf) < n:
                    got = self.bus.recv(min(0.05, max(0.001, deadline - time.perf_counter())))
                    if got is None:
                        if time.perf_counter() > deadline:
                            raise IsoTpError("N_Cr timeout in CF")
                        continue
                    cid, data = got
                    if cid != self.rx_id:
                        continue
                    if (data[0] >> 4) != PCI_CF:
                        continue
                    self.stats["rx_frames"] += 1
                    if (data[0] & 0x0F) != sn:
                        raise IsoTpError("SN mismatch: got %d want %d" % (data[0] & 0x0F, sn))
                    buf += data[1:]
                    sn = sn + 1 if sn < 15 else 1
                    cnt += 1
                    if self.bs and cnt >= self.bs and len(buf) < n:
                        cnt = 0
                        self._send_fc(FS_CTS)
                self.stats["rx_msg"] += 1
                return bytes(buf[:n])
            if pci == PCI_FC:
                continue                      # 发送方自己会处理
        return b""

    def _send_fc(self, fs):
        self.stats["fc_tx"] += 1
        self._send_frame(bytes([0x30 | fs, self.bs, self.stmin]))


class IsoTpServer(IsoTp):
    """服务端：把 recv() 复用到"请求/响应"模型上（DUT 侧用）。"""

    def serve(self, handler, stop=None):
        """阻塞循环：收到完整请求 -> handler(payload) -> 返回响应 bytes -> 发回。"""
        while stop is None or not stop():
            try:
                req = self.recv(timeout=0.5)
            except IsoTpError:
                continue
            if not req:
                continue
            resp = handler(req)
            if resp:
                self.send(resp)


# ----------------------------------------------------------------------------
# UDS 语义层
# ----------------------------------------------------------------------------

def crc32(data, crc=0xFFFFFFFF):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def security_key(seed, level):
    """与 MCU 端 app_uds_calc_key() 逐位一致（第 31 章 31-4 节）。"""
    k = seed & 0xFFFFFFFF
    for _ in range(8):
        k = ((k << 1) | (k >> 31)) & 0xFFFFFFFF
        k ^= 0x3A5C9E17
        k &= 0xFFFFFFFF
        if k & 1:
            k = (k + 0x5A5A5A5A) & 0xFFFFFFFF
    k ^= (level * 0x01020304) & 0xFFFFFFFF
    k = ((k << 7) | (k >> 25)) & 0xFFFFFFFF
    return k


NRC_NAMES = {
    0x10: "generalReject", 0x11: "serviceNotSupported", 0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLength", 0x22: "conditionsNotCorrect", 0x24: "requestSequenceError",
    0x31: "requestOutOfRange", 0x33: "securityAccessDenied", 0x35: "invalidKey",
    0x36: "exceedNumberOfAttempts", 0x37: "requiredTimeDelayNotExpired",
    0x72: "generalProgrammingFailure", 0x73: "wrongBlockSequenceCounter",
    0x78: "responsePending", 0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}

SID_NAMES = {
    0x10: "DiagnosticSessionControl", 0x11: "ECUReset", 0x14: "ClearDiagnosticInformation",
    0x19: "ReadDTCInformation", 0x22: "ReadDataByIdentifier", 0x27: "SecurityAccess",
    0x28: "CommunicationControl", 0x2E: "WriteDataByIdentifier",
    0x2F: "InputOutputControlByIdentifier", 0x31: "RoutineControl",
    0x34: "RequestDownload", 0x36: "TransferData", 0x37: "RequestTransferExit",
    0x3E: "TesterPresent", 0x85: "ControlDTCSetting",
}


class UdsClient:
    """UDS 客户端：自动处理 0x78 pending，返回 (resp, rtt_us, pending_count)。"""

    def __init__(self, tp: IsoTp, p2star=5.0):
        self.tp = tp
        self.p2star = p2star
        self.history = []

    def request(self, payload, timeout=None):
        payload = bytes(payload)
        timeout = timeout if timeout is not None else self.p2star
        t0 = time.perf_counter()
        self.tp.send(payload)
        pend = 0
        while True:
            resp = self.tp.recv(timeout)
            rtt = int((time.perf_counter() - t0) * 1e6)
            if len(resp) == 3 and resp[0] == 0x7F and resp[2] == 0x78:
                pend += 1
                t0 = time.perf_counter()      # P2* 续期
                continue
            self.history.append((payload, resp, rtt, pend))
            return resp, rtt, pend

    # 便捷封装
    def session(self, sub):
        return self.request([0x10, sub])

    def tester_present(self, suppress=False):
        return self.request([0x3E, 0x80 if suppress else 0x00])

    def read_did(self, did):
        return self.request([0x22, did >> 8, did & 0xFF])

    def write_did(self, did, data):
        return self.request(bytes([0x2E, did >> 8, did & 0xFF]) + bytes(data))

    def unlock(self, level=1):
        seed_resp, rtt, _ = self.request([0x27, 2 * level - 1])
        if seed_resp[0] != 0x67:
            return seed_resp, rtt
        seed = struct.unpack(">I", seed_resp[2:6])[0]
        key = security_key(seed, level)
        resp, rtt2, _ = self.request(bytes([0x27, 2 * level]) + struct.pack(">I", key))
        return resp, rtt + rtt2


def is_positive(resp, sid):
    return len(resp) >= 1 and resp[0] == ((sid + 0x40) & 0xFF)


def nrc_of(resp):
    if len(resp) == 3 and resp[0] == 0x7F:
        return resp[2]
    return None


def describe(resp):
    """把响应变成人可读的一行。"""
    if not resp:
        return "<empty>"
    if len(resp) == 3 and resp[0] == 0x7F:
        return "NRC 0x%02X %s" % (resp[2], NRC_NAMES.get(resp[2], "?"))
    return resp.hex(" ").upper()


def hexs(data):
    return bytes(data).hex(" ").upper()
