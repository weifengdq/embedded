#!/usr/bin/env python3
"""eth_host.py —— eth_raw_echo / eth_irq / eth_stats 的主机对打工具（AF_PACKET 原始套接字）

不用 IP、不用 scapy：直接 AF_PACKET + EtherType 0x88B5 收发自定义帧。
板端帧格式：dst(6) src(6) type=0x88B5(2) magic="ETH0"(4) seq(4) pattern(...)

用法（需 sudo，或给 python3 CAP_NET_RAW）：
  sudo python3 eth_host.py --iface enp6s0 echo  --secs 25          # 回显：收一帧回一帧
  sudo python3 eth_host.py --iface enp6s0 rtt   --len 64 --n 200   # 主动测 RTT（板端 echo 模式）
  sudo python3 eth_host.py --iface enp6s0 flood --len 1514 --secs 10   # 灌包（板端 eth rx）
  sudo python3 eth_host.py --iface enp6s0 listen --secs 10          # 只听，统计帧率/长度分布

输出统一以 "HOST " 开头，便于日志里和板端行区分开。
"""
import argparse
import socket
import struct
import sys
import time
from collections import Counter

ETH_P = 0x88B5
MAGIC = b"ETH0"
HDR = 14


def iface_mac(iface):
    with open("/sys/class/net/%s/address" % iface) as f:
        return bytes.fromhex(f.read().strip().replace(":", ""))


class Raw:
    def __init__(self, iface):
        self.iface = iface
        self.mac = iface_mac(iface)
        self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P))
        self.sock.bind((iface, 0))
        self.sock.settimeout(0.05)

    def send(self, frame):
        self.sock.send(frame)

    def recv(self):
        try:
            data = self.sock.recv(2048)
        except socket.timeout:
            return None
        return data

    def build(self, dst, seq, length, magic=MAGIC):
        """构造 length 字节净荷的测试帧：magic + seq + 递增 pattern"""
        payload = bytearray(length)
        payload[0:4] = magic
        struct.pack_into("<I", payload, 4, seq & 0xFFFFFFFF)
        for i in range(8, length):
            payload[i] = (i + seq) & 0xFF
        return dst + self.mac + struct.pack("!H", ETH_P) + bytes(payload)


def parse(frame):
    if len(frame) < HDR + 8:
        return None
    etype = struct.unpack("!H", frame[12:14])[0]
    if etype != ETH_P:
        return None
    payload = frame[HDR:]
    if payload[0:4] != MAGIC:
        return None
    seq = struct.unpack("<I", payload[4:8])[0]
    return {"dst": frame[0:6], "src": frame[6:12], "seq": seq, "len": len(payload)}


def mode_echo(r, secs):
    n = 0
    t0 = time.time()
    while time.time() - t0 < secs:
        f = r.recv()
        if f is None:
            continue
        p = parse(f)
        if p is None:
            continue
        # 交换源/目的 MAC 回发，净荷原样带回（板端按 magic+seq 配对）
        r.send(p["dst"] and (p["src"] + r.mac + struct.pack("!H", ETH_P) + f[HDR:] and
                             p["src"] + r.mac + struct.pack("!H", ETH_P) + f[HDR:]))
        n += 1
    print("HOST echo secs=%d replied=%d" % (secs, n))


def mode_rtt(r, length, n):
    dst = bytes.fromhex("ffffffffffff")
    t0s = {}
    lat = []
    for i in range(n):
        frame = r.build(dst, i, length)
        t0s[i] = time.perf_counter()
        r.send(frame)
    deadline = time.time() + 2.0
    got = 0
    while time.time() < deadline and got < n:
        f = r.recv()
        if f is None:
            continue
        p = parse(f)
        if p is None:
            continue
        seq = p["seq"]
        if seq in t0s:
            lat.append((time.perf_counter() - t0s.pop(seq)) * 1e6)
            got += 1
    if lat:
        lat.sort()
        print("HOST rtt len=%d n=%d got=%d lost=%d avg=%.1fus min=%.1fus p50=%.1fus p99=%.1fus max=%.1fus"
              % (length, n, got, n - got, sum(lat) / len(lat), lat[0],
                 lat[len(lat) // 2], lat[min(len(lat) - 1, int(len(lat) * 0.99))], lat[-1]))
    else:
        print("HOST rtt len=%d n=%d got=0 lost=%d" % (length, n, n))


def mode_flood(r, length, secs, dst_mac, rate):
    dst = bytes.fromhex(dst_mac.replace(":", "")) if dst_mac else bytes.fromhex("ffffffffffff")
    n = 0
    t0 = time.time()
    period = (1.0 / rate) if rate > 0 else 0.0
    nxt = t0
    while time.time() - t0 < secs:
        r.send(r.build(dst, n, length))
        n += 1
        if period:
            nxt += period
            d = nxt - time.time()
            if d > 0:
                time.sleep(d)
    dt = time.time() - t0
    print("HOST flood len=%d n=%d secs=%.2f fps=%.0f mbit=%.1f"
          % (length, n, dt, n / dt, n * (length + HDR + 4) * 8 / dt / 1e6))


def mode_listen(r, secs):
    n = 0
    lens = Counter()
    t0 = time.time()
    while time.time() - t0 < secs:
        f = r.recv()
        if f is None:
            continue
        p = parse(f)
        if p is None:
            continue
        n += 1
        lens[p["len"]] += 1
    dt = time.time() - t0
    top = ", ".join("%d:%d" % (k, v) for k, v in lens.most_common(5))
    print("HOST listen secs=%.1f frames=%d fps=%.0f lens={%s}" % (dt, n, n / dt, top))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="enp6s0")
    ap.add_argument("--len", type=int, default=64, help="净荷长度（不含 14B 头）")
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--dst", default=None, help="flood 目的 MAC（默认广播）")
    ap.add_argument("--rate", type=float, default=0, help="flood 发送速率 fps（0=全速）")
    ap.add_argument("mode", choices=["echo", "rtt", "flood", "listen"])
    a = ap.parse_args()

    r = Raw(a.iface)
    print("HOST iface=%s mac=%s ethertype=0x%04X mode=%s"
          % (a.iface, ":".join("%02x" % b for b in r.mac), ETH_P, a.mode))
    if a.mode == "echo":
        mode_echo(r, int(a.secs))
    elif a.mode == "rtt":
        mode_rtt(r, a.len, a.n)
    elif a.mode == "flood":
        mode_flood(r, a.len, a.secs, a.dst, a.rate)
    else:
        mode_listen(r, a.secs)


if __name__ == "__main__":
    main()
