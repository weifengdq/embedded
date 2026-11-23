#!/usr/bin/env python3
"""uds_tester.py - UDS 诊断序列测试（上位机侧，0x7E0/0x7E8）

与固件 samples/app/uds_ecu 的板内 tester 跑同一套序列，用来做"主机侧实测"：
  10 03 -> 27 01/02 -> 22 F190/F195/F187/F18C -> 2E F190(未解锁应 NRC 0x33)
        -> 27 01/02 -> 2E F190(成功) -> 22 F190(回读) -> 31 01 0203
        -> 19 02 -> 14 FFFFFF -> 19 02(空) -> 3E 00 -> 10 01

实现：
  --impl raw       本仓库 common.py（不依赖第三方库）
  --impl udsoncan  第三方 udsoncan 库（对照，验证标准实现互通）

对象：Bench B 的真实 DUT（vcan0 经 CAN 网关），或 host/uds/ecu_sim.py 模拟器。
"""
import argparse
import struct
import sys
import time

import common


class Step:
    def __init__(self, name, req):
        self.name = name
        self.req = req
        self.resp = b""
        self.rtt = 0
        self.ok = False
        self.note = ""


def expect(step, ok, note=""):
    step.ok = ok
    step.note = note
    return step


def run_raw(cl, steps):
    for st in steps:
        try:
            resp, rtt, pend = cl.request(st.req)
            st.resp, st.rtt = resp, rtt
            st.note = ("pending×%d " % pend) if pend else ""
        except common.IsoTpError as e:
            st.resp, st.rtt, st.ok, st.note = b"", 0, False, "timeout: %s" % e


def main():
    ap = argparse.ArgumentParser(description="UDS 诊断序列测试")
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--impl", default="raw", choices=["raw", "udsoncan"])
    ap.add_argument("--req-id", default="0x7E0")
    ap.add_argument("--resp-id", default="0x7E8")
    ap.add_argument("--p2star", type=float, default=5.0, help="P2* 秒")
    args = ap.parse_args()
    req_id = int(args.req_id, 0)
    resp_id = int(args.resp_id, 0)
    new_vin = b"HPM5E31NEWVIN0002"

    if args.impl == "udsoncan":
        return run_udsoncan(args, req_id, resp_id, new_vin)

    bus = common.RawCan(args.channel)
    tp = common.IsoTp(bus, rx_id=resp_id, tx_id=req_id, n_bs=args.p2star, n_cr=args.p2star)
    cl = common.UdsClient(tp, p2star=args.p2star)

    t_start = time.perf_counter()
    steps = []
    # 1) 10 03
    s = Step("10 03 扩展会话", [0x10, 0x03])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x10) and len(s.resp) == 6,
                        "P2=%d ms P2*=%d ms" % (struct.unpack(">H", s.resp[2:4])[0] if len(s.resp) >= 4 else 0,
                                                (struct.unpack(">H", s.resp[4:6])[0] * 10) if len(s.resp) >= 6 else 0)))
    # 2) 22 4 个 DID
    s = Step("22 F190/F195/F187/F18C", [0x22, 0xF1, 0x90, 0xF1, 0x95, 0xF1, 0x87, 0xF1, 0x8C])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x22) and len(s.resp) > 60,
                        "%d B" % len(s.resp)))
    # 3) 2E F190 未解锁 -> NRC 0x33
    s = Step("2E F190 未解锁", [0x2E, 0xF1, 0x90] + list(new_vin))
    run_raw(cl, [s])
    steps.append(expect(s, common.nrc_of(s.resp) == 0x33, "NRC 0x33 securityAccessDenied"))
    # 4) 27 01/02
    s = Step("27 01/02 安全访问", [0x27, 0x01])
    t0 = time.perf_counter()
    try:
        seed_resp, rtt1, _ = cl.request([0x27, 0x01])
        seed = struct.unpack(">I", seed_resp[2:6])[0] if len(seed_resp) == 6 else 0
        key = common.security_key(seed, 1)
        key_resp, rtt2, _ = cl.request(bytes([0x27, 0x02]) + struct.pack(">I", key))
        s.resp = key_resp
        s.rtt = rtt1 + rtt2
        s.ok = common.is_positive(key_resp, 0x27)
        s.note = "seed=0x%08X key=0x%08X" % (seed, key)
    except common.IsoTpError as e:
        s.note = str(e)
    steps.append(s)
    # 5) 2E F190 已解锁
    s = Step("2E F190 已解锁", [0x2E, 0xF1, 0x90] + list(new_vin))
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x2E), ""))
    # 6) 22 F190 回读
    s = Step("22 F190 回读", [0x22, 0xF1, 0x90])
    run_raw(cl, [s])
    steps.append(expect(s, len(s.resp) == 20 and s.resp[3:] == new_vin, "写入的 VIN 已回读"))
    # 7) 31 01 0203
    s = Step("31 01 0203 自检例程", [0x31, 0x01, 0x02, 0x03])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x31), ""))
    # 8) 19 02
    s = Step("19 02 读 DTC", [0x19, 0x02, 0xFF])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x19), "%d B" % len(s.resp)))
    # 9) 14 FFFFFF
    s = Step("14 FFFFFF 清故障码", [0x14, 0xFF, 0xFF, 0xFF])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x14), ""))
    # 10) 19 02 再读 -> 空
    s = Step("19 02 清码后为空", [0x19, 0x02, 0xFF])
    run_raw(cl, [s])
    steps.append(expect(s, len(s.resp) == 3 and s.resp[0] == 0x59, "%d B" % len(s.resp)))
    # 11) 3E 00
    s = Step("3E 00 TesterPresent", [0x3E, 0x00])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x3E), ""))
    # 12) 10 01
    s = Step("10 01 回默认会话", [0x10, 0x01])
    run_raw(cl, [s])
    steps.append(expect(s, common.is_positive(s.resp, 0x10), ""))
    bus.close()

    print("== UDS 诊断序列（impl=raw, %s, 0x%03X/0x%03X）==" % (args.channel, req_id, resp_id))
    print("  %-22s %-28s %-10s %s" % ("步骤", "响应", "RTT µs", "结果"))
    for s in steps:
        print("  %-22s %-28s %-10d %s %s" % (s.name, common.describe(s.resp)[:28], s.rtt,
                                             "PASS" if s.ok else "FAIL", s.note))
    ok = sum(1 for s in steps if s.ok)
    dt = time.perf_counter() - t_start
    avg = sum(s.rtt for s in steps) / max(1, len(steps))
    print("[uds_tester] %s steps=%d/%d avg_rtt=%.0f us total=%.2f s"
          % ("PASS" if ok == len(steps) else "FAIL", ok, len(steps), avg, dt))
    return 0 if ok == len(steps) else 1


def _expect_nrc(client, fn, nrc):
    """期望调用抛出 NegativeResponseException 且 NRC 匹配；匹配则返回 'NRC 0x..' 视为 PASS。"""
    try:
        fn()
    except Exception as e:      # noqa: BLE001
        code = getattr(getattr(e, "response", None), "code", None)
        if code == nrc:
            return "NRC 0x%02X (预期)" % code
        raise
    raise RuntimeError("期望 NRC 0x%02X，但收到了正响应" % nrc)


def run_udsoncan(args, req_id, resp_id, new_vin):
    try:
        import can
        import udsoncan
        from udsoncan.client import Client
        from udsoncan.connections import PythonIsoTpConnection
        import isotp
    except ImportError as e:
        print("  [skip] udsoncan 依赖缺失: %s" % e)
        return 1
    bus = can.interface.Bus(channel=args.channel, interface="socketcan")
    addr = isotp.Address(isotp.AddressingMode.Normal_11bits, txid=req_id, rxid=resp_id)
    stack = isotp.CanStack(bus=bus, address=addr)   # CanStack 自带收发线程
    conn = PythonIsoTpConnection(stack)
    cfg = dict(udsoncan.configs.default_client_config)
    cfg["security_algo"] = lambda level, seed, params: bytes(
        struct.pack(">I", common.security_key(struct.unpack(">I", seed)[0], level)))
    cfg["security_algo_params"] = None
    cfg["data_identifiers"] = {
        0xF190: udsoncan.AsciiCodec(17),
        0xF195: udsoncan.AsciiCodec(12),
        0xF187: udsoncan.AsciiCodec(11),
        0xF18C: udsoncan.AsciiCodec(13),
        0xF191: udsoncan.AsciiCodec(15),
    }
    ok = 0
    total = 0
    rows = []
    with Client(conn, config=cfg) as c:
        def check(name, fn):
            nonlocal ok, total
            t0 = time.perf_counter()
            try:
                r = fn()
                rtt = int((time.perf_counter() - t0) * 1e6)
                rows.append((name, rtt, True, str(r)[:40]))
                ok += 1
            except Exception as e:      # noqa: BLE001
                rtt = int((time.perf_counter() - t0) * 1e6)
                rows.append((name, rtt, False, str(e)[:40]))
            total += 1

        check("change_session(extended)", lambda: c.change_session(3))
        check("read_data_by_identifier F190", lambda: c.read_data_by_identifier(0xF190))
        check("write F190 before unlock", lambda: _expect_nrc(c, lambda: c.write_data_by_identifier(0xF190, new_vin.decode()), 0x33))
        check("unlock level1", lambda: c.unlock_security_access(1))
        check("write F190 after unlock", lambda: c.write_data_by_identifier(0xF190, new_vin.decode()))
        check("routine_control 0x0203", lambda: c.routine_control(0x0203, 1))
        check("read_dtc_information", lambda: c.read_dtc_information(
            udsoncan.services.ReadDTCInformation.Subfunction.reportDTCByStatusMask, 0xFF))
        check("clear_dtc", lambda: c.clear_dtc(0xFFFFFF))
        check("tester_present", lambda: c.tester_present())
        check("change_session(default)", lambda: c.change_session(1))
    bus.shutdown()
    print("== UDS 诊断序列（impl=udsoncan, %s）==" % args.channel)
    for name, rtt, good, note in rows:
        print("  %-32s %-9d %s %s" % (name, rtt, "PASS" if good else "FAIL", note))
    print("[uds_tester] %s steps=%d/%d" % ("PASS" if ok == total else "FAIL", ok, total))
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main())
