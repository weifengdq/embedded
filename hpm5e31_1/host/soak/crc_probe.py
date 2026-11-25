#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""crc_probe.py - 千兆链路 CRC 误码定向测量（板 -> 主机方向）

为什么需要它：15 h 耐久只能给出每 30 s 一个点的 ppm，无法分辨"误码是随帧、
随时间还是随帧长"。本工具用受控流量 + 秒级主机计数器 + 板端发帧计数，
给出一次运行内的 ppm 与每秒分布，用来判断机理并比较 PHY/RGMII 延迟档位。

用法（在远程 Ubuntu 上跑）：
  python3 host/soak/crc_probe.py ack   --secs 60            # 主机->板 TCP，板只发 ACK（小帧）
  python3 host/soak/crc_probe.py data  --secs 60            # 板->主机 TCP（1518 B 数据帧）
  python3 host/soak/crc_probe.py udp   --secs 60            # 主机->板 UDP 900M（板几乎不发）
  python3 host/soak/crc_probe.py ping  --secs 30            # ping 回包（98 B，低频）

输出：每秒 host rx_crc_errors 增量、板端 tx_frames 增量、折算 ppm，以及 JSON 摘要
（便于脚本化比较不同 PHY 延迟档位）。
"""
import argparse
import fcntl
import json
import os
import re
import subprocess
import sys
import threading
import time

TIOCEXCL = 0x540E

RE_SW = re.compile(r"\| tx_frames=(\d+) tx_err=(\d+) tx_drop_len=(\d+)")
RE_HW = re.compile(r"rx_frames=(\d+) rx_bytes=(\d+) rx_empty=(\d+) rx_err=(\d+) \| tx_frames=(\d+) tx_bytes=(\d+) tx_busy=(\d+) tx_err=(\d+)")
RE_PHYREG = re.compile(r"PHY BMCR=([0-9A-Fa-f]+) BMSR=([0-9A-Fa-f]+) GBSR=([0-9A-Fa-f]+) PHYSR=([0-9A-Fa-f]+)")
RE_UPTIME = re.compile(r"\[uptime\]\s*([0-9.]+)")
RE_IPERF = re.compile(r"\[iperf\] type=(\d+) remote=(\S+) bytes=(\d+) ms=(\d+) => (\d+) kbit/s")


class Board(object):
    """极简串口客户端：后台读线程 + 最新解析值 + 发命令等回显。"""

    def __init__(self, port, baud):
        import serial
        self.ser = serial.Serial(port, baud, timeout=0.1)
        try:
            fcntl.ioctl(self.ser.fileno(), TIOCEXCL)
        except Exception:
            pass
        self.lines = []
        self.data = {}
        self.lock = threading.Lock()
        self.stop = False
        self.th = threading.Thread(target=self._run, daemon=True)
        self.th.start()

    def _run(self):
        buf = b""
        while not self.stop:
            try:
                chunk = self.ser.read(4096)
            except Exception:
                time.sleep(0.2)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").replace("\r", "")
                with self.lock:
                    self.lines.append(line)
                    if len(self.lines) > 4000:
                        del self.lines[:2000]
                    self._parse(line)

    def _parse(self, line):
        m = RE_HW.search(line)
        if m:
            self.data.update(hw_rx_frames=int(m.group(1)), hw_tx_frames=int(m.group(5)),
                             hw_tx_err=int(m.group(8)), hw_rx_err=int(m.group(4)))
            return
        m = RE_SW.search(line)
        if m:
            self.data.update(sw_tx_frames=int(m.group(1)))
            return
        m = RE_PHYREG.search(line)
        if m:
            self.data.update(bmcr=m.group(1), bmsr=m.group(2), gbsr=m.group(3), physr=m.group(4))
            return
        m = RE_UPTIME.search(line)
        if m:
            self.data.update(uptime_s=float(m.group(1)))
            return
        m = RE_IPERF.search(line)
        if m:
            self.data.update(iperf_mbit=float(m.group(5)) / 1000.0, iperf_bytes=int(m.group(3)))

    def send(self, text):
        self.ser.write((text + "\r\n").encode())
        self.ser.flush()

    def snapshot(self):
        with self.lock:
            return dict(self.data)

    def ping_eth(self, timeout=1.0):
        """发一条 eth，等 hw 统计行刷新。"""
        with self.lock:
            n0 = len(self.lines)
        self.send("eth")
        t0 = time.time()
        while time.time() - t0 < timeout:
            with self.lock:
                hit = any(("tx_frames=" in ln and "rx_frames=" in ln and "|" in ln)
                          for ln in self.lines[n0:])
            if hit:
                return self.snapshot()   # 注意：不能在上面的锁内调用（Lock 不可重入）
            time.sleep(0.02)
        return self.snapshot()

    def close(self):
        self.stop = True
        try:
            self.ser.close()
        except Exception:
            pass


def sysfs(nic, key):
    p = "/sys/class/net/%s/statistics/%s" % (nic, key)
    try:
        with open(p) as f:
            return int(f.read().strip())
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["ack", "data", "udp", "ping"])
    ap.add_argument("--secs", type=int, default=60)
    ap.add_argument("--nic", default="enp6s0")
    ap.add_argument("--serial", default="/dev/ttyACM1")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--board-ip", default="192.168.2.100")
    ap.add_argument("--iperf", default="/usr/bin/iperf")
    ap.add_argument("--json", default="")
    ap.add_argument("--no-board", action="store_true")
    ap.add_argument("--note", default="")
    ap.add_argument("--rate", default="", help="ACK: iperf -b 限速（如 10M）；空=不限速")
    ap.add_argument("--bidir", action="store_true", help="ACK: 同时让板发数据（全双工）")
    ap.add_argument("--txdly", type=int, default=-1, help="先下发 eth txdly <n>（RGMII TXC 档位）")
    a = ap.parse_args()

    board = None if a.no_board else Board(a.serial, a.baud)
    if board:
        time.sleep(0.3)
        board.send("")
        time.sleep(0.3)
        board.ping_eth()            # 让基线 tx_frames 有值（否则 ppm 算不出来）
        if a.txdly >= 0:
            board.send("eth txdly %d" % a.txdly)
            time.sleep(0.5)
            print("[pre] txdly -> %d (readback in serial log)" % a.txdly, flush=True)

    # 预检
    print("[pre] nic=%s mode=%s secs=%d" % (a.nic, a.mode, a.secs), flush=True)
    print("[pre] host crc=%s errors=%s" % (sysfs(a.nic, "rx_crc_errors"), sysfs(a.nic, "rx_errors")), flush=True)

    host_proc = None
    board_cmd = None
    if a.mode == "ack":
        # 板作 TCP server；监听可能已存在（lwiperf 特性），两种回显都接受
        if board:
            board.send("eth iserver tcp")
            time.sleep(0.6)
    elif a.mode == "data":
        # 主机 iperf -s，板 lwiperf client（每次 10 s，循环到 --secs）
        host_proc = subprocess.Popen([a.iperf, "-s", "-i", "10", "-f", "m"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        board_cmd = "eth iclient %s tcp" % "192.168.2.1"
    elif a.mode == "udp":
        if board:
            board.send("eth iserver udp")
            time.sleep(0.6)
    elif a.mode == "ping":
        pass

    # 基线
    time.sleep(0.3)
    b = board.snapshot() if board else {}
    tx0 = b.get("hw_tx_frames")
    sw0 = b.get("sw_tx_frames")
    up0 = b.get("uptime_s")
    c0 = sysfs(a.nic, "rx_crc_errors")
    e0 = sysfs(a.nic, "rx_errors")
    t0 = time.time()
    print("[base] board tx_frames=%s sw_tx=%s uptime=%s host_crc=%s host_err=%s" % (tx0, sw0, up0, c0, e0), flush=True)

    samples = []
    proc = None
    if a.mode == "ack":
        argv = [a.iperf, "-c", a.board_ip, "-t", str(a.secs), "-i", "5", "-y", "C", "-f", "m"]
        if a.rate:
            argv += ["-b", a.rate]
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elif a.mode == "udp":
        proc = subprocess.Popen([a.iperf, "-c", a.board_ip, "-u", "-b", "900M", "-t", str(a.secs), "-i", "5", "-y", "C", "-f", "m"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elif a.mode == "ping":
        proc = subprocess.Popen(["sudo", "-n", "ping", "-f", "-w", str(a.secs), a.board_ip],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elif a.mode == "data" and board:
        board.send(board_cmd)

    if a.bidir and board:
        # 全双工：主机->板 TCP 打流（板发 ACK）的同时，板->主机也发数据
        board.send("eth iclient 192.168.2.1 tcp")
        host_proc = subprocess.Popen([a.iperf, "-s", "-i", "10", "-f", "m"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)

    # 秒级采样（data 模式板端 lwiperf 单次 10 s，用 --secs//10 次续跑）
    next_client = time.time() + 10.0
    while time.time() - t0 < a.secs:
        time.sleep(1.0)
        c = sysfs(a.nic, "rx_crc_errors")
        e = sysfs(a.nic, "rx_errors")
        bs = board.snapshot() if board else {}
        tx = bs.get("hw_tx_frames")
        samples.append({"t": round(time.time() - t0, 1), "crc": c, "err": e, "tx": tx})
        if (a.mode == "data" or a.bidir) and board and time.time() >= next_client:
            board.send("eth iclient 192.168.2.1 tcp")
            next_client = time.time() + 10.0

    # 结束
    if proc is not None:
        try:
            out, _ = proc.communicate(timeout=a.secs + 25)
        except Exception:
            proc.kill(); out = ""
    else:
        out = ""
    time.sleep(0.5)
    b1 = board.ping_eth() if board else {}
    c1 = sysfs(a.nic, "rx_crc_errors")
    e1 = sysfs(a.nic, "rx_errors")
    tx1 = b1.get("hw_tx_frames", tx0)
    sw1 = b1.get("sw_tx_frames", sw0)
    up1 = b1.get("uptime_s", up0)
    dt = time.time() - t0

    dcrc = c1 - c0
    dtx = (tx1 - tx0) if (tx0 is not None and tx1 is not None) else None
    ppm = (1e6 * dcrc / dtx) if dtx else None
    # 每秒增量
    per_sec = []
    prev = c0
    for s in samples:
        if s["crc"] is not None:
            per_sec.append(max(0, s["crc"] - prev))
            prev = s["crc"]

    res = {
        "mode": a.mode, "secs": round(dt, 2), "note": a.note,
        "host_crc_delta": dcrc, "host_err_delta": e1 - e0,
        "board_tx_frames": dtx, "board_sw_tx_frames": (sw1 - sw0) if (sw0 is not None and sw1 is not None) else None,
        "board_uptime_delta": (up1 - up0) if (up0 is not None and up1 is not None) else None,
        "crc_ppm": ppm, "crc_per_sec": dcrc / dt if dt else None,
        "board_tx_fps": (dtx / dt) if (dtx and dt) else None,
        "per_sec": per_sec,
        "iperf_tail": (out or "").strip().splitlines()[-4:],
        "host_crc_total": c1,
    }
    print("[result] %s" % json.dumps(res, ensure_ascii=False), flush=True)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(res, f, indent=1, ensure_ascii=False)
    if host_proc is not None:
        host_proc.terminate()
        try:
            host_proc.wait(timeout=5)
        except Exception:
            host_proc.kill()
    if board:
        board.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
