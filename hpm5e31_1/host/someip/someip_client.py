#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
someip_client.py -- 自实现 SOME/IP 客户端（SOME/IP-SD + 方法 RPC + 事件订阅 + SOME/IP-TP）

用途：
  1) 板端例程的"无 vsomeip 依赖"验证工具（vsomeip 构建慢、配置坑多，先用它把线上行为确认下来）；
  2) 教学：每一段字节都自己拼，便于对照 AUTOSAR PRS SOME/IP 规范逐字段看报文。

覆盖：
  - SOME/IP 报文头（16 B，全大端）
  - SOME/IP-SD：FindService / OfferService 解析、SubscribeEventgroup / Ack、TTL
  - 方法 REQUEST -> RESPONSE / ERROR
  - 事件 NOTIFICATION 统计（周期、抖动、丢包、单向时延）
  - SOME/IP-TP：4 KB 载荷分段发送 / 分段接收重组 + CRC 校验

用法示例：
  python3 someip_client.py --board 192.168.2.100 discover
  python3 someip_client.py --board 192.168.2.100 rpc
  python3 someip_client.py --board 192.168.2.100 event --secs 10
  python3 someip_client.py --board 192.168.2.100 tp --size 4096
  python3 someip_client.py --board 192.168.2.100 all --secs 10
"""

import argparse
import os
import select
import socket
import struct
import sys
import time
import zlib

# ---------------------------------------------------------------- 常量
SD_MCAST = "224.244.224.245"
SD_PORT = 30490
SVC_PORT = 30501

SERVICE_ID = 0x1234
INSTANCE_ID = 0x0001
MAJOR_VERSION = 0x01
INTERFACE_VERSION = 0x01
EVENTGROUP = 0x0001

M_ADD = 0x0001
M_GET_VERSION = 0x0002
M_SET_LED = 0x0003
M_TEMP_GET = 0x0004
M_TEMP_SET = 0x0005
M_GET_TIME = 0x0006
M_CRC32 = 0x0007
M_GET_BULK = 0x0010

E_COUNTER = 0x8001
E_ADC = 0x8002
E_TEMP = 0x8004

MT_REQUEST = 0x00
MT_REQUEST_NO_RETURN = 0x01
MT_NOTIFICATION = 0x02
MT_RESPONSE = 0x80
MT_ERROR = 0x81
TP_FLAG = 0x20

RC_OK = 0x00
RC_NAMES = {
    0x00: "E_OK",
    0x01: "E_NOT_OK",
    0x02: "E_UNKNOWN_SERVICE",
    0x03: "E_UNKNOWN_METHOD",
    0x04: "E_NOT_READY",
    0x05: "E_NOT_REACHABLE",
    0x06: "E_TIMEOUT",
    0x07: "E_WRONG_PROTOCOL_VERSION",
    0x08: "E_WRONG_INTERFACE_VERSION",
    0x09: "E_MALFORMED_MESSAGE",
    0x0A: "E_WRONG_MESSAGE_TYPE",
}
MT_NAMES = {
    0x00: "REQUEST", 0x01: "REQUEST_NO_RETURN", 0x02: "NOTIFICATION",
    0x20: "REQUEST_TP", 0x21: "REQUEST_NO_RETURN_TP", 0x22: "NOTIFICATION_TP",
    0x80: "RESPONSE", 0x81: "ERROR", 0xA0: "RESPONSE_TP", 0xA1: "ERROR_TP",
}

TP_SEG = 1392


# ---------------------------------------------------------------- 编解码
def enc_header(service, method, client, session, msg_type, rc, payload,
               iface=INTERFACE_VERSION, proto=0x01):
    """SOME/IP 头：MessageID(service,method) | Length(u32) | RequestID(client,session) |
    ProtocolVersion | InterfaceVersion | MessageType | ReturnCode"""
    length = 8 + len(payload)
    return struct.pack("!HHIHHBBBB", service, method, length, client, session,
                       proto, iface, msg_type, rc) + payload


def dec_header(data):
    if len(data) < 16:
        return None
    (service, method, length, client, session,
     proto, iface, mtype, rc) = struct.unpack("!HHIHHBBBB", data[:16])
    if length + 8 > len(data):
        return None
    return dict(service=service, method=method, length=length, client=client,
                session=session, proto=proto, iface=iface, mtype=mtype, rc=rc,
                payload=data[16:16 + (length - 8)])


def enc_sd_entry_service(etype, service, instance, major, ttl, minor=0):
    return struct.pack("!BBBBHHB3sI", etype, 0, 0, 0, service, instance, major,
                       ttl.to_bytes(3, "big"), minor)


def enc_sd_entry_eventgroup(etype, service, instance, major, ttl, eventgroup, counter=1,
                            num_options=0):
    # 第 4 字节 = 两段 option run 的个数（低 4 位 = 第一个 run）。带 endpoint option 时必须写 1，
    # 否则对端会认为"没有 option"，事件就退化成发给订阅者的 SD 端点（30490）——踩过的坑（第 35 章坑 5）。
    return struct.pack("!BBBBHHB3sHH", etype, 0, 0, num_options, service, instance, major,
                       ttl.to_bytes(3, "big"), counter, eventgroup)


def enc_sd_option_ipv4(ip, port, otype=0x04, proto=0x11):
    """IPv4 Endpoint Option：Length=9（不含 Length 字段与 Type 字段），总长 12 B"""
    return struct.pack("!HBB4sBBH", 9, otype, 0, socket.inet_aton(ip), 0, proto, port)


def enc_sd(session, entries, options, flags=0xC0):
    payload = struct.pack("!BBBB", flags, 0, 0, 0)
    payload += struct.pack("!I", 16 * len(entries)) + b"".join(entries)
    opt = b"".join(options)
    payload += struct.pack("!I", len(opt)) + opt
    return enc_header(0xFFFF, 0x8100, 0x0000, session & 0xFFFF, MT_NOTIFICATION, RC_OK, payload)


def dec_sd(payload):
    if len(payload) < 8:
        return None
    out = dict(flags=payload[0], entries=[], options=[])
    elen = struct.unpack("!I", payload[4:8])[0]
    off = 8
    if off + elen + 4 > len(payload):
        return None
    for i in range(elen // 16):
        e = payload[off + i * 16: off + (i + 1) * 16]
        etype = e[0]
        service, instance = struct.unpack("!HH", e[4:8])
        major = e[8]
        ttl = int.from_bytes(e[9:12], "big")
        rec = dict(type=etype, service=service, instance=instance, major=major,
                   ttl=ttl, idx1=e[1], idx2=e[2], numopts=e[3])
        if etype in (0x00, 0x01):
            rec["minor"] = struct.unpack("!I", e[12:16])[0]
        else:
            rec["counter"] = struct.unpack("!H", e[12:14])[0] & 0x0F
            rec["eventgroup"] = struct.unpack("!H", e[14:16])[0]
        out["entries"].append(rec)
    off += elen
    olen = struct.unpack("!I", payload[off:off + 4])[0]
    off += 4
    used = 0
    while used + 3 <= olen:
        ln = struct.unpack("!H", payload[off + used: off + used + 2])[0]
        otype = payload[off + used + 2]
        if ln < 1:
            break
        total = 3 + ln
        if used + total > olen:
            break
        if otype in (0x04, 0x24, 0x14) and ln >= 9:
            out["options"].append(dict(
                type=otype,
                ip=socket.inet_ntoa(payload[off + used + 4: off + used + 8]),
                proto=payload[off + used + 9],
                port=struct.unpack("!H", payload[off + used + 10: off + used + 12])[0]))
        used += total
    return out


def hexdump(data, limit=64):
    s = " ".join("%02X" % b for b in data[:limit])
    return s + (" ..." if len(data) > limit else "")

# ---------------------------------------------------------------- 客户端
class SomeIpClient(object):
    def __init__(self, board_ip="192.168.2.100", local_ip="192.168.2.1",
                 event_port=30502, verbose=False):
        self.board_ip = board_ip
        self.local_ip = local_ip
        self.event_port = event_port
        self.verbose = verbose
        self.session = 0x0100
        self.client_id = 0x00A5
        self.tp_segs = 0
        self.tp_bytes = 0

        # SD socket：监听 30490（组播 OfferService + 单播 Ack 都从这里收）
        self.sd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.sd.bind(("", SD_PORT))
        except OSError as exc:
            raise SystemExit("bind UDP %d failed: %s（是否有别的进程占用？）" % (SD_PORT, exc))
        mreq = struct.pack("4s4s", socket.inet_aton(SD_MCAST), socket.inet_aton(local_ip))
        try:
            self.sd.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        except OSError as exc:
            raise SystemExit("join %s on %s failed: %s（网卡/路由不对？）" % (SD_MCAST, local_ip, exc))
        self.sd.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
        self.sd.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        self.sd.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

        # 服务 socket：方法请求/响应 + 事件接收（事件端点由 subscribe 里的 option 通告给板端）
        self.svc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.svc.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.svc.bind((local_ip, event_port))

        self.offers = []
        self.ack = None

    # ---------------- 基础 ----------------
    def next_session(self):
        self.session = (self.session + 1) & 0xFFFF
        if self.session == 0:
            self.session = 1
        return self.session

    def log(self, msg):
        if self.verbose:
            print("    [dbg] " + msg)

    def close(self):
        try:
            mreq = struct.pack("4s4s", socket.inet_aton(SD_MCAST), socket.inet_aton(self.local_ip))
            self.sd.setsockopt(socket.IPPROTO_IP, socket.IP_DROP_MEMBERSHIP, mreq)
        except OSError:
            pass
        self.sd.close()
        self.svc.close()

    # ---------------- SD ----------------
    def find_service(self, timeout=1.0):
        """发 FindService（组播），返回 (收到 Offer 的时刻, 条目) 或 (None, None)"""
        pkt = enc_sd(self.next_session(),
                     [enc_sd_entry_service(0x00, SERVICE_ID, INSTANCE_ID, MAJOR_VERSION, 3)],
                     [])
        t_send = time.time()
        self.sd.sendto(pkt, (SD_MCAST, SD_PORT))
        self.log("FindService -> %s:%d" % (SD_MCAST, SD_PORT))
        deadline = t_send + timeout
        while time.time() < deadline:
            r, _, _ = select.select([self.sd], [], [], max(0.0, deadline - time.time()))
            if not r:
                continue
            data, addr = self.sd.recvfrom(2048)
            hdr = dec_header(data)
            if hdr is None or hdr["service"] != 0xFFFF or hdr["method"] != 0x8100:
                continue
            sd = dec_sd(hdr["payload"])
            if not sd:
                continue
            for e in sd["entries"]:
                if e["type"] == 0x01 and e["service"] == SERVICE_ID:
                    ep = sd["options"][0] if sd["options"] else None
                    return time.time(), e, ep, addr
        return None, None, None, None

    def wait_offer(self, timeout=5.0, require_ttl=True):
        """听组播 OfferService，返回 (时刻, 条目, endpoint option)"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            r, _, _ = select.select([self.sd], [], [], max(0.0, deadline - time.time()))
            if not r:
                continue
            data, addr = self.sd.recvfrom(2048)
            hdr = dec_header(data)
            if hdr is None or hdr["service"] != 0xFFFF or hdr["method"] != 0x8100:
                continue
            sd = dec_sd(hdr["payload"])
            if not sd:
                continue
            for e in sd["entries"]:
                if e["type"] != 0x01:
                    continue
                if e["service"] != SERVICE_ID:
                    continue
                if require_ttl and e["ttl"] == 0:
                    continue
                ep = sd["options"][0] if sd["options"] else None
                self.offers.append((time.time(), e, ep, addr))
                return time.time(), e, ep
        return None, None, None

    def watch_offers(self, secs):
        """统计 OfferService 重发周期（TTL/2 心跳）"""
        t0 = time.time()
        times = []
        while time.time() - t0 < secs:
            r, _, _ = select.select([self.sd], [], [], 0.2)
            if not r:
                continue
            data, addr = self.sd.recvfrom(2048)
            hdr = dec_header(data)
            if hdr is None or hdr["service"] != 0xFFFF:
                continue
            sd = dec_sd(hdr["payload"])
            if not sd:
                continue
            for e in sd["entries"]:
                if e["type"] == 0x01 and e["service"] == SERVICE_ID:
                    times.append(time.time())
        gaps = [(times[i + 1] - times[i]) * 1000.0 for i in range(len(times) - 1)]
        return times, gaps

    def subscribe(self, ttl=3, timeout=1.0, eventgroup=EVENTGROUP, unsubscribe=False):
        """SubscribeEventgroup / StopSubscribeEventgroup，等 Ack"""
        t = 0 if unsubscribe else ttl
        eig = enc_sd_entry_eventgroup(0x06, SERVICE_ID, INSTANCE_ID, MAJOR_VERSION, t, eventgroup,
                                      num_options=1)
        opt = enc_sd_option_ipv4(self.local_ip, self.event_port)
        pkt = enc_sd(self.next_session(), [eig], [opt])
        self.sd.sendto(pkt, (self.board_ip, SD_PORT))
        self.log("SubscribeEventgroup eg=0x%04X ttl=%d -> %s:%d（事件端点 %s:%d）" %
                 (eventgroup, t, self.board_ip, SD_PORT, self.local_ip, self.event_port))
        deadline = time.time() + timeout
        while time.time() < deadline:
            r, _, _ = select.select([self.sd], [], [], max(0.0, deadline - time.time()))
            if not r:
                continue
            data, addr = self.sd.recvfrom(2048)
            hdr = dec_header(data)
            if hdr is None or hdr["service"] != 0xFFFF:
                continue
            sd = dec_sd(hdr["payload"])
            if not sd:
                continue
            for e in sd["entries"]:
                if e["type"] in (0x07, 0x0E) and e.get("eventgroup") == eventgroup:
                    self.ack = dict(type=e["type"], ttl=e["ttl"], counter=e.get("counter"),
                                    from_addr=addr, t=time.time())
                    return self.ack
        return None

    # ---------------- 方法 RPC ----------------
    def _collect_tp(self, first_hdr, deadline):
        """收到第一个 TP 段后继续收，按 offset 重组；返回完整 payload"""
        chunks = {}
        order = []
        hdr = first_hdr
        while True:
            tp = struct.unpack("!I", hdr["payload"][:4])[0]
            off = (tp >> 4) * 16
            more = tp & 1
            chunks[off] = hdr["payload"][4:]
            order.append((off, len(hdr["payload"]) - 4, more))
            self.tp_segs += 1
            self.log("  TP seg: offset=%d len=%d more=%d" % (off, len(hdr["payload"]) - 4, more))
            if not more:
                break
            if time.time() > deadline:
                raise TimeoutError("TP 重组超时（已收 %d 段）" % len(chunks))
            data = self._recv_svc(deadline)
            hdr = dec_header(data)
            if hdr is None:
                raise ValueError("TP 段解析失败")
        out = bytearray()
        for off in sorted(chunks):
            if off != len(out):
                raise ValueError("TP 分段不连续: offset=%d, expected=%d（收到的段: %s）" %
                                 (off, len(out), order))
            out += chunks[off]
        self.tp_bytes += len(out)
        return bytes(out)

    def _recv_svc(self, deadline):
        r, _, _ = select.select([self.svc], [], [], max(0.0, deadline - time.time()))
        if not r:
            raise TimeoutError("等待服务响应超时")
        data, addr = self.svc.recvfrom(4096)
        return data

    def request(self, method, payload=b"", timeout=1.0, expect_tp=False):
        """发 REQUEST，等 RESPONSE/ERROR；返回 (rc, 响应载荷, RTT_ms)"""
        sess = self.next_session()
        pkt = enc_header(SERVICE_ID, method, self.client_id, sess, MT_REQUEST, RC_OK, payload)
        t0 = time.perf_counter()
        self.svc.sendto(pkt, (self.board_ip, SVC_PORT))
        deadline = time.time() + timeout
        while True:
            data = self._recv_svc(deadline)
            hdr = dec_header(data)
            if self.verbose:
                if hdr is None:
                    self.log("rx %d B: dec_header FAILED (%s)" % (len(data), hexdump(data, 24)))
                else:
                    self.log("rx %d B: svc=0x%04X mtd=0x%04X sess=%u mtype=0x%02X payload=%d B" %
                             (len(data), hdr["service"], hdr["method"], hdr["session"],
                              hdr["mtype"], len(hdr["payload"])))
            if hdr is None:
                continue
            if hdr["service"] != SERVICE_ID or hdr["method"] != method:
                continue
            if hdr["session"] != sess:
                continue
            base = hdr["mtype"] & ~TP_FLAG
            if base not in (MT_RESPONSE, MT_ERROR):
                continue
            if hdr["mtype"] & TP_FLAG:
                body = self._collect_tp(hdr, deadline)
            else:
                body = hdr["payload"]
            rtt = (time.perf_counter() - t0) * 1000.0
            return hdr["rc"], body, rtt

    def request_tp(self, method, payload, timeout=2.0):
        """把 payload 用 SOME/IP-TP 分段发出去（验证板端重组）"""
        sess = self.next_session()
        t0 = time.perf_counter()
        segs = 0
        off = 0
        while off < len(payload):
            chunk = payload[off:off + TP_SEG]
            more = 1 if off + len(chunk) < len(payload) else 0
            tp_hdr = struct.pack("!I", ((off // 16) << 4) | more)
            pkt = enc_header(SERVICE_ID, method, self.client_id, sess, MT_REQUEST | TP_FLAG,
                            RC_OK, tp_hdr + chunk)
            self.svc.sendto(pkt, (self.board_ip, SVC_PORT))
            segs += 1
            off += len(chunk)
        deadline = time.time() + timeout
        while True:
            data = self._recv_svc(deadline)
            hdr = dec_header(data)
            if hdr is None or hdr["session"] != sess:
                continue
            base = hdr["mtype"] & ~TP_FLAG
            if base not in (MT_RESPONSE, MT_ERROR):
                continue
            if hdr["mtype"] & TP_FLAG:
                body = self._collect_tp(hdr, deadline)
            else:
                body = hdr["payload"]
            return hdr["rc"], body, (time.perf_counter() - t0) * 1000.0, segs
    # ---------------- 时基对齐（NTP 式） ----------------
    def host_us(self):
        return time.perf_counter() * 1e6

    def sync_clock(self, samples=9):
        """用 GET_TIME 方法做 NTP 式单程偏移估计：offset = board_us - host_us
        取 RTT 最小的一次（NTP 惯例，排队抖动最小）"""
        best = None
        for _ in range(samples):
            t0 = self.host_us()
            rc, body, rtt = self.request(M_GET_TIME, b"", timeout=1.0)
            t3 = self.host_us()
            if rc != RC_OK or len(body) < 4:
                continue
            board_us = struct.unpack("!I", body[:4])[0]
            if best is None or rtt < best["rtt"]:
                best = dict(offset=board_us - (t0 + t3) / 2.0, rtt=rtt, board_us=board_us)
        return best

    # ---------------- 事件统计 ----------------
    def collect_events(self, secs, ready=None):
        recs = []
        t_end = time.time() + secs
        while time.time() < t_end:
            r, _, _ = select.select([self.svc], [], [], 0.05)
            if not r:
                continue
            data, addr = self.svc.recvfrom(4096)
            hdr = dec_header(data)
            if hdr is None or hdr["service"] != SERVICE_ID:
                continue
            if (hdr["mtype"] & ~TP_FLAG) != MT_NOTIFICATION:
                continue
            recs.append(dict(host_us=self.host_us(), event=hdr["method"],
                             payload=hdr["payload"], session=hdr["session"], addr=addr))
            if ready is not None:
                ready(recs)
        return recs


PERIOD_MS = {E_COUNTER: 100.0, E_ADC: 10.0, E_TEMP: 500.0}
EVENT_NAMES = {E_COUNTER: "counter", E_ADC: "adc", E_TEMP: "temperature"}


def event_stats(recs, offset_us):
    """按事件号分组统计：周期均值/抖动(σ)/最小最大、丢包、单向时延"""
    groups = {}
    for r in recs:
        groups.setdefault(r["event"], []).append(r)
    out = []
    for ev, rs in sorted(groups.items()):
        ts = [r["host_us"] for r in rs]
        span = (ts[-1] - ts[0]) / 1e6
        gaps_ms = [(ts[i + 1] - ts[i]) / 1000.0 for i in range(len(ts) - 1)]
        mean = sum(gaps_ms) / len(gaps_ms) if gaps_ms else 0.0
        var = sum((g - mean) ** 2 for g in gaps_ms) / len(gaps_ms) if gaps_ms else 0.0
        # 丢包：counter 事件载荷 = u32 seq + u32 板端时刻(us)
        lost = 0
        if ev == E_COUNTER and len(rs) >= 2:
            seqs = [struct.unpack("!I", r["payload"][:4])[0] for r in rs if len(r["payload"]) >= 4]
            if seqs:
                lost = (seqs[-1] - seqs[0] + 1) - len(seqs)
        # 单向时延（若有板端时间戳）
        lat_ms = []
        if len(rs[0]["payload"]) >= 8 and offset_us is not None:
            for r in rs:
                board_us = struct.unpack("!I", r["payload"][4:8])[0]
                # offset_us = 板端_us - 主机_us（同步时测得）；事件发送时刻的"主机等价时间" = board_us - offset_us
                lat_ms.append((r["host_us"] - (board_us - offset_us)) / 1000.0)
        out.append(dict(event=ev, name=EVENT_NAMES.get(ev, "0x%04X" % ev), count=len(rs),
                        span=span, mean=mean, sigma=var ** 0.5,
                        min=min(gaps_ms) if gaps_ms else 0.0, max=max(gaps_ms) if gaps_ms else 0.0,
                        lost=lost, lat_min=min(lat_ms) if lat_ms else None,
                        lat_mean=(sum(lat_ms) / len(lat_ms)) if lat_ms else None,
                        lat_max=max(lat_ms) if lat_ms else None))
    return out


# ---------------------------------------------------------------- 命令行子命令
def cmd_discover(c, args):
    print("== SD 发现 ==")
    ok = True
    t, e, ep = c.wait_offer(args.timeout)
    if t is None:
        print("  FAIL 等待组播 OfferService 超时（板端是否在跑 someip_* 例程？网线/网卡？）")
        return False
    print("  PASS 收到组播 OfferService：service 0x%04X instance 0x%04X major %d ttl %d s" %
          (e["service"], e["instance"], e["major"], e["ttl"]))
    if ep:
        print("       endpoint option: %s:%d proto 0x%02X（UDP=0x11）" % (ep["ip"], ep["port"], ep["proto"]))
        ok = ok and ep["ip"] == c.board_ip and ep["port"] == SVC_PORT and ep["proto"] == 0x11
    t_before_find = time.time()
    t_find, e2, ep2, addr = c.find_service(args.timeout)
    if t_find is None:
        print("  FAIL FindService 未得到 OfferService 响应")
        return False
    print("  PASS FindService -> OfferService %.1f ms（单播回 %s:%d）" %
          ((t_find - t_before_find) * 1000.0, addr[0], addr[1]))
    times, gaps = c.watch_offers(args.watch)
    if gaps:
        print("  PASS 周期 OfferService: %d 条 / %.1f s，重发间隔 均值 %.1f ms（min %.1f / max %.1f）" %
              (len(times), args.watch, sum(gaps) / len(gaps), min(gaps), max(gaps)))
        ok = ok and abs(sum(gaps) / len(gaps) - 1500.0) < 400.0
    else:
        print("  FAIL 观察窗内没有周期 OfferService")
        ok = False
    print("== SD 发现 %s ==" % ("PASS" if ok else "FAIL"))
    return ok


def cmd_rpc(c, args):
    print("== 方法 RPC ==")
    results = []

    def check(name, cond, detail=""):
        results.append(cond)
        print("  %s %-28s %s" % ("PASS" if cond else "FAIL", name, detail))

    rc, body, rtt = c.request(M_ADD, struct.pack("!II", 2, 3))
    check("add(2,3)", rc == RC_OK and len(body) == 4 and struct.unpack("!I", body)[0] == 5,
          "rc=%s value=%s rtt=%.2f ms" % (RC_NAMES.get(rc), struct.unpack("!I", body)[0] if len(body) == 4 else "?",
                                          rtt))
    rc, body, rtt = c.request(M_ADD, struct.pack("!II", 0xFFFFFFFF, 1))
    check("add(0xFFFFFFFF,1)", rc == RC_OK and struct.unpack("!I", body)[0] == 0,
          "value=%s（u32 回绕）" % (struct.unpack("!I", body)[0] if len(body) == 4 else "?"))
    rc, body, rtt = c.request(M_GET_VERSION)
    ver = body[4:].decode("ascii", "replace") if len(body) >= 4 else ""
    check("get_version", rc == RC_OK and len(body) >= 4 and
          struct.unpack("!I", body[:4])[0] == len(body) - 4, "'%s'" % ver)
    rc, body, rtt = c.request(M_SET_LED, bytes([1]))
    check("set_led(1)", rc == RC_OK and len(body) >= 1 and body[0] == 1, "")
    rc, body, rtt = c.request(M_SET_LED, bytes([0]))
    check("set_led(0)", rc == RC_OK and len(body) >= 1 and body[0] == 0, "")
    rc, body, rtt = c.request(M_TEMP_GET)
    t0 = struct.unpack("!f", body[:4])[0] if len(body) >= 4 else float("nan")
    check("temp getter", rc == RC_OK and len(body) == 4 and 15.0 < t0 < 60.0, "%.3f C" % t0)
    rc, body, rtt = c.request(M_TEMP_SET, struct.pack("!f", 42.25))
    t1 = struct.unpack("!f", body[:4])[0] if len(body) >= 4 else float("nan")
    check("temp setter", rc == RC_OK and abs(t1 - 42.25) < 1e-3, "readback %.3f C" % t1)
    rc, body, rtt = c.request(M_GET_TIME)
    check("get_time(us)", rc == RC_OK and len(body) == 4, "board_us=%s" %
          (struct.unpack("!I", body)[0] if len(body) == 4 else "?"))
    rc, body, rtt = c.request(0x00FF)
    check("unknown method -> ERROR(0x03)", rc == 0x03, "rc=%s" % RC_NAMES.get(rc))
    rc, body, rtt = c.request(M_ADD, struct.pack("!II", 2, 3))
    check("response session 回显", rc == RC_OK, "（见下：RTT 统计）")

    # RTT 统计
    rtts = []
    for _ in range(args.count):
        rc, body, rtt = c.request(M_ADD, struct.pack("!II", 1, 2))
        if rc == RC_OK:
            rtts.append(rtt)
    rtts.sort()
    if rtts:
        avg = sum(rtts) / len(rtts)
        print("  RTT n=%d min=%.3f avg=%.3f p50=%.3f p99=%.3f max=%.3f ms" %
              (len(rtts), rtts[0], avg, rtts[len(rtts) // 2], rtts[int(len(rtts) * 0.99)], rtts[-1]))
    print("== 方法 RPC %s (%d/%d) ==" % ("PASS" if all(results) else "FAIL",
                                       sum(1 for r in results if r), len(results)))
    return all(results), rtts


def cmd_event(c, args):
    print("== 事件订阅 ==")
    # 订阅前带宽（应该只有 SD，无 NOTIFICATION）
    t0 = time.time()
    pre_bytes = 0
    pre_notif = 0
    while time.time() - t0 < 1.0:
        r, _, _ = select.select([c.svc], [], [], 0.1)
        if not r:
            continue
        data, _ = c.svc.recvfrom(4096)
        pre_bytes += len(data)
        h = dec_header(data)
        if h and (h["mtype"] & ~TP_FLAG) == MT_NOTIFICATION:
            pre_notif += 1
    print("  订阅前 1 s：收到 %d B，NOTIFICATION %d 条（应为 0）" % (pre_bytes, pre_notif))

    ack = c.subscribe(ttl=args.ttl)
    ok = ack is not None and ack["type"] == 0x07
    kind = "Ack" if (ack and ack["type"] == 0x07) else ("Nack" if ack else "无响应")
    print("  %s SubscribeEventgroup -> %s ttl=%s counter=%s" %
          ("PASS" if ok else "FAIL", kind, ack["ttl"] if ack else "?", ack["counter"] if ack else "?"))
    if not ok:
        print("== 事件订阅 FAIL ==")
        return False

    sync = c.sync_clock()
    if sync:
        print("  时基对齐：offset=%.3f ms（最优样本 RTT %.3f ms）" %
              (sync["offset"] / 1000.0, sync["rtt"]))

    t_start = time.time()
    recs = c.collect_events(args.secs)
    span = time.time() - t_start
    total_bytes = sum(len(r["payload"]) + 16 for r in recs)
    bps = total_bytes * 8 / span if span > 0 else 0
    stats = event_stats(recs, sync["offset"] if sync else None)

    print("  收到 NOTIFICATION %d 条 / %.2f s，事件流量 %.1f kbit/s（订阅前 %.1f kbit/s）" %
          (len(recs), span, bps / 1000.0, pre_bytes * 8 / 1.0 / 1000.0))
    print("  %-12s %6s %8s %9s %9s %9s %6s %10s" %
          ("事件", "条数", "周期ms", "σms", "minms", "maxms", "丢包", "时延ms"))
    for s in stats:
        lat = "%.2f/%.2f" % (s["lat_mean"], s["lat_max"]) if s["lat_mean"] is not None else "-"
        print("  %-12s %6d %8.2f %9.3f %9.2f %9.2f %6d %10s" %
              (s["name"], s["count"], s["mean"], s["sigma"], s["min"], s["max"], s["lost"], lat))

    ok = len(stats) > 0
    if not ok:
        print("  FAIL 6 s 内一条 NOTIFICATION 都没收到（Ack 到了但事件没到：检查事件端点/端口、订阅表）")
    for s in stats:
        exp = PERIOD_MS.get(s["event"])
        if exp is None:
            continue
        if abs(s["mean"] - exp) > max(1.0, exp * 0.1):
            print("  FAIL %s 周期 %.2f ms 偏离期望 %.1f ms 超过 10%%" % (s["name"], s["mean"], exp))
            ok = False
        if s["lost"] != 0:
            print("  FAIL %s 丢包 %d 条" % (s["name"], s["lost"]))
            ok = False
    print("  Wextra: 期望周期 counter=100 ms / adc=10 ms / temperature=500 ms")
    c.subscribe(unsubscribe=True)
    print("== 事件订阅 %s ==" % ("PASS" if ok else "FAIL"))
    return ok, stats


def cmd_tp(c, args):
    print("== SOME/IP-TP ==")
    size = args.size
    ok = True
    # 1) 板 -> 主机：GET_BULK 响应必须分段，主机重组后校验图案 + CRC
    rc, body, rtt = c.request(M_GET_BULK, struct.pack("!I", size), timeout=3.0)
    expect = bytes(((i * 31 + 7) & 0xFF) for i in range(size))
    good = (rc == RC_OK) and (body == expect)
    print("  %s 板->主机 %d B：收到 %d B，%d 段，RTT %.2f ms，CRC32 0x%08X %s" %
          ("PASS" if good else "FAIL", size, len(body), c.tp_segs, rtt,
           zlib.crc32(body) & 0xFFFFFFFF, "一致" if good else "不一致"))
    ok = ok and good
    # 2) 主机 -> 板：4096 B 请求走 TP，板端重组后返回 CRC32
    payload = bytes(((i * 13 + 5) & 0xFF) for i in range(size))
    crc_local = zlib.crc32(payload) & 0xFFFFFFFF
    rc, body, rtt, segs = c.request_tp(M_CRC32, payload, timeout=3.0)
    crc_board = struct.unpack("!I", body[:4])[0] if len(body) >= 4 else 0
    good = (rc == RC_OK) and (crc_board == crc_local)
    print("  %s 主机->板 %d B（%d 段）：板端重组 CRC32 0x%08X vs 本地 0x%08X %s，RTT %.2f ms" %
          ("PASS" if good else "FAIL", size, segs, crc_board, crc_local,
           "一致" if good else "不一致", rtt))
    ok = ok and good
    print("== SOME/IP-TP %s ==" % ("PASS" if ok else "FAIL"))
    return ok

# ---------------------------------------------------------------- main
def build_parser():
    ap = argparse.ArgumentParser(
        description="SOME/IP 客户端：自实现 SOME/IP-SD + 方法 RPC + 事件订阅 + SOME/IP-TP",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="示例：\n"
               "  python3 someip_client.py --board 192.168.2.100 discover\n"
               "  python3 someip_client.py --board 192.168.2.100 rpc --count 200\n"
               "  python3 someip_client.py --board 192.168.2.100 event --secs 10\n"
               "  python3 someip_client.py --board 192.168.2.100 tp --size 4096\n"
               "  python3 someip_client.py --board 192.168.2.100 all --secs 10")
    ap.add_argument("--board", default="192.168.2.100", help="板端 IP（默认 192.168.2.100）")
    ap.add_argument("--local", default="192.168.2.1", help="主机接板网卡 IP（默认 192.168.2.1）")
    ap.add_argument("--port", type=int, default=30502, help="主机事件接收端口（默认 30502）")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印报文级调试信息")
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("discover", help="SOME/IP-SD 发现（组播 Offer + FindService）")
    p.add_argument("--timeout", type=float, default=3.0)
    p.add_argument("--watch", type=float, default=6.0, help="观察 OfferService 周期的秒数")

    p = sub.add_parser("rpc", help="方法 RPC + RTT 统计")
    p.add_argument("--count", type=int, default=200, help="RTT 采样次数")

    p = sub.add_parser("event", help="事件订阅 + 周期/抖动/丢包/时延统计")
    p.add_argument("--secs", type=float, default=10.0)
    p.add_argument("--ttl", type=int, default=3)

    p = sub.add_parser("tp", help="SOME/IP-TP 4 KB 分段/重组 + CRC 校验")
    p.add_argument("--size", type=int, default=4096)

    p = sub.add_parser("core", help="discover + rpc + event（不含 TP，配 someip_rpc 例程）")
    p.add_argument("--secs", type=float, default=10.0)
    p.add_argument("--count", type=int, default=200)
    p.add_argument("--timeout", type=float, default=3.0)
    p.add_argument("--watch", type=float, default=5.0)
    p.add_argument("--ttl", type=int, default=3)

    p = sub.add_parser("all", help="依次跑 discover/rpc/event/tp")
    p.add_argument("--secs", type=float, default=10.0)
    p.add_argument("--count", type=int, default=200)
    p.add_argument("--size", type=int, default=4096)
    p.add_argument("--timeout", type=float, default=3.0)
    p.add_argument("--watch", type=float, default=5.0)
    p.add_argument("--ttl", type=int, default=3)
    return ap


def main(argv=None):
    ap = build_parser()
    args = ap.parse_args(argv)
    if not args.cmd:
        args.cmd = "all"
        args.secs = 10.0
        args.count = 200
        args.size = 4096
        args.timeout = 3.0
        args.watch = 5.0
        args.ttl = 3

    print("someip_client: board=%s local=%s event_port=%d" % (args.board, args.local, args.port))
    c = SomeIpClient(board_ip=args.board, local_ip=args.local,
                     event_port=args.port, verbose=args.verbose)
    summary = []
    try:
        if args.cmd == "discover":
            summary.append(("discover", cmd_discover(c, args)))
        elif args.cmd == "core":
            summary.append(("discover", cmd_discover(c, args)))
            print("")
            ok, _ = cmd_rpc(c, args)
            summary.append(("rpc", ok))
            print("")
            ok, _ = cmd_event(c, args)
            summary.append(("event", ok))
        elif args.cmd == "rpc":
            ok, _ = cmd_rpc(c, args)
            summary.append(("rpc", ok))
        elif args.cmd == "event":
            ok, _ = cmd_event(c, args)
            summary.append(("event", ok))
        elif args.cmd == "tp":
            summary.append(("tp", cmd_tp(c, args)))
        else:
            summary.append(("discover", cmd_discover(c, args)))
            print("")
            ok, _ = cmd_rpc(c, args)
            summary.append(("rpc", ok))
            print("")
            ok, _ = cmd_event(c, args)
            summary.append(("event", ok))
            print("")
            summary.append(("tp", cmd_tp(c, args)))
    finally:
        c.close()

    print("")
    print("================ 汇总 ================")
    for name, ok in summary:
        print("  %-10s %s" % (name, "PASS" if ok else "FAIL"))
    allok = all(ok for _, ok in summary)
    print("  总计: %s" % ("PASS" if allok else "FAIL"))
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
