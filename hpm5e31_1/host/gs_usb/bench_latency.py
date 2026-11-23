#!/usr/bin/env python3
"""bench_latency.py —— gs_usb 单帧延迟 + 经典帧吞吐（供第 40 章 fig-40-5）

两件事：
  1) **单帧延迟**：can0 写一帧 -> can1 收到，测 Δt = t_recv(CLOCK_MONOTONIC) - t_write。
     这个值包含：主机 USB 提交 -> 设备 USB 收 -> CAN 控制器发送 -> 总线 -> 对端 CAN 控制器 ->
     设备 USB 上传 -> 主机唤醒。是"USB-CAN 适配器端到端"的实测延迟（不是纯总线延迟）。
  2) **吞吐**：用 cangen 满速打 5 s，统计 can0 TX / can1 RX 帧数与帧率、丢帧率、总线错误计数。

用法：
  sudo -n python3 host/gs_usb/bench_latency.py                 # 默认：延迟 200 帧 + 吞吐 5 s
  sudo -n python3 host/gs_usb/bench_latency.py --lat-only
  sudo -n python3 host/gs_usb/bench_latency.py --thr-only --duration 3
  sudo -n python3 host/gs_usb/bench_latency.py --json           # 单行 JSON，便于给图脚本消费

注意：需要 root（CAN_RAW socket + ip link 配置）；脚本自己会 up/down 接口并设置 fd off。
"""

import argparse
import json
import socket
import statistics
import struct
import subprocess
import sys
import time

AF_CAN = 29
CAN_RAW = 1
SOL_CAN_RAW = 101
CAN_RAW_RECV_OWN_MSGS = 5


def sh(cmd, check=False):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    if check and p.returncode != 0:
        raise RuntimeError(f"命令失败({p.returncode}): {cmd}\n{out}")
    return p.returncode, out.strip()


def iface_up(name, bitrate):
    s = f"ip link set {name} down 2>/dev/null; "
    s += f"ip link set {name} type can bitrate {bitrate} fd off loopback off listen-only off one-shot off; "
    s += f"ip link set {name} up"
    return sh(s)


def open_raw(iface):
    sock = socket.socket(AF_CAN, socket.SOCK_RAW, CAN_RAW)
    sock.setsockopt(SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, struct.pack("i", 0))
    sock.bind((iface,))
    sock.settimeout(0.5)
    return sock


def pack_frame(can_id, data):
    return struct.pack("=IB3x8s", can_id & 0x1FFFFFFF, len(data), bytes(data).ljust(8, b"\x00"))


def read_link_stats(iface):
    """返回 (rx_packets, tx_packets, rx_errors, tx_errors, rx_dropped, tx_dropped)"""
    rc, out = sh(f"ip -s link show {iface}")
    stats = {"rx_packets": 0, "tx_packets": 0, "rx_errors": 0, "tx_errors": 0,
             "rx_dropped": 0, "tx_dropped": 0}
    if rc != 0:
        return stats
    lines = out.splitlines()
    for i, ln in enumerate(lines):
        if ln.strip().startswith("RX:") and i + 1 < len(lines):
            nums = lines[i + 1].split()
            if len(nums) >= 6:
                stats["rx_packets"] = int(nums[1]); stats["rx_errors"] = int(nums[2]); stats["rx_dropped"] = int(nums[3])
        if ln.strip().startswith("TX:") and i + 1 < len(lines):
            nums = lines[i + 1].split()
            if len(nums) >= 6:
                stats["tx_packets"] = int(nums[1]); stats["tx_errors"] = int(nums[2]); stats["tx_dropped"] = int(nums[3])
    return stats


def latency(tx_iface, rx_iface, count, gap):
    tx = open_raw(tx_iface)
    rx = open_raw(rx_iface)
    # 先清空可能残留的接收队列
    rx.settimeout(0.01)
    try:
        while True:
            rx.recv(16)
    except (socket.timeout, BlockingIOError):
        pass
    rx.settimeout(0.5)
    deltas = []
    lost = 0
    for i in range(count):
        fid = 0x300 + (i & 0x0F)
        t0 = time.monotonic_ns()
        tx.send(pack_frame(fid, [i & 0xFF, (i >> 8) & 0xFF, 0x5A, 0xA5]))
        deadline = time.monotonic() + 0.2
        got = False
        while time.monotonic() < deadline:
            try:
                data = rx.recv(16)
            except socket.timeout:
                continue
            if len(data) >= 16:
                can_id = struct.unpack_from("=I", data, 0)[0] & 0x1FFFFFFF
                if can_id == fid:
                    deltas.append(time.monotonic_ns() - t0)
                    got = True
                    break
        if not got:
            lost += 1
        time.sleep(gap)
    tx.close(); rx.close()
    us = [d / 1000.0 for d in deltas]
    return {
        "count": count, "recv": len(us), "lost": lost,
        "min_us": round(min(us), 1) if us else None,
        "median_us": round(statistics.median(us), 1) if us else None,
        "mean_us": round(statistics.fmean(us), 1) if us else None,
        "max_us": round(max(us), 1) if us else None,
        "stdev_us": round(statistics.pstdev(us), 1) if len(us) > 1 else None,
        "p99_us": round(sorted(us)[int(len(us) * 0.99) - 1], 1) if len(us) > 2 else None,
    }


def throughput(tx_iface, rx_iface, duration, bitrate):
    before_tx = read_link_stats(tx_iface)
    before_rx = read_link_stats(rx_iface)
    t0 = time.monotonic()
    # cangen: 1 ms 间隔（约 1000 帧/s），用 timeout 限制时长；-x 关闭本地回环避免自收
    sh(f"timeout {duration} cangen {tx_iface} -g 1 -I r -D r -x")
    elapsed = time.monotonic() - t0
    time.sleep(0.3)
    after_tx = read_link_stats(tx_iface)
    after_rx = read_link_stats(rx_iface)
    d_tx = after_tx["tx_packets"] - before_tx["tx_packets"]
    d_rx = after_rx["rx_packets"] - before_rx["rx_packets"]
    return {
        "duration_s": round(elapsed, 2),
        "tx_frames": d_tx, "rx_frames": d_rx,
        "tx_fps": round(d_tx / elapsed, 1) if elapsed > 0 else 0,
        "rx_fps": round(d_rx / elapsed, 1) if elapsed > 0 else 0,
        "loss_pct": round((d_tx - d_rx) * 100.0 / d_tx, 3) if d_tx else None,
        "tx_errors": after_tx["tx_errors"] - before_tx["tx_errors"],
        "rx_errors": after_rx["rx_errors"] - before_rx["rx_errors"],
        "tx_dropped": after_tx["tx_dropped"] - before_tx["tx_dropped"],
        "rx_dropped": after_rx["rx_dropped"] - before_rx["rx_dropped"],
        "frame_bytes": 8,
        "payload_bitrate_kbps": round(d_tx * (8 + 47) * 8 / elapsed / 1000.0, 1) if elapsed > 0 else 0,
        "bus_load_pct": round(d_tx * (47 + 64) / elapsed / bitrate * 100.0, 2) if elapsed > 0 else 0,
    }


def main():
    ap = argparse.ArgumentParser(description="gs_usb 单帧延迟 + 吞吐测试")
    ap.add_argument("--tx-iface", default="can0")
    ap.add_argument("--rx-iface", default="can1")
    ap.add_argument("--count", type=int, default=200, help="延迟测试帧数")
    ap.add_argument("--gap", type=float, default=0.002, help="延迟测试帧间隔（秒）")
    ap.add_argument("--duration", type=float, default=5.0, help="吞吐测试时长（秒）")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--lat-only", action="store_true")
    ap.add_argument("--thr-only", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    out = {"test": "bench", "bitrate": args.bitrate, "lat": None, "thr": None, "verdict": "FAIL"}

    for i in (args.tx_iface, args.rx_iface):
        if not __import__("os").path.exists(f"/sys/class/net/{i}"):
            out["reason"] = f"接口 {i} 不存在"
            print(json.dumps(out, ensure_ascii=False))
            return 2
    iface_up(args.tx_iface, args.bitrate)
    iface_up(args.rx_iface, args.bitrate)

    if not args.thr_only:
        out["lat"] = latency(args.tx_iface, args.rx_iface, args.count, args.gap)
    if not args.lat_only:
        out["thr"] = throughput(args.tx_iface, args.rx_iface, args.duration, args.bitrate)

    ok = True
    if out["lat"]:
        L = out["lat"]
        med = L["median_us"] if L["median_us"] is not None else 1e9
        if L["recv"] < args.count * 0.9 or med > 20000:
            ok = False
    if out["thr"]:
        T = out["thr"]
        loss = T["loss_pct"] if T["loss_pct"] is not None else 100.0
        if T["tx_frames"] <= 0 or loss > 5.0:
            ok = False
    out["verdict"] = "PASS" if ok else "FAIL"

    if args.json:
        print(json.dumps(out, ensure_ascii=False))
    else:
        print("=== gs_usb 单帧延迟（%s -> %s，%d 帧）===" % (args.tx_iface, args.rx_iface, args.count))
        if out["lat"]:
            L = out["lat"]
            print(f"收到/发送      : {L['recv']}/{L['count']}（丢 {L['lost']}）")
            print(f"延迟 min/中位/mean/max/p99 : {L['min_us']} / {L['median_us']} / {L['mean_us']} / {L['max_us']} / {L['p99_us']} us")
            print(f"抖动 σ         : {L['stdev_us']} us")
        print()
        print("=== 经典帧吞吐（cangen -g 1，%.1f s）===" % args.duration)
        if out["thr"]:
            T = out["thr"]
            print(f"tx/rx 帧数     : {T['tx_frames']} / {T['rx_frames']}（{T['duration_s']} s）")
            print(f"帧率           : tx {T['tx_fps']} fps，rx {T['rx_fps']} fps")
            print(f"丢帧率         : {T['loss_pct']} %")
            print(f"错误/丢弃      : tx_err={T['tx_errors']} rx_err={T['rx_errors']} tx_drop={T['tx_dropped']} rx_drop={T['rx_dropped']}")
            print(f"有效载荷带宽   : {T['payload_bitrate_kbps']} kbit/s；总线负载约 {T['bus_load_pct']} % @ {args.bitrate} bit/s")
        print(f"结论           : {out['verdict']}")
    return 0 if out["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
