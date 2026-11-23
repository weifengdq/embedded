#!/usr/bin/env python3
"""xcp_client.py —— 自写最小 XCP on CAN 客户端（python-can，无 pyxcp 依赖）

为什么自写：pyxcp 0.30.1 的 CAN 传输层在监听线程里抛
  AttributeError: 'PythonCanWrapper' object has no attribute 'can_interface'
（自建 Bus 和传 user-supplied Bus 两种写法都复现，见第 34 章 U-34-2），
而 XCP on CAN 的 CTO 本身极简单（8 字节单帧），因此这里用 python-can 直接实现主机侧：
CTO/RES 同步请求-响应 + DAQ 帧异步接收。

帧格式（XCPonCAN）：
  CMD: CAN ID 0x555, data[0]=命令码, data[1..7]=参数, 不足补 0 到 DLC=8
  RES: CAN ID 0x556, data[0]=0xFF, data[1..]=数据
  ERR: CAN ID 0x556, data[0]=0xFE, data[1]=错误码
  DAQ: CAN ID 0x556, data[0]=PID(0x00..0xFB)=绝对 ODT 号
"""
from __future__ import annotations

import time

import can

PID_RES = 0xFF
PID_ERR = 0xFE
PID_EV = 0xFD
PID_SERV = 0xFC


class XcpError(Exception):
    def __init__(self, code):
        super().__init__("XCP ERR 0x%02X" % code)
        self.code = code


class XcpClient:
    def __init__(self, channel="vcan0", cmd_id=0x555, res_id=0x556,
                 bitrate=500000, timeout=0.2):
        self.cmd_id = cmd_id
        self.res_id = res_id
        self.timeout = timeout
        self.bus = can.Bus(interface="socketcan", channel=channel, bitrate=bitrate)
        self.resource = 0
        self.max_cto = 8
        self.max_dto = 8
        self.last_error = 0
        self.daq_frames = 0

    # ---------------- 底层 ----------------
    def close(self):
        try:
            self.bus.shutdown()
        except Exception:
            pass

    def _send(self, payload):
        data = bytes(payload)[:8]
        data = data + b"\x00" * (8 - len(data))
        self.bus.send(can.Message(arbitration_id=self.cmd_id,
                                  is_extended_id=False, data=data, dlc=8))

    def recv_raw(self, timeout=None):
        return self.bus.recv(self.timeout if timeout is None else timeout)

    def request(self, payload):
        """发 CTO 并等 RES/ERR；期间收到的 DAQ 帧直接丢弃（统计里计 daq_frames）"""
        self._send(payload)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            msg = self.recv_raw(deadline - time.monotonic())
            if msg is None or msg.arbitration_id != self.res_id:
                continue
            d = bytes(msg.data)
            if not d:
                continue
            if d[0] == PID_RES:
                return d[1:]
            if d[0] == PID_ERR:
                self.last_error = d[1]
                raise XcpError(d[1])
            if d[0] == PID_EV:
                continue
            self.daq_frames += 1          # 0x00..0xFB
        raise TimeoutError("XCP 超时：命令 0x%02X 无响应" % payload[0])

    # ---------------- 标准命令 ----------------
    def connect(self, mode=0):
        r = self.request([0xFF, mode])
        self.resource = r[0]
        comm = r[1]
        self.max_cto = r[2]
        self.max_dto = r[3] | (r[4] << 8)
        return {"resource": self.resource, "comm_mode_basic": comm,
                "max_cto": self.max_cto, "max_dto": self.max_dto,
                "protocol": r[5], "transport": r[6]}

    def disconnect(self):
        return self.request([0xFE])

    def get_status(self):
        r = self.request([0xFD])
        return {"session_status": r[0] | (r[1] << 8),
                "protection": r[2], "state": r[3]}

    def get_id(self, mode=0):
        r = self.request([0xFA, mode])
        length = int.from_bytes(r[3:7], "little")
        name = b""
        while len(name) < length:            # GET_ID 后 MTA 已指向标识串
            n = min(7, length - len(name))
            name += self.upload(n)
        return name.decode("ascii", "replace")

    def set_mta(self, addr, ext=0):
        return self.request([0xF6, 0, ext] + list(addr.to_bytes(4, "little")))

    def upload(self, n):
        if n > 7:
            raise ValueError("UPLOAD 一次最多 7 字节")
        return self.request([0xF5, n])

    def short_upload(self, n, addr, ext=0):
        return self.request([0xF4, 0, n, ext] + list(addr.to_bytes(4, "little")))

    def download(self, data):
        if len(data) > 6:
            raise ValueError("DOWNLOAD 一帧最多 6 字节")
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
        return self.request([0xE1, bit_off, size, ext] + list(addr.to_bytes(4, "little")))

    def set_daq_list_mode(self, mode, list_no, event, prescaler=1, priority=0):
        return self.request([0xE0, mode, list_no & 0xFF, (list_no >> 8) & 0xFF,
                             event & 0xFF, (event >> 8) & 0xFF, prescaler, priority])

    def start_stop_daq_list(self, mode, list_no):
        r = self.request([0xDE, mode, list_no & 0xFF, (list_no >> 8) & 0xFF])
        return r[0] if r else 0xFF

    def start_stop_synch(self, mode):
        return self.request([0xDD, mode])
