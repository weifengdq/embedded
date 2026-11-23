#!/usr/bin/env python3
"""isotp_test.py - ISO-TP 回显测试（上位机侧）

两种实现对照：
  --impl raw     本仓库 common.py 的裸 SocketCAN+ISO-TP 实现（逐帧可控、可打印）
  --impl isotp   can-isotp 第三方库（验证"别人的实现也能跟我们的 ECU 互通"）
  --impl both    两种都跑，结果并列

对象：
  * 真实 DUT（Bench B：vcan0 <- cannelloni <- 板 CAN1 网关 <- 总线 <- 板 CAN0 的 isotp_echo 固件）
  * 模拟器（host/uds/ecu_sim.py --mode echo，用于先排除工具自身问题）

用法：
  python3 isotp_test.py --channel vcan0
  python3 isotp_test.py --channel vcan0 --sizes 1,7,8,64,256,1024,4095 --impl both
"""
import argparse
import struct
import sys
import time

import common

SIZES_DEFAULT = [1, 7, 8, 12, 64, 256, 1024, 4095]


def pattern(n):
    x = 0x12345678 ^ n
    out = bytearray(n)
    for i in range(n):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFF
        out[i] = (x >> 24) & 0xFF
    return bytes(out)


def run_raw(channel, sizes, bs, stmin, tx_id, rx_id, timeout):
    bus = common.RawCan(channel)
    tp = common.IsoTp(bus, rx_id=rx_id, tx_id=tx_id, bs=bs, stmin=stmin, n_bs=timeout, n_cr=timeout)
    rows = []
    ok = 0
    for n in sizes:
        data = pattern(n)
        t0 = time.perf_counter()
        try:
            tp.send(data)
            resp = tp.recv(timeout)
            rtt = (time.perf_counter() - t0) * 1e6
            good = resp == data
        except common.IsoTpError as e:
            resp, rtt, good = b"", 0.0, False
            print("  len=%-5d ERROR %s" % (n, e))
        kbps = (2 * n) / (rtt / 1e6) / 1024 if rtt else 0
        rows.append((n, rtt, kbps, good))
        ok += 1 if good else 0
        print("  len=%-5d rtt=%-9.0f us  %-8.1f KB/s  %s" % (n, rtt, kbps, "OK" if good else "FAIL"))
    bus.close()
    return ok, rows


def run_isotp_lib(channel, sizes, tx_id, rx_id, timeout):
    """用 can-isotp + python-can 做同一件事（第三方实现对照）。"""
    try:
        import can
        import isotp
    except ImportError:
        print("  [skip] 未安装 python-can / can-isotp")
        return 0, []
    bus = can.interface.Bus(channel=channel, interface="socketcan")
    addr = isotp.Address(isotp.AddressingMode.Normal_11bits, txid=tx_id, rxid=rx_id)
    # can-isotp 2.x：CanStack 必须显式 start()（内部线程负责收发，不要手动 process()）
    stack = isotp.CanStack(bus=bus, address=addr,
                           params={"blocksize": 0, "stmin": 0,
                                   "rx_flowcontrol_timeout": int(timeout * 1000),
                                   "rx_consecutive_frame_timeout": int(timeout * 1000)})
    stack.start()
    rows = []
    ok = 0
    for n in sizes:
        data = pattern(n)
        t0 = time.perf_counter()
        try:
            stack.send(data)
            resp = stack.recv()
            rtt = (time.perf_counter() - t0) * 1e6
            good = resp == data
        except Exception as e:      # noqa: BLE001 - 第三方库异常类型多
            resp, rtt, good = b"", 0.0, False
            print("  len=%-5d ERROR %s" % (n, e))
        kbps = (2 * n) / (rtt / 1e6) / 1024 if rtt else 0
        rows.append((n, rtt, kbps, good))
        ok += 1 if good else 0
        print("  len=%-5d rtt=%-9.0f us  %-8.1f KB/s  %s" % (n, rtt, kbps, "OK" if good else "FAIL"))
    stack.stop()
    bus.shutdown()
    return ok, rows


def main():
    ap = argparse.ArgumentParser(description="ISO-TP 回显测试（裸实现 / can-isotp 对照）")
    ap.add_argument("--channel", default="vcan0", help="SocketCAN 接口（默认 vcan0）")
    ap.add_argument("--impl", default="raw", choices=["raw", "isotp", "both"])
    ap.add_argument("--sizes", default=",".join(str(s) for s in SIZES_DEFAULT))
    ap.add_argument("--tx-id", default="0x700", help="请求 ID（tester -> DUT）")
    ap.add_argument("--rx-id", default="0x708", help="响应 ID（DUT -> tester）")
    ap.add_argument("--bs", type=int, default=0, help="本端作为接收方通告的 BS")
    ap.add_argument("--stmin", type=lambda s: int(s, 0), default=0, help="本端通告的 STmin")
    ap.add_argument("--timeout", type=float, default=3.0, help="收发超时（秒）")
    args = ap.parse_args()
    sizes = [int(s, 0) for s in args.sizes.split(",") if s.strip()]
    tx_id = int(args.tx_id, 0)
    rx_id = int(args.rx_id, 0)

    print("== ISO-TP echo test: channel=%s id 0x%03X->0x%03X BS=%d STmin=0x%02X ==" %
          (args.channel, tx_id, rx_id, args.bs, args.stmin))
    total_ok = total = 0
    t_start = time.perf_counter()
    if args.impl in ("raw", "both"):
        print("[impl=raw] 本仓库 common.py 裸实现")
        ok, _ = run_raw(args.channel, sizes, args.bs, args.stmin, tx_id, rx_id, args.timeout)
        total_ok += ok
        total += len(sizes)
    if args.impl in ("isotp", "both"):
        print("[impl=can-isotp] 第三方库对照")
        ok, _ = run_isotp_lib(args.channel, sizes, tx_id, rx_id, args.timeout)
        total_ok += ok
        total += len(sizes)
    dt = time.perf_counter() - t_start
    print("[isotp_test] %s sizes=%d/%d elapsed=%.2f s" %
          ("PASS" if total_ok == total and total else "FAIL", total_ok, total, dt))
    return 0 if (total and total_ok == total) else 1


if __name__ == "__main__":
    sys.exit(main())
