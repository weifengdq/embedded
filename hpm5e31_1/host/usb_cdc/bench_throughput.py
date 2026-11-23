#!/usr/bin/env python3
"""host/usb_cdc/bench_throughput.py -- 主机侧 USB CDC ACM 回显吞吐实测（第 20 章）

板端：samples/usb/usb_cdc_acm_echo（CherryUSB CDC ACM，收到什么回什么）。
主机：pyserial 打开 CDC 节点（VID 0x34b7），后台线程持续排空回显，主线程按块大小写 1 MB，
      统计"主机->设备"吞吐；另做 512 B 回显一致性检查与"先写满再读"行为记录。

用法：python3 host/usb_cdc/bench_throughput.py [--json]
日志：logs/<时间戳>-usb_cdc-throughput-host.log
"""
import argparse
import json
import os
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("need pyserial", file=sys.stderr)
    sys.exit(2)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOGDIR = os.path.join(REPO, "logs")


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x34B7:
            return p.device
    for cand in ("/dev/ttyACM2", "/dev/ttyACM1"):
        if os.path.exists(cand):
            return cand
    return None


class Reader(threading.Thread):
    def __init__(self, ser):
        super().__init__(daemon=True)
        self.ser = ser
        self.n = 0
        self.stop = False

    def run(self):
        while not self.stop:
            try:
                b = self.ser.read(4096)
            except Exception:
                break
            if b:
                self.n += len(b)


def write_phase(ser, blk, total, max_s):
    sent = 0
    t0 = time.monotonic()
    while sent < total:
        n = min(blk, total - sent)
        payload = bytes(((sent + i) & 0xFF) for i in range(n))
        w = ser.write(payload)
        ser.flush()
        if w != n:
            return sent, time.monotonic() - t0, "short-write"
        sent += n
        if time.monotonic() - t0 > max_s:
            return sent, time.monotonic() - t0, "timeout"
    return sent, time.monotonic() - t0, "ok"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    port = args.port or find_port()
    ts = time.strftime("%Y%m%d-%H%M%S")
    os.makedirs(LOGDIR, exist_ok=True)
    logpath = os.path.join(LOGDIR, "%s-usb_cdc-throughput-host.log" % ts)
    lines = []

    def log(s):
        print(s)
        lines.append(s)

    log("=== USB CDC ACM echo throughput (host side, pyserial) ===")
    log("time: %s" % ts)
    if port is None:
        log("RESULT: no CDC ACM device found (expect VID 0x34b7)")
        open(logpath, "w").write("\n".join(lines) + "\n")
        return 1
    log("port: %s" % port)
    try:
        ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=5.0)
        ser.dtr = True
        ser.rts = True
        time.sleep(0.3)
        ser.reset_input_buffer()
    except Exception as e:
        log("open failed: %r" % (e,))
        open(logpath, "w").write("\n".join(lines) + "\n")
        return 1

    # ---- A) 512 B 回显一致性 ----
    pat = bytes((i * 7 + 3) & 0xFF for i in range(512))
    ser.write(pat)
    ser.flush()
    got = b""
    t0 = time.monotonic()
    while len(got) < 512 and time.monotonic() - t0 < 2.0:
        got += ser.read(512 - len(got))
    integ = (got == pat)
    log("integrity 512 B echo: %s (%d/%d B)" % ("PASS" if integ else "FAIL", len(got), 512))

    # ---- B) 吞吐：后台排空回显，主线程写 ----
    results = []
    for blk in (512, 2048, 16384):
        ser.reset_input_buffer()
        rd = Reader(ser)
        rd.start()
        sent, dt, why = write_phase(ser, blk, 1024 * 1024, 8.0)
        time.sleep(0.2)
        rd.stop = True
        rd.join(timeout=1.0)
        mbps = sent / dt / 1e6 if dt > 0 else 0.0
        results.append(dict(block=blk, bytes=sent, seconds=round(dt, 4),
                            mbps=round(mbps, 3), echo_bytes=rd.n, status=why))
        log("block=%-6d write %d B in %.4f s -> %.3f MB/s (echo %d B) %s"
            % (blk, sent, dt, mbps, rd.n, why))
        ser.reset_input_buffer()

    # ---- C) 先写满再读（例程 echo 的已知行为） ----
    ser.reset_input_buffer()
    total = 256 * 1024
    t0 = time.monotonic()
    ser.write(bytes((i * 3) & 0xFF for i in range(2048)) * (total // 2048))
    ser.flush()
    wdt = time.monotonic() - t0
    rd = Reader(ser)
    rd.start()
    time.sleep(1.0)
    rd.stop = True
    rd.join(timeout=1.0)
    log("write-all-then-read: wrote %d B in %.3f s, read back %d B in 1 s"
        % (total, wdt, rd.n))
    ser.close()

    log("RESULT: %d sizes measured" % len(results))
    if args.json:
        log(json.dumps(dict(port=port, integrity=integ, results=results)))
    open(logpath, "w").write("\n".join(lines) + "\n")
    log("log: %s" % logpath)
    return 0 if results else 1


if __name__ == "__main__":
    sys.exit(main())
