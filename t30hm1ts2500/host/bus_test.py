#!/usr/bin/env python3
"""bus_test.py - 10BASE-T1S 总线功能/性能测试

注意: 板端 lwIP 的 lwiperf 只实现 **iperf2** 协议, 主机请用 iperf (v2), 不要用 iperf3。

用法:
  ./bus_test.py plca                 # 显示主机 PLCA 配置与状态
  ./bus_test.py ping 11 12 13        # ping 192.168.10.x
  ./bus_test.py arp                  # 扫描 ARP
  ./bus_test.py iperf 11             # iperf2 双向测速 (板端需 iperf -s)
  ./bus_test.py udp 11 7             # UDP echo 测试 (端口 7)
  ./bus_test.py soak 11 1000         # 1000 次 ping, 统计丢包/RTT
"""
import argparse
import glob
import re
import socket
import subprocess
import sys

IFACE = "enx001ec0d1c337"
NET = "192.168.10."


def sh(cmd, check=False):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and p.returncode != 0:
        raise RuntimeError(p.stderr)
    return p.stdout + p.stderr


def iface():
    for d in glob.glob("/sys/class/net/*"):
        try:
            if open(d + "/address").read().strip() == "00:1e:c0:d1:c3:37":
                return d.split("/")[-1]
        except OSError:
            pass
    return IFACE


def cmd_plca(_):
    i = iface()
    print(sh(f"ip -br addr show {i}"))
    print(sh(f"ethtool --get-plca-cfg {i}"))
    print(sh(f"ethtool --get-plca-status {i}"))


def cmd_ping(args):
    for a in args:
        ip = NET + str(a)
        out = sh(f"ping -c 5 -i 0.2 -W 1 {ip}")
        m = re.search(r"(\d+) packets transmitted, (\d+) received.*?([\d.]+)% packet loss", out)
        rtt = re.search(r"rtt min/avg/max/mdev = ([\d.]+)/([\d.]+)", out)
        if m:
            print(f"{ip}: loss={m.group(3)}% avg={rtt.group(2) + ' ms' if rtt else '-'}")
        else:
            print(f"{ip}: FAIL\n{out}")


def cmd_arp(_):
    print(sh(f"ip neigh show dev {iface()}"))


def parse_iperf(out):
    # iperf2: "[  3]  0.0- 5.0 sec  1.20 MBytes  2.01 Mbits/sec"
    m = re.findall(r"[\d.]+\s+([\d.]+)\s+(Mbits/sec|Kbits/sec|bits/sec)", out)
    if not m:
        return None
    val, unit = m[-1]
    v = float(val)
    if unit == "Kbits/sec":
        v /= 1000.0
    elif unit == "bits/sec":
        v /= 1e6
    return v


def cmd_iperf(args):
    ip = NET + str(args[0])
    dur = str(args[1]) if len(args) > 1 else "8"
    print("== 下行 (主机 -> 板) ==")
    out = sh(f"iperf -c {ip} -t {dur} -i 2 -f m")
    print(out)
    bw = parse_iperf(out)
    print(f"   => {bw:.2f} Mbit/s" if bw else "   => 解析失败")
    print("== 上行 (板 -> 主机) ==")
    out = sh(f"iperf -c {ip} -t {dur} -i 2 -f m -R")
    bw = parse_iperf(out)
    print(out)
    print(f"   => {bw:.2f} Mbit/s" if bw else "   => 解析失败")


def cmd_udp(args):
    ip = NET + str(args[0])
    port = int(args[1]) if len(args) > 1 else 7
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(1.0)
    ok = 0
    for i in range(20):
        msg = f"hello-{i}-" + "x" * 100
        s.sendto(msg.encode(), (ip, port))
        try:
            data, _ = s.recvfrom(2048)
            if data.decode(errors="replace") == msg:
                ok += 1
        except socket.timeout:
            pass
    print(f"UDP echo {ip}:{port}: {ok}/20 成功")


def cmd_soak(args):
    ip = NET + str(args[0])
    n = int(args[1]) if len(args) > 1 else 1000
    out = sh(f"ping -c {n} -i 0.005 -W 1 {ip}")
    m = re.search(r"(\d+) packets transmitted, (\d+) received.*?([\d.]+)% packet loss", out)
    rtt = re.search(r"rtt min/avg/max/mdev = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", out)
    if m and rtt:
        print(f"{ip}: tx={m.group(1)} rx={m.group(2)} loss={m.group(3)}% "
              f"rtt min/avg/max = {rtt.group(1)}/{rtt.group(2)}/{rtt.group(3)} ms")
    else:
        print(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["plca", "ping", "arp", "iperf", "udp", "soak"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    globals()["cmd_" + a.command](a.args)


if __name__ == "__main__":
    main()
