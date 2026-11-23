#!/usr/bin/env python3
"""xcp_eth_client.py —— 自写 XCP on Ethernet(UDP) 客户端（标准库 socket，无 pyxcp 依赖）

为什么自写：
  - pyxcp 0.30.1 的 CAN 传输层在本仓库环境不可用（第 35 章 U-35-2），以太网路径
    同样不想再赌它的 C++ 扩展；XCPonEth 的传输层头只有 4 字节，自己实现约 200 行，
    可控、可加统计（CTR 间隙、RTT、单包多 ODT 计数）。
  - 台架 UDP 链路实测有 ~5% 单包丢弃（见第 35 章表 35-13），所以对幂等命令做了
    超时重发；UPLOAD/DOWNLOAD/WRITE_DAQ 这些"会推进 MTA/条目指针"的命令不盲重发，
    由调用方重设 MTA/指针后再试（本文件 get_id 与 xcp_daq.py 的 _eth_write_mem 就是
    这么做的）。

线上格式（ASAM XCP 1.5 XCPonEth，与 vectorgrp/XCPlite 的 xcptl_cfg.h 一致）：
    UDP 载荷 = 一条或多条消息首尾相接，每条：
        LEN(2 B, LE) | CTR(2 B, LE) | XCP 协议层报文(LEN B) | [0~3 B 填充]
    - LEN 不含 4 B 头；非最后一条消息按 4 B 对齐（LEN 含填充）；
    - CTR 每发一条消息 +1（回绕），接收侧用它算丢包（UDP 没有重传/排序保证）。

用法（由 host/xcp/xcp_daq.py --interface eth 调用）：
    c = XcpEthClient("192.168.2.100", 5555)
    info = c.connect(); ...
"""
from __future__ import annotations

import socket
import struct
import time

PID_RES = 0xFF
PID_ERR = 0xFE
PID_EV = 0xFD
PID_SERV = 0xFC

HDR = struct.Struct("<HH")

# 允许超时重发的命令：重复执行无副作用（读类/幂等配置）。
# 不包含：UPLOAD(0xF5) 推进 MTA、DOWNLOAD(0xF0) 写后推进 MTA、
#         WRITE_DAQ(0xE1) 自动移动条目指针 —— 由调用方"重设指针再发"。
RETRY_SAFE = {
    0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF4, 0xEC,
    0xDC, 0xDA, 0xD9, 0xD8, 0xD7, 0xD6, 0xD5, 0xD4, 0xD3, 0xE2, 0xE0, 0xDE,
    0xDD, 0xE9, 0xE8, 0xE7, 0xEA, 0xDF, 0xDB,
}


class XcpError(Exception):
    def __init__(self, code):
        super().__init__("XCP ERR 0x%02X" % code)
        self.code = code


class XcpEthClient:
    def __init__(self, host="192.168.2.100", port=5555, timeout=0.5, local_port=0):
        self.addr = (host, int(port))
        self.timeout = timeout
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", int(local_port)))
        self.sock.settimeout(timeout)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        except OSError:
            pass
        self.ctr_tx = 0
        self.ctr_rx_last = None
        self.max_cto = 8
        self.max_dto = 8
        self.resource = 0
        self.last_error = 0
        self.stats = {
            "datagrams_rx": 0,
            "msgs_rx": 0,
            "bytes_rx": 0,
            "ctr_gap": 0,
            "ctr_dup": 0,
            "framing_err": 0,
            "ev": 0,
            "timeouts": 0,
        }

    # ---------------- 底层 ----------------

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def reset_stats(self):
        for k in self.stats:
            self.stats[k] = 0

    def _send_cto(self, payload):
        pkt = HDR.pack(len(payload), self.ctr_tx) + bytes(payload)
        self.ctr_tx = (self.ctr_tx + 1) & 0xFFFF
        self.sock.sendto(pkt, self.addr)

    def _parse(self, data):
        """把一个 UDP 数据报拆成多条 XCP 消息；返回 payload 列表"""
        out = []
        off = 0
        n = len(data)
        self.stats["datagrams_rx"] += 1
        self.stats["bytes_rx"] += n
        while off + 4 <= n:
            ln, ctr = HDR.unpack_from(data, off)
            if (ln == 0) or (off + 4 + ln > n):
                self.stats["framing_err"] += 1
                break
            payload = data[off + 4: off + 4 + ln]
            if self.ctr_rx_last is not None:
                expect = (self.ctr_rx_last + 1) & 0xFFFF
                if ctr != expect:
                    diff = (ctr - expect) & 0xFFFF
                    if diff < 0x8000:
                        self.stats["ctr_gap"] += diff
                    else:
                        self.stats["ctr_dup"] += 1
            self.ctr_rx_last = ctr
            self.stats["msgs_rx"] += 1
            out.append(payload)
            off += 4 + ln
        return out

    def recv_messages(self, timeout=None):
        self.sock.settimeout(self.timeout if timeout is None else timeout)
        try:
            data, _ = self.sock.recvfrom(65535)
        except socket.timeout:
            return []
        return self._parse(data)

    def _request_once(self, payload, timeout=None):
        self._send_cto(payload)
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            left = deadline - time.monotonic()
            for p in self.recv_messages(max(0.001, left)):
                if not p:
                    continue
                if p[0] == PID_RES:
                    return p[1:]
                if p[0] == PID_ERR:
                    self.last_error = p[1]
                    raise XcpError(p[1])
                if p[0] == PID_EV:
                    self.stats["ev"] += 1
                # DAQ（0x00..0xFB）在控制阶段直接丢
        raise TimeoutError("XCP 超时：命令 0x%02X 无响应（%s）"
                           % (payload[0], self.addr[0] + ":" + str(self.addr[1])))

    def request(self, payload, timeout=None, retries=None):
        """发一条 CTO 并等 RES/ERR；幂等命令在 UDP 丢包时自动重发（默认 2 次）"""
        if retries is None:
            retries = 2 if payload[0] in RETRY_SAFE else 0
        last = None
        for _attempt in range(retries + 1):
            try:
                return self._request_once(payload, timeout)
            except TimeoutError as exc:
                last = exc
                self.stats["timeouts"] += 1
                # 丢掉可能迟到的旧响应，避免下一条请求错位
                self.sock.settimeout(0.0)
                try:
                    while True:
                        self.sock.recvfrom(65535)
                except (BlockingIOError, socket.timeout, OSError):
                    pass
        raise last

    # ---------------- 标准命令 ----------------

    def connect(self, mode=0, retries=2):
        # CONNECT 不能无脑重发：从站已连接时按规范会静默忽略重复 CONNECT。
        # 超时后先用 GET_STATUS 探活：能回 GET_STATUS 说明第一包其实到了、只是 RES
        # 丢了，按板端已知配置继续（打印告警，不假装没发生）。
        last = None
        for _attempt in range(retries + 1):
            try:
                r = self._request_once([0xFF, mode])
                self.resource = r[0]
                comm = r[1]
                self.max_cto = r[2] or 8
                self.max_dto = r[3] | (r[4] << 8)
                return {
                    "resource": self.resource, "comm_mode_basic": comm,
                    "max_cto": self.max_cto, "max_dto": self.max_dto,
                    "protocol": r[5], "transport": r[6],
                }
            except TimeoutError as exc:
                last = exc
                self.stats["timeouts"] += 1
                try:
                    self._request_once([0xFD], timeout=0.3)
                except (TimeoutError, XcpError):
                    continue
                print("  [warn] CONNECT RES 丢失，但 GET_STATUS 正常：按板端配置继续"
                      "（MAX_CTO=255 / MAX_DTO=256，XCPonEth 固定配置）")
                self.resource = 0x05
                self.max_cto = 255
                self.max_dto = 256
                return {"resource": 0x05, "comm_mode_basic": 0x80,
                        "max_cto": 255, "max_dto": 256,
                        "protocol": 0x14, "transport": 0x01}
        raise last

    def disconnect(self):
        return self.request([0xFE])

    def get_status(self):
        r = self.request([0xFD])
        return {"session_status": r[0] | (r[1] << 8),
                "protection": r[2], "state": r[3]}

    def get_id(self, mode=0):
        # UPLOAD 会推进 MTA，不能单独重发；超时后重新 GET_ID（会重设 MTA）再取
        chunk = max(1, min(200, self.max_cto - 1))
        for attempt in range(3):
            r = self.request([0xFA, mode])
            length = int.from_bytes(r[3:7], "little")
            name = b""
            try:
                while len(name) < length:
                    n = min(chunk, length - len(name))
                    name += self._resp_payload(self.request([0xF5, n], retries=0))
                return name.decode("ascii", "replace")
            except TimeoutError:
                if attempt == 2:
                    raise
        return ""

    def set_mta(self, addr, ext=0):
        return self.request([0xF6, 0, ext] + list(addr.to_bytes(4, "little")))

    def upload(self, n):
        return self._resp_payload(self.request([0xF5, n]))

    def short_upload(self, n, addr, ext=0):
        return self._resp_payload(self.request([0xF4, 0, n, ext] +
                                               list(addr.to_bytes(4, "little"))))

    def download(self, data):
        return self.request([0xF0, len(data)] + list(data))

    def modify_bits(self, shift, and_mask, xor_mask):
        return self.request([0xEC, shift,
                             and_mask & 0xFF, (and_mask >> 8) & 0xFF,
                             xor_mask & 0xFF, (xor_mask >> 8) & 0xFF])

    def get_seed(self, mode, resource):
        r = self.request([0xF8, mode, resource])
        return r[1:1 + r[0]]

    def unlock(self, key):
        return self.request([0xF7, len(key)] + list(key))

    def get_daq_clock(self):
        r = self.request([0xDC])
        return int.from_bytes(r[:4], "little")

    # ---------------- DAQ ----------------

    def free_daq(self):
        return self.request([0xD6])

    def alloc_daq(self, count):
        return self.request([0xD5, 0, count & 0xFF, (count >> 8) & 0xFF])

    def alloc_odt(self, list_no, odt_count):
        return self.request([0xD4, 0, list_no & 0xFF, (list_no >> 8) & 0xFF, odt_count])

    def alloc_odt_entry(self, list_no, odt, entries):
        return self.request([0xD3, 0, list_no & 0xFF, (list_no >> 8) & 0xFF, odt, entries])

    def set_daq_ptr(self, list_no, odt, entry):
        return self.request([0xE2, 0, list_no & 0xFF, (list_no >> 8) & 0xFF, odt, entry])

    def write_daq(self, size, addr, bit_off=0xFF, ext=0):
        # 不自动重发：失败由调用方重设 DAQ_PTR 后重试（见 xcp_daq.py _eth_setup_daq）
        return self.request([0xE1, bit_off, size, ext] + list(addr.to_bytes(4, "little")),
                            retries=0)

    def set_daq_list_mode(self, mode, list_no, event, prescaler=1, priority=0):
        return self.request([0xE0, mode, list_no & 0xFF, (list_no >> 8) & 0xFF,
                             event & 0xFF, (event >> 8) & 0xFF, prescaler, priority])

    def start_stop_daq_list(self, mode, list_no):
        r = self.request([0xDE, mode, list_no & 0xFF, (list_no >> 8) & 0xFF])
        return r[0] if r else 0xFF

    def start_stop_synch(self, mode):
        return self.request([0xDD, mode])

    # ---------------- 小工具 ----------------

    @staticmethod
    def _resp_payload(r):
        return bytes(r) if isinstance(r, (bytes, bytearray)) else bytes(r)
