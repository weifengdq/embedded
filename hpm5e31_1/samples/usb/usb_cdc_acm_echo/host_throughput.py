#!/usr/bin/env python3
"""host_throughput.py - USB CDC ACM (bulk) 吞吐与稳定性实测（主机侧）

用法:
    python3 host_throughput.py                 # 自动找 HPMicro(VID 34b7) CDC 端口
    python3 host_throughput.py --port /dev/ttyACM2 --mb 1
    python3 host_throughput.py --sweep         # 块大小扫描
    python3 host_throughput.py --cycles 20     # 与 CH343 共存: 20 次开关端口 + 1KB 往返

判据: 回读内容与写入一致; 峰值吞吐看 1 MB 大块; 开关 20 次节点号不变即 PASS。
"""
import argparse
import glob
import os
import sys
import time

import serial

HPM_VID = "34b7"   # HPMicro

def find_port():
    for p in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        try:
            vid = open(p).read().strip().lower()
        except OSError:
            continue
        if vid == HPM_VID:
            dev = os.path.dirname(p)
            for t in glob.glob(dev + "/*/tty/ttyACM*"):
                return "/dev/" + os.path.basename(t), os.path.basename(dev)
    return None, None

def open_port(port):
    s = serial.Serial(port, 921600, timeout=2.0, write_timeout=10.0)
    s.dtr = True
    s.rts = True
    time.sleep(0.05)
    s.reset_input_buffer()
    s.reset_output_buffer()
    return s

def read_exact(s, n, timeout=10.0):
    buf = bytearray()
    t0 = time.perf_counter()
    while len(buf) < n:
        chunk = s.read(min(65536, n - len(buf)))
        if chunk:
            buf += chunk
            t0 = time.perf_counter()
        elif time.perf_counter() - t0 > timeout:
            break
    return bytes(buf)

def run_once(port, total, block):
    s = open_port(port)
    payload = bytes((i * 7 + 3) & 0xFF for i in range(block))
    written = 0
    t0 = time.perf_counter()
    while written < total:
        n = min(block, total - written)
        s.write(payload[:n])
        written += n
    s.flush()
    t_wr = time.perf_counter() - t0
    got = read_exact(s, total)
    t_all = time.perf_counter() - t0
    s.close()
    expect = payload * (total // block) if total % block == 0 else None
    ok = (len(got) == total) and (expect is not None) and (got == expect)
    return t_wr, t_all, total / t_wr / 1e6, total / t_all / 1e6, ok, len(got)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--mb", type=float, default=1.0)
    ap.add_argument("--block", type=int, default=16384)
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--cycles", type=int, default=0)
    args = ap.parse_args()

    port, dev = (args.port, "?") if args.port else find_port()
    if not port:
        print("CDC device (VID %s) not found; board not running usb_cdc_acm_echo?" % HPM_VID)
        return 2
    print("CDC port=%s usbdev=%s" % (port, dev))

    total = int(args.mb * 1024 * 1024)
    if args.sweep:
        print("%-10s %-10s %-12s %-12s %-8s" % ("block", "bytes", "write_MBps", "echo_MBps", "data"))
        for block in (512, 2048, 8192, 16384, 65536):
            if block > total or total % block:
                continue
            t_wr, t_all, wr, ec, ok, got = run_once(port, total, block)
            print("%-10d %-10d %-12.2f %-12.2f %-8s" % (block, total, wr, ec, "OK" if ok else "BAD"))
    else:
        t_wr, t_all, wr, ec, ok, got = run_once(port, total, args.block)
        print("BENCH_CDC total=%d block=%d write_s=%.3f write_MBps=%.2f echo_roundtrip_s=%.3f echo_MBps=%.2f data=%s got=%d"
              % (total, args.block, t_wr, wr, t_all, ec, "OK" if ok else "BAD", got))

    if args.cycles:
        ok_n = 0
        names = set()
        for i in range(args.cycles):
            try:
                s = open_port(port)
                names.add(os.path.basename(os.path.realpath(port)))
                s.write(b"PING" * 256)
                s.flush()
                got = read_exact(s, 1024, timeout=3.0)
                s.close()
                if got == b"PING" * 256:
                    ok_n += 1
                else:
                    print("  cycle %d FAIL got=%d" % (i + 1, len(got)))
            except Exception as e:
                print("  cycle %d EXC %s" % (i + 1, e))
            time.sleep(0.05)
        ch_ok = os.path.exists("/dev/ttyACM0")
        print("COEXIST cycles=%d ok=%d/%d node=%s ch343_present=%d"
              % (args.cycles, ok_n, args.cycles, ",".join(sorted(names)), 1 if ch_ok else 0))
    return 0

if __name__ == "__main__":
    sys.exit(main())
