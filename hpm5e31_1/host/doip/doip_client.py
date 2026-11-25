#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""doip_client.py - DoIP(ISO 13400-2) 上位机：车辆发现 -> 路由激活 -> UDS 诊断 -> 大报文/异常用例

只用 Python 标准库（socket/struct/time），不依赖第三方包：
    python3 doip_client.py discover --count 5
    python3 doip_client.py announce --secs 5
    python3 doip_client.py entity
    python3 doip_client.py route --sa 0x0E00
    python3 doip_client.py uds
    python3 doip_client.py bulk --size 4000 --count 20
    python3 doip_client.py err
    python3 doip_client.py all

板端固件：samples/app/doip_server（板 192.168.2.100，主机 enp6s0 192.168.2.1/24）。
端口：UDP 13400（发现/声明/实体状态/电源模式），TCP 13400（路由激活 + 诊断报文）。

协议要点（第 32 章有逐字节表）：
  通用头 8 B = 版本(1) + 版本取反(1) + 载荷类型(2,大端) + 载荷长度(4,大端)
  0x8001 诊断报文 = SA(2) + TA(2) + UDS 数据(n)；板端回 0x8002 肯定应答 + 0x8001 响应
  0x8002/0x8003 应答 = 实体地址(2) + tester 地址(2) + 应答码(1)
"""

import argparse
import random
import socket
import struct
import sys
import time

# ---------------- 协议常量 ----------------
PT_GENERIC_NACK = 0x0000
PT_VEHICLE_ID_REQ = 0x0001
PT_VEHICLE_ID_REQ_EID = 0x0002
PT_VEHICLE_ID_REQ_VIN = 0x0003
PT_VEHICLE_ANNOUNCE = 0x0004
PT_ROUTING_ACT_REQ = 0x0005
PT_ROUTING_ACT_RESP = 0x0006
PT_ALIVE_CHECK_REQ = 0x0007
PT_ALIVE_CHECK_RESP = 0x0008
PT_ENTITY_STATUS_REQ = 0x4001
PT_ENTITY_STATUS_RESP = 0x4002
PT_POWER_MODE_REQ = 0x4003
PT_POWER_MODE_RESP = 0x4004
PT_DIAG_MESSAGE = 0x8001
PT_DIAG_ACK = 0x8002
PT_DIAG_NACK = 0x8003

PT_NAME = {
    PT_GENERIC_NACK: "generic-nack",
    PT_VEHICLE_ID_REQ: "vehicle-id-req",
    PT_VEHICLE_ID_REQ_EID: "vehicle-id-req-eid",
    PT_VEHICLE_ID_REQ_VIN: "vehicle-id-req-vin",
    PT_VEHICLE_ANNOUNCE: "vehicle-announce",
    PT_ROUTING_ACT_REQ: "routing-act-req",
    PT_ROUTING_ACT_RESP: "routing-act-resp",
    PT_ALIVE_CHECK_REQ: "alive-check-req",
    PT_ALIVE_CHECK_RESP: "alive-check-resp",
    PT_ENTITY_STATUS_REQ: "entity-status-req",
    PT_ENTITY_STATUS_RESP: "entity-status-resp",
    PT_POWER_MODE_REQ: "power-mode-req",
    PT_POWER_MODE_RESP: "power-mode-resp",
    PT_DIAG_MESSAGE: "diag-message",
    PT_DIAG_ACK: "diag-ack",
    PT_DIAG_NACK: "diag-nack",
}

NACK_CODE = {0x00: "incorrect pattern format", 0x01: "unknown payload type",
             0x02: "message too large", 0x03: "out of memory",
             0x04: "invalid payload length"}
RA_CODE = {0x00: "denied-unknown-SA", 0x01: "denied-all-sockets",
           0x02: "denied-SA-diff", 0x03: "denied-SA-already-active",
           0x04: "denied-missing-auth", 0x05: "denied-reject-confirm",
           0x06: "denied-unsupported-activation-type",
           0x07: "denied-secure-TLS-required",
           0x10: "OK", 0x11: "OK-need-confirm"}
DIAG_NACK_CODE = {0x02: "invalid source address", 0x03: "unknown target address",
                  0x04: "diagnostic message too large", 0x05: "out of memory",
                  0x06: "target unreachable", 0x07: "unknown network",
                  0x08: "transport protocol error"}

# ---------------- 统计 ----------------
STATS = {"pass": 0, "fail": 0}
RESULTS = []


def check(name, ok, detail=""):
    """统一 PASS/FAIL 输出 + 计数（书稿里的实测表格直接抄这张表）"""
    STATS["pass" if ok else "fail"] += 1
    RESULTS.append((name, ok, detail))
    print("  %-46s %s %s" % (name, "PASS" if ok else "FAIL", detail))
    return ok


def summary(title):
    total = STATS["pass"] + STATS["fail"]
    print("\n[%s] %d 项：PASS %d / FAIL %d -> %s"
          % (title, total, STATS["pass"], STATS["fail"],
             "PASS" if STATS["fail"] == 0 else "FAIL"))
    return STATS["fail"] == 0


def calc_key(seed, level):
    """UDS 0x27 种子->密钥，与 components/app_uds/src/app_uds.c 的 app_uds_calc_key() 一致"""
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


class DoIPError(Exception):
    pass


class DiagResult(object):
    """一次 0x8001 诊断交互的结果"""

    def __init__(self, ack_code, uds, pending, t_ack_us, t_resp_us, nack_code=None):
        self.ack_code = ack_code
        self.uds = uds
        self.pending = pending
        self.t_ack_us = t_ack_us
        self.t_resp_us = t_resp_us
        self.nack_code = nack_code

    def __repr__(self):
        return ("DiagResult(ack=0x%02X uds=%s pending=%d t_ack=%.1f us t_resp=%.1f us nack=%s)"
                % (self.ack_code if self.ack_code is not None else -1,
                   self.uds.hex(" ").upper()[:40], self.pending,
                   self.t_ack_us or -1, self.t_resp_us or -1, self.nack_code))


class DoIPClient(object):
    def __init__(self, board="192.168.2.100", port=13400, local_ip=None,
                 tester=0x0E00, entity=0x1000, timeout=2.0):
        self.board = board
        self.port = port
        self.local_ip = local_ip
        self.tester = tester
        self.entity = entity
        self.timeout = timeout
        self.tcp = None
        self.rxbuf = b""
        self.udp = None
        self.udp_seq = 0
        self.activated = False

    # ---------------- 通用头 ----------------
    @staticmethod
    def build_frame(pt, payload=b"", version=0x02):
        return struct.pack(">BBHI", version, version ^ 0xFF, pt, len(payload)) + payload

    @staticmethod
    def parse_frame(data):
        if len(data) < 8:
            raise DoIPError("frame too short: %d B" % len(data))
        ver, inv, pt, ln = struct.unpack(">BBHI", data[:8])
        if (ver ^ inv) != 0xFF:
            raise DoIPError("bad version inverse: 0x%02X/0x%02X" % (ver, inv))
        if len(data) - 8 != ln:
            raise DoIPError("payload length mismatch: header %d / got %d" % (ln, len(data) - 8))
        return ver, pt, data[8:]

    # ---------------- UDP：车辆发现 / 声明 ----------------
    def _udp_socket(self, timeout):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.bind((self.local_ip or "0.0.0.0", self.port))
        s.settimeout(timeout)
        return s

    def udp_request(self, pt, payload=b"", timeout=None):
        """发一条 UDP 请求，收一条响应；返回 (rtt_us, pt, payload, src)"""
        to = timeout if timeout is not None else self.timeout
        s = self._udp_socket(to)
        try:
            t0 = time.perf_counter()
            s.sendto(self.build_frame(pt, payload), (self.board, self.port))
            data, src = s.recvfrom(65535)
            rtt = (time.perf_counter() - t0) * 1e6
            ver, rpt, rpay = self.parse_frame(data)
            return rtt, rpt, rpay, src
        finally:
            s.close()

    @staticmethod
    def parse_announce(payload):
        """0x0004 载荷：VIN(17) 逻辑地址(2) EID(6) GID(6) further(1) [同步状态(1)]"""
        if len(payload) < 32:
            raise DoIPError("announce payload %d B < 32" % len(payload))
        vin = payload[0:17].rstrip(b"\x00").decode("ascii", "replace")
        addr = struct.unpack(">H", payload[17:19])[0]
        eid = payload[19:25].hex(":").upper()
        gid = payload[25:31].hex(":").upper()
        further = payload[31]
        sync = payload[32] if len(payload) >= 33 else None
        return dict(vin=vin, addr=addr, eid=eid, gid=gid, further=further, sync=sync,
                    raw_len=len(payload))

    def discover(self, kind="plain", count=5, timeout=None):
        """车辆识别：0x0001 无参数 / 0x0002 带 EID / 0x0003 带 VIN；统计往返时延"""
        if kind == "eid":
            pt, payload = PT_VEHICLE_ID_REQ_EID, bytes.fromhex("FFFFFFFFFFFF")
        elif kind == "vin":
            pt, payload = PT_VEHICLE_ID_REQ_VIN, b"\x00" * 17
        else:
            pt, payload = PT_VEHICLE_ID_REQ, b""
        rtts = []
        info = None
        for _ in range(count):
            rtt, rpt, rpay, _src = self.udp_request(pt, payload, timeout)
            if rpt != PT_VEHICLE_ANNOUNCE:
                raise DoIPError("期望 0x0004，收到 0x%04X" % rpt)
            info = self.parse_announce(rpay)
            rtts.append(rtt)
        info["rtt_us"] = rtts
        return info

    def listen_announce(self, secs=5.0):
        """监听广播车辆声明（板端上电 3 次 / 或 doip timer period 打开的周期声明）"""
        s = self._udp_socket(0.2)
        out = []
        t_end = time.perf_counter() + secs
        try:
            while time.perf_counter() < t_end:
                try:
                    data, src = s.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    _ver, pt, pay = self.parse_frame(data)
                except DoIPError:
                    continue
                if pt != PT_VEHICLE_ANNOUNCE:
                    continue
                info = self.parse_announce(pay)
                info["t"] = time.perf_counter()
                info["src"] = src
                out.append(info)
        finally:
            s.close()
        return out

    def entity_status(self):
        rtt, pt, pay, _ = self.udp_request(PT_ENTITY_STATUS_REQ)
        if pt != PT_ENTITY_STATUS_RESP or len(pay) < 7:
            raise DoIPError("实体状态响应异常 pt=0x%04X len=%d" % (pt, len(pay)))
        node_type, max_sock, open_sock = pay[0], pay[1], pay[2]
        max_size = struct.unpack(">I", pay[3:7])[0]
        return dict(rtt_us=rtt, node_type=node_type, max_sockets=max_sock,
                    open_sockets=open_sock, max_data_size=max_size)

    def power_mode(self):
        rtt, pt, pay, _ = self.udp_request(PT_POWER_MODE_REQ)
        if pt != PT_POWER_MODE_RESP or len(pay) < 1:
            raise DoIPError("电源模式响应异常 pt=0x%04X len=%d" % (pt, len(pay)))
        return dict(rtt_us=rtt, mode=pay[0])

    # ---------------- TCP ----------------
    def connect(self, timeout=None):
        to = timeout if timeout is not None else self.timeout
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.tcp.settimeout(to)
        self.tcp.connect((self.board, self.port))
        self.rxbuf = b""
        return self

    def _recv_exact(self, n, timeout):
        """从 TCP 流里精确取 n 字节（多收的留在 self.rxbuf，下次接着用）"""
        t_end = time.perf_counter() + timeout
        while len(self.rxbuf) < n:
            remain = t_end - time.perf_counter()
            if remain <= 0:
                raise socket.timeout("recv %d B timeout (got %d)" % (n, len(self.rxbuf)))
            self.tcp.settimeout(remain)
            chunk = self.tcp.recv(65535)
            if not chunk:
                raise DoIPError("连接被对端关闭")
            self.rxbuf += chunk
        out = self.rxbuf[:n]
        self.rxbuf = self.rxbuf[n:]
        return out

    def recv_frame(self, timeout=None):
        """收一整帧（8 B 头 + 载荷）；返回 (pt, payload, t_us)"""
        to = timeout if timeout is not None else self.timeout
        t0 = time.perf_counter()
        hdr = self._recv_exact(8, to)
        ver, inv, pt, ln = struct.unpack(">BBHI", hdr)
        if (ver ^ inv) != 0xFF:
            raise DoIPError("bad version inverse 0x%02X/0x%02X" % (ver, inv))
        pay = self._recv_exact(ln, to) if ln else b""
        return pt, pay, (time.perf_counter() - t0) * 1e6

    def send_frame(self, pt, payload=b""):
        self.tcp.sendall(self.build_frame(pt, payload))

    def routing_activation(self, sa=None, at=0x00, oem=None, with_oem=None, timeout=None):
        """0x0005 路由激活；返回 (code, dict, rtt_us)"""
        sa = self.tester if sa is None else sa
        if with_oem is None:
            with_oem = oem is not None
        payload = struct.pack(">HB", sa, at) + b"\x00\x00\x00\x00"
        if with_oem:
            payload += (oem if oem is not None else b"\x00\x00\x00\x00")
        t0 = time.perf_counter()
        self.send_frame(PT_ROUTING_ACT_REQ, payload)
        pt, pay, _ = self.recv_frame(timeout)
        rtt = (time.perf_counter() - t0) * 1e6
        if pt != PT_ROUTING_ACT_RESP:
            raise DoIPError("期望 0x0006，收到 0x%04X" % pt)
        if len(pay) < 5:
            raise DoIPError("路由激活响应 %d B < 5" % len(pay))
        t_sa, t_ta, code = struct.unpack(">HHB", pay[:5])
        info = dict(tester_sa=t_sa, entity_addr=t_ta, code=code,
                    code_name=RA_CODE.get(code, "?"),
                    reserved=pay[5:9].hex(), oem=pay[9:13].hex() if len(pay) >= 13 else "")
        if code == 0x10:
            self.activated = True
        return code, info, rtt

    def send_diag(self, uds, sa=None, ta=None, timeout=None, keep_pending=True):
        """发 0x8001 诊断报文；返回 DiagResult（含 0x8002 ack 与最终 UDS 响应）"""
        sa = self.tester if sa is None else sa
        ta = self.entity if ta is None else ta
        to = timeout if timeout is not None else self.timeout
        payload = struct.pack(">HH", sa, ta) + uds
        t0 = time.perf_counter()
        self.send_frame(PT_DIAG_MESSAGE, payload)
        ack_code = None
        t_ack = None
        pending = 0
        while True:
            pt, pay, _ = self.recv_frame(to)
            if pt == PT_DIAG_ACK:
                ack_code = pay[4] if len(pay) >= 5 else None
                t_ack = (time.perf_counter() - t0) * 1e6
                continue
            if pt == PT_DIAG_NACK:
                nack = pay[4] if len(pay) >= 5 else None
                return DiagResult(ack_code, b"", pending, t_ack,
                                  (time.perf_counter() - t0) * 1e6, nack)
            if pt == PT_DIAG_MESSAGE:
                if len(pay) < 4:
                    raise DoIPError("诊断响应载荷 %d B < 4" % len(pay))
                rsa, rta = struct.unpack(">HH", pay[:4])
                body = pay[4:]
                if (rsa, rta) != (self.entity, sa):
                    # 地址方向不对：不静默放过，直接报错（书稿“易错点”里记录的方向问题）
                    raise DoIPError("诊断响应地址不符: SA=0x%04X TA=0x%04X (期望 0x%04X/0x%04X)"
                                    % (rsa, rta, self.entity, sa))
                if keep_pending and len(body) >= 3 and body[0] == 0x7F and body[2] == 0x78:
                    pending += 1
                    continue
                return DiagResult(ack_code, body, pending, t_ack,
                                  (time.perf_counter() - t0) * 1e6)
            # 其它类型（如 alive check 请求）忽略但记录
            if pt == PT_ALIVE_CHECK_REQ:
                self.send_frame(PT_ALIVE_CHECK_RESP, struct.pack(">H", sa))

    def uds(self, *payload):
        """发 UDS 请求，返回响应字节；负响应也原样返回（调用者自己判 NRC）"""
        res = self.send_diag(bytes(payload))
        return res.uds

    def read_doip_stats(self):
        """读板端 0x0302 例程的 DoIP 计数器（14 个 u32：收发/路由/超时/连接槽）"""
        r = self.uds(0x31, 0x01, 0x03, 0x02)          # start
        if len(r) < 6 or r[0] != 0x71:
            return None
        r = self.uds(0x31, 0x03, 0x03, 0x02)          # requestResults
        if len(r) < 6 or r[0] != 0x71:
            return None
        body = r[6:6 + 56]
        if len(body) < 56:
            return None
        v = struct.unpack(">14I", body)
        keys = ["diag_rx", "diag_tx", "big_msg", "route_ok", "route_deny", "udp_rx",
                "err_hdr", "alive_timeout", "tcp_accept", "tcp_close", "initial_timeout",
                "conn0", "conn1", "conn2"]
        st = dict(zip(keys, v))
        for i in range(3):
            raw = st["conn%d" % i]
            st["conn%d" % i] = dict(state=(raw >> 16) & 0xFFFF, tester=raw & 0xFFFF)
        return st

    def alive_check(self, timeout=None):
        """收 0x0007 回 0x0008（板端在“同 SA 新连接接管”时会主动发）"""
        pt, pay, _ = self.recv_frame(timeout)
        if pt != PT_ALIVE_CHECK_REQ:
            raise DoIPError("期望 0x0007，收到 0x%04X" % pt)
        self.send_frame(PT_ALIVE_CHECK_RESP, struct.pack(">H", self.tester))
        return True

    def close(self):
        if self.tcp is not None:
            try:
                self.tcp.close()
            except OSError:
                pass
            self.tcp = None
        self.activated = False

    # ---------------- 高层 UDS 封装 ----------------
    def session(self, sub=0x03):
        return self.uds(0x10, sub)

    def read_did(self, *dids):
        req = [0x22]
        for d in dids:
            req += [(d >> 8) & 0xFF, d & 0xFF]
        return self.uds(*req)

    def security_access(self, level=1):
        r = self.uds(0x27, 0x01 if level == 1 else 0x03)
        if len(r) != 6 or r[0] != 0x67:
            return None
        seed = struct.unpack(">I", r[2:6])[0]
        key = calc_key(seed, level)
        r2 = self.uds(0x27, 0x02 if level == 1 else 0x04,
                      (key >> 24) & 0xFF, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF)
        return dict(seed=seed, key=key, resp=r2)


def hexs(data):
    return data.hex(" ").upper()


# ---------------- 子命令 ----------------
def cmd_discover(args):
    print("\n== 车辆发现（UDP 0x0001/0x0002/0x0003 -> 0x0004）==")
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    for kind in ("plain", "eid", "vin"):
        if args.kind not in ("all", kind):
            continue
        info = c.discover(kind, args.count, args.timeout)
        rtts = info["rtt_us"]
        print("  [%s] VIN=%s 逻辑地址=0x%04X EID=%s GID=%s further=0x%02X 载荷=%d B"
              % (kind, info["vin"], info["addr"], info["eid"], info["gid"],
                 info["further"], info["raw_len"]))
        print("       RTT min/avg/max = %.1f / %.1f / %.1f us（%d 次）"
              % (min(rtts), sum(rtts) / len(rtts), max(rtts), len(rtts)))
        check("0x%04X 车辆识别响应 -> 0x0004" % pt,
              info["vin"] != "" and info["addr"] != 0, "RTT avg %.1f us" % (sum(rtts) / len(rtts)))
    if args.kind == "all":
        # 0x0002/0x0003 的 PT 号单独判一次（上面 check 只覆盖 plain）
        pass
    return summary("discover")


def cmd_announce(args):
    print("\n== 广播车辆声明（0x0004，板端周期声明需先 doip timer period <ms>）==")
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    got = c.listen_announce(args.secs)
    if not got:
        check("收到广播声明", False, "%.1f s 内 0 条（板端默认只在上电发 3 次；"
              "用 doip timer period 1000 打开周期声明）" % args.secs)
        return summary("announce")
    gaps = [(got[i]["t"] - got[i - 1]["t"]) * 1e3 for i in range(1, len(got))]
    print("  收到 %d 条，来源 %s:%d，VIN=%s 逻辑地址=0x%04X"
          % (len(got), got[0]["src"][0], got[0]["src"][1], got[0]["vin"], got[0]["addr"]))
    if gaps:
        print("  间隔 min/avg/max = %.1f / %.1f / %.1f ms"
              % (min(gaps), sum(gaps) / len(gaps), max(gaps)))
    check("收到广播声明", True, "%d 条 / %.1f s" % (len(got), args.secs))
    if gaps:
        check("声明周期稳定（σ < 30 ms）", max(gaps) - min(gaps) < 30.0,
              "min %.1f / max %.1f ms" % (min(gaps), max(gaps)))
    return summary("announce")


def cmd_entity(args):
    print("\n== 实体状态 0x4001/0x4002 与诊断电源模式 0x4003/0x4004 ==")
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    st = c.entity_status()
    print("  节点类型=%d（0=DoIP 节点 1=网关） 最大 TCP 连接=%d 当前连接=%d 最大报文=%d B RTT=%.1f us"
          % (st["node_type"], st["max_sockets"], st["open_sockets"],
             st["max_data_size"], st["rtt_us"]))
    check("0x4001 实体状态", st["max_sockets"] > 0 and st["max_data_size"] >= 1024,
          "max_data_size=%d B" % st["max_data_size"])
    pm = c.power_mode()
    print("  诊断电源模式=0x%02X（0x00 未就绪 / 0x01 就绪）RTT=%.1f us" % (pm["mode"], pm["rtt_us"]))
    check("0x4003 诊断电源模式", pm["mode"] in (0x00, 0x01), "mode=0x%02X" % pm["mode"])
    return summary("entity")


def cmd_route(args):
    print("\n== 路由激活 0x0005/0x0006 ==")
    c = DoIPClient(board=args.board, local_ip=args.local,
                   tester=args.sa, timeout=args.timeout)
    c.connect()
    try:
        code, info, rtt = c.routing_activation()
        print("  请求 SA=0x%04X -> 响应 tester=0x%04X 实体=0x%04X code=0x%02X(%s) RTT=%.1f us"
              % (args.sa, info["tester_sa"], info["entity_addr"], code,
                 info["code_name"], rtt))
        check("路由激活成功（0x10）", code == 0x10, "code=0x%02X" % code)
        # 重复激活（同 SA、同 socket）应仍是 0x10（幂等）
        code2, _info2, _ = c.routing_activation()
        check("重复路由激活幂等", code2 == 0x10, "code=0x%02X" % code2)
    finally:
        c.close()
    return summary("route")


def _do_uds_sequence(c):
    """完整 UDS 序列（10/22/2E/27/31/19/3E），与第 31 章 uds_ecu 的序列一致"""
    r = c.session(0x03)
    check("10 03 扩展会话", r[:2] == b"\x50\x03", hexs(r))

    r = c.read_did(0xF190, 0xF195, 0xF187, 0xF18C)
    ok = len(r) > 3 and r[0] == 0x62
    vin = r[3:20].decode("ascii", "replace") if ok and len(r) >= 20 else "?"
    check("22 读 4 个 DID", ok, "VIN=%s" % vin)

    # 未解锁写 VIN -> NRC 0x33
    new_vin = b"HPM5E31DOIPVIN001"
    r = c.uds(0x2E, 0xF1, 0x90) + new_vin if False else c.uds(0x2E, 0xF1, 0x90, *new_vin)
    check("2E F190 未解锁 -> NRC 0x33", r == b"\x7F\x2E\x33", hexs(r))

    sec = c.security_access(1)
    if sec is None:
        check("27 01/02 安全访问", False, "没拿到种子")
        return
    print("  27 01 种子=0x%08X key=0x%08X -> %s" % (sec["seed"], sec["key"], hexs(sec["resp"])))
    check("27 01/02 安全访问", sec["resp"][:2] == b"\x67\x02", hexs(sec["resp"]))

    r = c.uds(0x2E, 0xF1, 0x90, *new_vin)
    check("2E F190 已解锁写入", r[:3] == b"\x6E\xF1\x90", hexs(r))

    r = c.read_did(0xF190)
    check("22 F190 回读写入的 VIN", len(r) == 20 and r[3:20] == new_vin,
          r[3:20].decode("ascii", "replace") if len(r) >= 20 else hexs(r))

    r = c.uds(0x31, 0x01, 0x02, 0x03)
    check("31 01 0203 自检例程", r[:4] == b"\x71\x01\x02\x03", hexs(r))

    r = c.uds(0x19, 0x02, 0xFF)
    check("19 02 读 DTC", len(r) >= 3 and r[0] == 0x59, hexs(r)[:40])

    # 慢例程：板端先回 7F 31 78，再回 71 01 03 01（验证 DoIP 上的 P2* 续期）
    res = c.send_diag(bytes([0x31, 0x01, 0x03, 0x01]), timeout=3.0)
    check("31 01 0301 慢例程（NRC 0x78 + P2* 续期）",
          res.uds[:4] == b"\x71\x01\x03\x01" and res.pending >= 1,
          "pending=%d t_ack=%.0f us t_resp=%.1f ms"
          % (res.pending, res.t_ack_us or -1, (res.t_resp_us or 0) / 1e3))

    r = c.uds(0x3E, 0x00)
    check("3E 00 会话保持", r[:2] == b"\x7E\x00", hexs(r))

    r = c.uds(0x10, 0x01)
    check("10 01 回默认会话", r[:2] == b"\x50\x01", hexs(r))


def cmd_uds(args):
    print("\n== 路由激活 + UDS 诊断序列（0x10/0x22/0x2E/0x27/0x31/0x19/0x3E）==")
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    c.connect()
    try:
        code, info, rtt = c.routing_activation()
        check("路由激活 0x10", code == 0x10, "code=0x%02X RTT=%.1f us" % (code, rtt))
        _do_uds_sequence(c)
    finally:
        c.close()
    return summary("uds")


def cmd_bulk(args):
    print("\n== DoIP 大报文：入向 %d B / 出向 %d B（TCP 分段与吞吐）=="
          % (args.size, args.size))
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=5.0)
    c.connect()
    try:
        code, _info, _ = c.routing_activation()
        if code != 0x10:
            check("路由激活", False, "code=0x%02X" % code)
            return summary("bulk")
        # 1) 入向：一条 size 字节 UDS 请求（0x2E 写 F190 + 填充），UDS 应回 NRC 0x13（长度错）
        uds = bytes([0x2E, 0xF1, 0x90]) + bytes((i & 0xFF) for i in range(args.size - 3))
        res = c.send_diag(uds, timeout=5.0)
        check("入向 %d B 诊断报文被正确分帧" % args.size,
              res.ack_code == 0x00 and len(res.uds) >= 1,
              "ack=0x%02X UDS=%s t_ack=%.1f ms t_resp=%.1f ms"
              % (res.ack_code or -1, hexs(res.uds)[:20],
                 (res.t_ack_us or 0) / 1e3, (res.t_resp_us or 0) / 1e3))
        check("入向超长 UDS 请求被 UDS 拒绝（NRC 0x13/0x31）",
              len(res.uds) == 3 and res.uds[0] == 0x7F and res.uds[2] in (0x13, 0x31),
              hexs(res.uds))
        # 2) 出向：UDS 例程 0x0204 触发板端连发 count 条 size 字节 0x8001
        n = args.size
        cnt = args.count
        c.send_frame(PT_DIAG_MESSAGE,
                     struct.pack(">HH", c.tester, c.entity)
                     + bytes([0x31, 0x01, 0x02, 0x04, (n >> 8) & 0xFF, n & 0xFF,
                              (cnt >> 8) & 0xFF, cnt & 0xFF]))
        t0 = time.perf_counter()
        sizes = []
        bench_done = None
        while True:
            pt, pay, _ = c.recv_frame(5.0)
            if pt == PT_DIAG_ACK:
                continue
            if pt == PT_DIAG_MESSAGE:
                body = pay[4:]
                if body[:2] == b"\x62\xF1":      # bench 伪 UDS 响应
                    sizes.append(len(body))
                    continue
                bench_done = (time.perf_counter() - t0) * 1e6
                check("UDS 例程 0x0204 触发 %d 条 %d B 0x8001" % (cnt, n),
                      len(sizes) == cnt and all(s == n for s in sizes),
                      "收到 %d 条，长度 %s，总耗时 %.1f ms"
                      % (len(sizes), sorted(set(sizes))[:3], bench_done / 1e3))
                break
        if sizes and bench_done:
            total_bytes = sum(sizes) + 8 * len(sizes)
            print("  出向吞吐：%d 条 x %d B（含 8 B 头 = %d B），%.1f ms -> %.2f Mbit/s"
                  % (len(sizes), sizes[0], total_bytes, bench_done / 1e3,
                     total_bytes * 8 / bench_done))
            check("出向大报文吞吐 > 10 Mbit/s",
                  total_bytes * 8 / bench_done > 10.0,
                  "%.2f Mbit/s" % (total_bytes * 8 / bench_done))
    finally:
        c.close()
    return summary("bulk")


def cmd_err(args):
    print("\n== 异常用例：错误路由激活 / 错误 SA/TA / 超时 / NACK ==")
    # 1) 未知 SA
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    c.connect()
    try:
        code, info, _ = c.routing_activation(sa=0x1234)
        check("未知 SA 0x1234 -> 拒绝 0x00", code == 0x00, info["code_name"])
        code, info, _ = c.routing_activation(sa=0x0E80)
        check("未配置的 SA 0x0E80 -> 拒绝 0x00", code == 0x00, info["code_name"])
        code, info, _ = c.routing_activation(sa=0x0E00, at=0x02)
        check("非法激活类型 0x02 -> 拒绝 0x06", code == 0x06, info["code_name"])
        # 未激活就发诊断 -> 0x8003 code 0x02
        res = c.send_diag(b"\x10\x01", sa=0x0E00)
        check("未路由激活发诊断 -> 0x8003/0x02", res.nack_code == 0x02,
              "nack=0x%02X" % (res.nack_code or 0))
        # 正常激活后测错误 TA / 错误 SA
        code, _info, _ = c.routing_activation()
        check("正常激活（前置）", code == 0x10, "code=0x%02X" % code)
        res = c.send_diag(b"\x10\x01", ta=0x2000)
        check("错误目标地址 TA=0x2000 -> 0x8003/0x03", res.nack_code == 0x03,
              "nack=0x%02X" % (res.nack_code or 0))
        res = c.send_diag(b"\x10\x01", sa=0x0000)
        check("非法源地址 SA=0x0000 -> 0x8003/0x02", res.nack_code == 0x02,
              "nack=0x%02X" % (res.nack_code or 0))
        # 通用头版本取反错误 -> 0x0000 NACK code 0x00 后断链
        c.tcp.sendall(struct.pack(">BBHI", 0x02, 0x00, PT_DIAG_MESSAGE, 4) + b"\x00" * 4)
        pt, pay, _ = c.recv_frame(1.0)
        check("通用头版本取反错误 -> 0x0000/0x00", pt == PT_GENERIC_NACK and pay[:1] == b"\x00",
              "pt=0x%04X code=0x%02X" % (pt, pay[0] if pay else -1))
        # 未知载荷类型 -> 0x0000 NACK code 0x01
        try:
            c2 = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
            c2.connect()
            c2.routing_activation()
            c2.send_frame(0x1234, b"\x01\x02\x03")
            pt, pay, _ = c2.recv_frame(1.0)
            check("未知载荷类型 0x1234 -> 0x0000/0x01",
                  pt == PT_GENERIC_NACK and pay[:1] == b"\x01",
                  "pt=0x%04X code=0x%02X" % (pt, pay[0] if pay else -1))
            c2.close()
        except (DoIPError, socket.timeout, OSError) as exc:
            check("未知载荷类型 0x1234 -> 0x0000/0x01", False, str(exc))
    finally:
        c.close()

    # 2) initial inactivity：连上不发路由激活，2 s 后板端应断开
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    c.connect()
    t0 = time.perf_counter()
    closed = False
    try:
        while (time.perf_counter() - t0) < 4.0:
            c.tcp.settimeout(0.5)
            try:
                data = c.tcp.recv(1024)
            except socket.timeout:
                continue
            if not data:
                closed = True
                break
    except (DoIPError, OSError):
        closed = True
    dt = time.perf_counter() - t0
    c.close()
    check("initial inactivity：2 s 未路由激活被断链", closed and 1.5 <= dt <= 3.5,
          "%.2f s 后断开" % dt)

    # 3) general inactivity：激活后静默，板端 general 定时器到点断链
    #    （板端默认 300 s；测试时用 doip timer general 3000 提前）
    c = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    c.connect()
    try:
        code, _info, _ = c.routing_activation()
        t0 = time.perf_counter()
        closed = False
        while (time.perf_counter() - t0) < 8.0:
            c.tcp.settimeout(0.5)
            try:
                data = c.tcp.recv(1024)
            except socket.timeout:
                continue
            if not data:
                closed = True
                break
        dt = time.perf_counter() - t0
        check("general inactivity：静默后断链（需 doip timer general 3000）",
              closed and 2.0 <= dt <= 6.0, "%.2f s 后断开" % dt)
    finally:
        c.close()

    # 4) 同 SA 双连接：老连接收到 alive check，500 ms 不应答被踢，新连接被拒 0x03
    c1 = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    c1.connect()
    code, _info, _ = c1.routing_activation()
    c2 = DoIPClient(board=args.board, local_ip=args.local, timeout=args.timeout)
    c2.connect()
    try:
        code2, info2, _ = c2.routing_activation()
        got_alive = False
        try:
            c1.tcp.settimeout(1.0)
            pt, pay, _ = c1.recv_frame(1.0)
            got_alive = (pt == PT_ALIVE_CHECK_REQ)
        except (socket.timeout, DoIPError):
            got_alive = False
        check("同 SA 新连接被拒 0x03 且老连接收到 0x0007",
              code2 == 0x03 and got_alive,
              "code=0x%02X alive_check=%s" % (code2, got_alive))
        # 老连接 500 ms 内不应答 -> 板端断链；随后新连接可成功激活
        time.sleep(0.8)
        code3, info3, _ = c2.routing_activation()
        check("alive check 超时后老连接被踢、新连接可激活",
              code3 == 0x10, "code=0x%02X" % code3)
    finally:
        c1.close()
        c2.close()
    return summary("err")


def cmd_all(args):
    ok = True
    ok &= cmd_discover(argparse.Namespace(board=args.board, local=args.local,
                                          timeout=args.timeout, kind="all", count=args.count))
    ok &= cmd_entity(args)
    ok &= cmd_announce(argparse.Namespace(board=args.board, local=args.local,
                                          timeout=args.timeout, secs=args.announce_secs))
    ok &= cmd_route(argparse.Namespace(board=args.board, local=args.local,
                                       timeout=args.timeout, sa=0x0E00))
    ok &= cmd_uds(args)
    ok &= cmd_bulk(argparse.Namespace(board=args.board, local=args.local,
                                      timeout=args.timeout, size=args.size,
                                      count=args.bulk_count))
    ok &= cmd_err(args)
    print("\n========== 总结果：%s（PASS %d / FAIL %d）=========="
          % ("PASS" if ok else "FAIL", STATS["pass"], STATS["fail"]))
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description="DoIP(ISO 13400-2) 上位机测试工具（标准库实现）")
    ap.add_argument("--board", default="192.168.2.100", help="板端 IP（默认 192.168.2.100）")
    ap.add_argument("--local", default="192.168.2.1", help="主机网卡 IP（默认 192.168.2.1）")
    ap.add_argument("--timeout", type=float, default=2.0, help="单次收发超时（秒）")
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("discover", help="UDP 车辆发现（0x0001/0x0002/0x0003）")
    p.add_argument("--kind", default="all", choices=["all", "plain", "eid", "vin"])
    p.add_argument("--count", type=int, default=5)
    p.set_defaults(func=cmd_discover)

    p = sub.add_parser("announce", help="监听广播车辆声明 0x0004")
    p.add_argument("--secs", type=float, default=5.0)
    p.set_defaults(func=cmd_announce)

    p = sub.add_parser("entity", help="实体状态 0x4001 + 电源模式 0x4003")
    p.set_defaults(func=cmd_entity)

    p = sub.add_parser("route", help="路由激活 0x0005/0x0006")
    p.add_argument("--sa", type=lambda x: int(x, 0), default=0x0E00)
    p.set_defaults(func=cmd_route)

    p = sub.add_parser("uds", help="路由激活 + 完整 UDS 服务序列")
    p.set_defaults(func=cmd_uds)

    p = sub.add_parser("bulk", help="大报文（入向/出向）与吞吐")
    p.add_argument("--size", type=int, default=4000)
    p.add_argument("--count", type=int, default=20)
    p.set_defaults(func=cmd_bulk)

    p = sub.add_parser("err", help="异常用例（错误 SA/TA、超时、NACK）")
    p.set_defaults(func=cmd_err)

    p = sub.add_parser("all", help="全部用例")
    p.add_argument("--count", type=int, default=5)
    p.add_argument("--size", type=int, default=4000)
    p.add_argument("--bulk-count", type=int, default=20)
    p.add_argument("--announce-secs", type=float, default=5.0)
    p.set_defaults(func=cmd_all)

    args = ap.parse_args(argv)
    if not getattr(args, "func", None):
        ap.print_help()
        return 2
    try:
        ok = args.func(args)
    except (DoIPError, socket.timeout, OSError) as exc:
        print("ERROR: %s" % exc)
        return 1
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
