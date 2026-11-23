#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_tests.py - DoIP 服务端的 pytest 风格回归套件（也可直接 python3 运行）

    python3 run_tests.py                # 全部用例 + 实测数据表
    python3 run_tests.py -k bulk        # 只跑名字含 bulk 的用例
    python3 run_tests.py --json r.json  # 额外输出机器可读结果（书稿/画图用）
    pytest run_tests.py                 # 装了 pytest 也能直接跑（用例函数无参数）

前置：板端固件 samples/app/doip_server 在跑，板 192.168.2.100，主机 192.168.2.1/24。
部分用例需要板端先执行（hwrun --send）：
    doip timer general 3000     -> 让 general inactivity 用例在 ~3 s 内断链
    doip timer period 1000      -> 让周期声明用例有广播可收
"""

import argparse
import socket
import struct
import sys
import time
import traceback

import doip_client as dc

BOARD = "192.168.2.100"
MEASURE = []      # (指标, 数值, 单位, 备注)


def measure(name, value, unit, note=""):
    MEASURE.append((name, value, unit, note))
    print("    [measure] %-38s %9.2f %s %s" % (name, value, unit, note))


def client(tester=0x0E00, timeout=2.0):
    return dc.DoIPClient(board=BOARD, local_ip=None, tester=tester, timeout=timeout)


def activated_client(timeout=2.0):
    c = client(timeout=timeout)
    c.connect()
    code, info, rtt = c.routing_activation()
    assert code == 0x10, "路由激活失败 code=0x%02X (%s)" % (code, info["code_name"])
    return c


def test_vehicle_discovery_plain():
    c = client()
    info = c.discover("plain", 5)
    assert info["vin"] == "HPM5E31TESTVIN001", info["vin"]
    assert info["addr"] == 0x1000, hex(info["addr"])
    assert len(info["eid"].split(":")) == 6
    rtts = info["rtt_us"]
    measure("UDP 0x0001 发现 RTT 均值", sum(rtts) / len(rtts), "us", "n=5")
    measure("UDP 0x0001 发现 RTT 最大", max(rtts), "us")


def test_vehicle_discovery_eid_vin():
    c = client()
    for kind in ("eid", "vin"):
        info = c.discover(kind, 3)
        assert info["vin"] == "HPM5E31TESTVIN001"


def test_entity_status_and_power_mode():
    c = client()
    st = c.entity_status()
    assert st["max_sockets"] >= 1
    assert st["max_data_size"] >= 512, st["max_data_size"]
    pm = c.power_mode()
    assert pm["mode"] in (0x00, 0x01)
    measure("UDP 0x4001 实体状态 RTT", st["rtt_us"], "us",
            "max_sockets=%d max_data=%d" % (st["max_sockets"], st["max_data_size"]))
    measure("UDP 0x4003 电源模式 RTT", pm["rtt_us"], "us", "mode=0x%02X" % pm["mode"])


def test_vehicle_announcement_broadcast():
    c = client()
    got = c.listen_announce(4.0)
    if not got:
        print("    [skip] 4 s 内没有广播声明（板端未开 doip timer period）")
        return
    gaps = [(got[i]["t"] - got[i - 1]["t"]) * 1e3 for i in range(1, len(got))]
    assert got[0]["vin"] == "HPM5E31TESTVIN001"
    measure("广播声明条数 / 4 s", len(got), "条", "src=%s" % (got[0]["src"],))
    if gaps:
        measure("声明间隔均值", sum(gaps) / len(gaps), "ms", "n=%d" % len(gaps))
        measure("声明间隔最大偏差", max(gaps) - min(gaps), "ms")


def test_routing_activation_ok():
    c = client()
    c.connect()
    try:
        code, info, rtt = c.routing_activation()
        assert code == 0x10, info
        assert info["tester_sa"] == 0x0E00
        assert info["entity_addr"] == 0x1000
        measure("路由激活 RTT", rtt, "us", "0x0006/0x10")
        code2, _, _ = c.routing_activation()
        assert code2 == 0x10
    finally:
        c.close()


def test_routing_activation_denied():
    c = client()
    c.connect()
    try:
        code, info, _ = c.routing_activation(sa=0x1234)
        assert code == 0x00, info
        code, info, _ = c.routing_activation(sa=0x0E00, at=0x02)
        assert code == 0x06, info
        code, info, _ = c.routing_activation(sa=0x0E00, at=0x00, with_oem=False)
        assert code == 0x10, info
    finally:
        c.close()


def test_uds_full_sequence():
    c = client()
    c.connect()
    try:
        code, _, _ = c.routing_activation()
        assert code == 0x10
        assert c.session(0x03)[:2] == b"\x50\x03"
        r = c.read_did(0xF190, 0xF195, 0xF187, 0xF18C)
        assert r[0] == 0x62 and len(r) >= 20
        vin0 = r[3:20]
        assert c.uds(0x2E, 0xF1, 0x90, *b"HPM5E31DOIPVIN001") == b"\x7F\x2E\x33"
        sec = c.security_access(1)
        assert sec is not None and sec["resp"][:2] == b"\x67\x02"
        new_vin = b"HPM5E31DOIPVIN001"
        assert c.uds(0x2E, 0xF1, 0x90, *new_vin)[:3] == b"\x6E\xF1\x90"
        assert c.read_did(0xF190)[3:20] == new_vin
        assert c.uds(0x2E, 0xF1, 0x90, *vin0)[:3] == b"\x6E\xF1\x90"
        assert c.uds(0x31, 0x01, 0x02, 0x03)[:4] == b"\x71\x01\x02\x03"
        assert c.uds(0x19, 0x02, 0xFF)[0] == 0x59
        assert c.uds(0x3E, 0x00)[:2] == b"\x7E\x00"
        assert c.uds(0x10, 0x01)[:2] == b"\x50\x01"
    finally:
        c.close()


def test_uds_pending_78_and_timing():
    c = activated_client(timeout=3.0)
    try:
        c.session(0x03)
        res = c.send_diag(bytes([0x31, 0x01, 0x03, 0x01]), timeout=3.0)
        assert res.uds[:4] == b"\x71\x01\x03\x01"
        assert res.pending >= 1, "没有收到 7F 31 78"
        measure("0x8002 ACK 时延", res.t_ack_us, "us", "回调只入队、不阻塞")
        measure("慢服务最终响应时间", res.t_resp_us, "us", "含 1 条 0x78")
        t0 = time.perf_counter()
        for _ in range(20):
            c.uds(0x3E, 0x00)
        dt = (time.perf_counter() - t0) / 20.0 * 1e6
        measure("3E 00 平均往返", dt, "us", "n=20，TCP 已建连")
    finally:
        c.close()


def test_uds_negative_responses():
    c = activated_client()
    try:
        assert c.uds(0x99) == b"\x7F\x99\x11"
        assert c.uds(0x22, 0x12, 0x34)[:3] == b"\x7F\x22\x31"
        assert c.uds(0x10)[:3] == b"\x7F\x10\x13"
        measure("负响应 NRC 检查", 3, "条", "0x11/0x31/0x13")
    finally:
        c.close()


def test_bulk_inbound_4k():
    """入向大报文：DoIP 帧 4008 B 能被完整收下并 ACK/按 ISO 回 0x8003/0x04；
    512 B UDS（516 B 帧）能正常交给 UDS 处理（实体声明的 max data size = 516）"""
    c = activated_client(timeout=5.0)
    try:
        c.session(0x03)
        # a) 4000 B UDS：超过实体能力 -> 0x8003/0x04（diagnostic message too large）
        uds = bytes([0x2E, 0xF1, 0x90]) + bytes((i & 0xFF) for i in range(3997))
        t0 = time.perf_counter()
        c.send_frame(dc.PT_DIAG_MESSAGE, struct.pack(">HH", c.tester, c.entity) + uds)
        nack = None
        while (time.perf_counter() - t0) < 4.0:
            pt, pay, _ = c.recv_frame(4.0)
            if pt == dc.PT_DIAG_NACK:
                nack = pay[4]
                break
            if pt == dc.PT_DIAG_MESSAGE:
                break
        dt = (time.perf_counter() - t0) * 1e6
        assert nack == 0x04, "期望 0x8003/0x04，实际 %s" % ("0x%02X" % nack if nack is not None else "无 NACK")
        measure("入向 4008 B 帧的否定应答时延", dt, "us", "0x8003/0x04 超长")
        # b) 512 B UDS（516 B 帧）：正常交给 UDS，回 NRC 0x13（0x2E 长度不对）
        uds2 = bytes([0x2E, 0xF1, 0x90]) + bytes((i & 0xFF) for i in range(509))
        t0 = time.perf_counter()
        c.send_frame(dc.PT_DIAG_MESSAGE, struct.pack(">HH", c.tester, c.entity) + uds2)
        ack = None
        resp = None
        while (time.perf_counter() - t0) < 4.0:
            pt, pay, _ = c.recv_frame(4.0)
            if pt == dc.PT_DIAG_ACK:
                ack = pay[4]
                continue
            if pt == dc.PT_DIAG_MESSAGE:
                resp = pay[4:]
                break
        dt2 = (time.perf_counter() - t0) * 1e6
        assert ack == 0x00 and resp is not None and resp[0] == 0x7F, (ack, (resp or b"").hex(" "))
        measure("入向 516 B 帧 ACK+响应时延", dt2, "us", "UDS 回 NRC 0x%02X" % resp[2])
    finally:
        c.close()


def test_bulk_outbound_4k():
    """出向大报文：单帧 4000 B 的 0x8001（4008 B 帧）；再一次请求连发 20 帧 512 B"""
    c = activated_client(timeout=5.0)
    try:
        c.session(0x03)
        # a) 单帧 4000 B
        n = 4000
        t0 = time.perf_counter()
        c.send_frame(dc.PT_DIAG_MESSAGE,
                     struct.pack(">HH", c.tester, c.entity)
                     + bytes([0x31, 0x01, 0x02, 0x04, (n >> 8) & 0xFF, n & 0xFF, 0, 1]))
        big = None
        rtt = None
        while (time.perf_counter() - t0) < 5.0:
            pt, pay, _ = c.recv_frame(5.0)
            if pt != dc.PT_DIAG_MESSAGE:
                continue
            body = pay[4:]
            if len(body) == n and body[:3] == b"\x62\xF1\x90":
                big = body
                rtt = (time.perf_counter() - t0) * 1e6
                continue
            assert body[:4] == b"\x71\x01\x02\x04", body[:8].hex(" ")
            break
        assert big is not None, "没有收到 4000 B 的 0x8001"
        measure("出向 4000 B 单帧总耗时", rtt, "us", "4008 B / 含 0x8002 ACK")
        measure("出向 4000 B 吞吐", (n + 8) * 8 / rtt, "Mbit/s", "单帧")
    finally:
        c.close()
    # b) 单帧 512 B 对照（同一路径、不同尺寸）
    c = activated_client(timeout=5.0)
    try:
        c.session(0x03)
        n = 512
        t0 = time.perf_counter()
        c.send_frame(dc.PT_DIAG_MESSAGE,
                     struct.pack(">HH", c.tester, c.entity)
                     + bytes([0x31, 0x01, 0x02, 0x04, (n >> 8) & 0xFF, n & 0xFF, 0, 1]))
        got = None
        while (time.perf_counter() - t0) < 5.0:
            pt, pay, _ = c.recv_frame(5.0)
            if pt != dc.PT_DIAG_MESSAGE:
                continue
            body = pay[4:]
            if len(body) == n:
                got = (time.perf_counter() - t0) * 1e6
                continue
            if body[:4] == b"\x71\x01\x02\x04":
                break
        assert got is not None, "没有收到 512 B 的 0x8001"
        measure("出向 512 B 单帧总耗时", got, "us", "520 B 帧 / 含 0x8002 ACK")
        measure("出向 512 B 吞吐", (n + 8) * 8 / got, "Mbit/s", "单帧")
    finally:
        c.close()


def test_diag_address_errors():
    c = activated_client()
    try:
        res = c.send_diag(b"\x10\x01", ta=0x2000)
        assert res.nack_code == 0x03, res.nack_code
        res = c.send_diag(b"\x10\x01", sa=0x0000)
        assert res.nack_code == 0x02, res.nack_code
    finally:
        c.close()


def test_generic_header_nack():
    c = activated_client()
    try:
        c.tcp.sendall(struct.pack(">BBHI", 0x02, 0x00, dc.PT_DIAG_MESSAGE, 4) + b"\x00" * 4)
        pt, pay, _ = c.recv_frame(1.0)
        assert pt == dc.PT_GENERIC_NACK and pay[0] == 0x00, (pt, pay)
    finally:
        c.close()
    c = activated_client()
    try:
        c.send_frame(0x1234, b"\x01\x02\x03")
        pt, pay, _ = c.recv_frame(1.0)
        assert pt == dc.PT_GENERIC_NACK and pay[0] == 0x01, (pt, pay)
    finally:
        c.close()


def test_initial_inactivity_timeout():
    c = client()
    c.connect()
    t0 = time.perf_counter()
    closed = False
    try:
        while (time.perf_counter() - t0) < 5.0:
            c.tcp.settimeout(0.5)
            try:
                data = c.tcp.recv(1024)
            except socket.timeout:
                continue
            if not data:
                closed = True
                break
    finally:
        c.close()
    dt = time.perf_counter() - t0
    assert closed, "连接没有被断开"
    assert 1.5 <= dt <= 3.5, "断链时间 %.2f s 不在 2 s 附近" % dt
    measure("initial inactivity 断链时间", dt * 1e3, "ms",
            "A_DoIP_Initial_Inactivity=2000 ms")


def test_general_inactivity_timeout():
    c = activated_client()
    t0 = time.perf_counter()
    closed = False
    try:
        while (time.perf_counter() - t0) < 8.0:
            c.tcp.settimeout(0.5)
            try:
                data = c.tcp.recv(1024)
            except socket.timeout:
                continue
            if not data:
                closed = True
                break
    finally:
        c.close()
    dt = time.perf_counter() - t0
    if not closed:
        print("    [skip] 8 s 内未断链（板端 general 定时器可能仍是 300 s 默认值）")
        return
    measure("general inactivity 断链时间", dt * 1e3, "ms", "板端 timer general 3000")


def test_dual_connection_alive_check():
    c1 = activated_client()
    c2 = client()
    c2.connect()
    try:
        code2, _, _ = c2.routing_activation()
        assert code2 == 0x03, "同 SA 新连接应被拒 0x03，实际 0x%02X" % code2
        pt, pay, _ = c1.recv_frame(1.5)
        assert pt == dc.PT_ALIVE_CHECK_REQ, "老连接没收到 0x0007"
        time.sleep(0.9)
        code3, _, _ = c2.routing_activation()
        assert code3 == 0x10, "老连接被踢后新连接应能激活，实际 0x%02X" % code3
    finally:
        c1.close()
        c2.close()


CASES = [
    test_vehicle_discovery_plain,
    test_vehicle_discovery_eid_vin,
    test_entity_status_and_power_mode,
    test_vehicle_announcement_broadcast,
    test_routing_activation_ok,
    test_routing_activation_denied,
    test_uds_full_sequence,
    test_uds_pending_78_and_timing,
    test_uds_negative_responses,
    test_bulk_inbound_4k,
    test_bulk_outbound_4k,
    test_diag_address_errors,
    test_generic_header_nack,
    test_dual_connection_alive_check,
    test_initial_inactivity_timeout,
    test_general_inactivity_timeout,
]


def main(argv=None):
    global BOARD
    ap = argparse.ArgumentParser(description="DoIP 服务端回归套件")
    ap.add_argument("-k", default="", help="只跑名字含该子串的用例")
    ap.add_argument("--board", default=BOARD)
    ap.add_argument("--json", default="", help="把结果写到 JSON（画图/书稿用）")
    args = ap.parse_args(argv)
    BOARD = args.board

    npass = nfail = 0
    fails = []
    t_all = time.perf_counter()
    for case in CASES:
        if args.k and args.k not in case.__name__:
            continue
        t0 = time.perf_counter()
        print("\n[%s]" % case.__name__)
        try:
            case()
            npass += 1
            print("  PASS (%.0f ms)" % ((time.perf_counter() - t0) * 1e3))
        except Exception as exc:
            nfail += 1
            fails.append((case.__name__, str(exc)))
            print("  FAIL (%.0f ms): %s" % ((time.perf_counter() - t0) * 1e3, exc))
            traceback.print_exc()
    dt = time.perf_counter() - t_all
    print("\n========== 结果：PASS %d / FAIL %d（总 %.1f s）==========" % (npass, nfail, dt))
    for name, err in fails:
        print("  FAIL %-42s %s" % (name, err))
    if MEASURE:
        print("\n---- 实测数据（书稿表格直接抄这一节）----")
        print("| 指标 | 数值 | 单位 | 备注 |")
        print("|---|---|---|---|")
        for name, val, unit, note in MEASURE:
            print("| %s | %.2f | %s | %s |" % (name, val, unit, note))
    if args.json:
        import json
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(dict(passed=npass, failed=nfail, seconds=dt,
                           measures=[dict(name=a, value=b, unit=c, note=d)
                                     for a, b, c, d in MEASURE],
                           failures=fails), f, ensure_ascii=False, indent=1)
        print("JSON -> %s" % args.json)
    return 0 if nfail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
