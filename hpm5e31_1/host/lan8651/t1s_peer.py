#!/usr/bin/env python3
"""t1s_peer.py —— 主机侧 10BASE-T1S 裸帧对端（配合 samples/lan8651/lan8651_raw_frame）

用法(需要 sudo, 因为 AF_PACKET 需要 CAP_NET_RAW):
    sudo python3 t1s_peer.py --iface enx001ec0d1c337 --count 10 --interval 0.2

行为: 发 count 个 EtherType=0x88B5 的帧(广播), 然后等待并打印板端回帧。
"""
import argparse
import socket
import sys
import time

ETHERTYPE = 0x88B5


def get_mac(iface):
    with open("/sys/class/net/%s/address" % iface) as f:
        return bytes.fromhex(f.read().strip().replace(":", ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="enx001ec0d1c337")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--interval", type=float, default=0.2)
    ap.add_argument("--wait", type=float, default=5.0)
    ap.add_argument("--ethertype", type=lambda x: int(x, 0), default=ETHERTYPE)
    ap.add_argument("--loop", action="store_true", help="send forever until /tmp/t1s_stop exists")
    ap.add_argument("--rate", type=float, default=5.0, help="frames per second in loop mode")
    args = ap.parse_args()

    src = get_mac(args.iface)
    dst = b"\xff" * 6
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(args.ethertype))
    s.bind((args.iface, 0))
    s.settimeout(0.2)
    print("[host] iface=%s mac=%s ethertype=0x%04x" % (args.iface, src.hex(":"), args.ethertype))

    if args.loop:
        import os
        i = 0
        period = 1.0 / args.rate
        while not os.path.exists("/tmp/t1s_stop"):
            payload = bytes([0x11, 0x22, i & 0xFF, 0x33]) + bytes(range(40))
            frame = dst + src + args.ethertype.to_bytes(2, "big") + payload
            s.send(frame)
            if i % 10 == 0:
                print("[host tx] #%d len=%d" % (i, len(frame)), flush=True)
            i += 1
            t_end = time.time() + period
            while time.time() < t_end:
                try:
                    r = s.recv(2048)
                except socket.timeout:
                    continue
                if int.from_bytes(r[12:14], "big") != args.ethertype:
                    continue
                print("[host rx] len=%d src=%s payload=%s" % (len(r), r[6:12].hex(":"), r[14:24].hex()), flush=True)
        print("[host] loop finished, sent %d frames" % i)
        s.close()
        return 0

    for i in range(args.count):
        payload = bytes([0x11, 0x22, i & 0xFF, 0x33]) + bytes(range(40))
        frame = dst + src + args.ethertype.to_bytes(2, "big") + payload
        s.send(frame)
        print("[host tx] #%d len=%d" % (i, len(frame)))
        time.sleep(args.interval)
        # drain anything already queued
        try:
            while True:
                r = s.recv(2048)
                print("[host rx] len=%d src=%s type=0x%04x payload=%s" %
                      (len(r), r[6:12].hex(":"), int.from_bytes(r[12:14], "big"), r[14:22].hex()))
        except socket.timeout:
            pass

    t0 = time.time()
    n = 0
    while time.time() - t0 < args.wait:
        try:
            r = s.recv(2048)
        except socket.timeout:
            continue
        if int.from_bytes(r[12:14], "big") != args.ethertype:
            continue
        n += 1
        print("[host rx] len=%d src=%s payload=%s" % (len(r), r[6:12].hex(":"), r[14:24].hex()))
    print("[host] received %d frames with ethertype 0x%04x" % (n, args.ethertype))
    s.close()
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
