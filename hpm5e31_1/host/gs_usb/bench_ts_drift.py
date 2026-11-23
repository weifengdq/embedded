#!/usr/bin/env python3
"""bench_ts_drift.py —— 硬件时间戳绝对精度/频率偏差分段采样与线性拟合（U-40-20）

背景：gs_usb 的硬件时间戳来自设备 1 MHz 计数器，内核用 CYCLECOUNTER 把它扩展到
64 位并锚定到 REALTIME。设备晶振与主机时钟存在频率差，表现为"同帧（主机软件戳 −
设备硬件戳）"随时间线性漂移。单窗口内只能量比值（test_timestamp.py 的 hw/sw
interval ratio）；要做绝对精度评估，需要跨多个窗口采"偏移 vs 时间"再线性拟合。

本脚本两种模式：
  采样：python3 bench_ts_drift.py --secs 50 --append /tmp/ts_drift.csv
        can0 每 20 ms 发一帧，can1 原始 socket 收，记录每帧 (sw_ts, hw_ts)；
        输出本段统计（覆盖率、同帧偏移 min/中位/max/σ、段内拟合 ppm），并追加一行 CSV。
  拟合：python3 bench_ts_drift.py --fit /tmp/ts_drift.csv
        对各段中位偏移 vs 段中点时刻做最小二乘，给出整体频率偏差（ppm）、
        截距（固定延迟）与 R²。

判据（供第 40 章引用，不作为硬 FAIL）：
  * 偏移随时间的斜率即设备 vs 主机的频率差；|ppm| 越小越好，本项目 R4 单窗口量到 -40.7 ppm；
  * 固定延迟（截距）主要是 USB 上传 + 驱动入栈的 60~80 µs，与时间戳读数本身无关。
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
SOL_SOCKET = 1
SO_TIMESTAMPING = 37
SCM_TIMESTAMPING = 37
SOF_TIMESTAMPING_RX_HARDWARE = 1 << 2
SOF_TIMESTAMPING_RX_SOFTWARE = 1 << 3
SOF_TIMESTAMPING_SOFTWARE = 1 << 4
SOF_TIMESTAMPING_RAW_HARDWARE = 1 << 6
CAN_EFF_MASK = 0x1FFFFFFF


def sh(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def iface_ok(name):
    import os
    return os.path.isdir(f"/sys/class/net/{name}")


def setup_iface(name, bitrate):
    sh(f"ip link set {name} down 2>/dev/null")
    rc, out = sh(f"ip link set {name} type can bitrate {bitrate} fd off loopback off "
                 f"listen-only off one-shot off")
    if rc != 0:
        return False, out
    rc, out2 = sh(f"ip link set {name} up")
    return rc == 0, (out + " " + out2).strip()


def open_raw(iface, hw_ts):
    s = socket.socket(AF_CAN, socket.SOCK_RAW, CAN_RAW)
    s.bind((iface,))
    s.settimeout(0.5)
    if hw_ts:
        flags = (SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE
                 | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE)
        s.setsockopt(SOL_SOCKET, SO_TIMESTAMPING, struct.pack("I", flags))
    return s


def pack_frame(can_id, data):
    return struct.pack("=IB3x8s", can_id & CAN_EFF_MASK, len(data), bytes(data).ljust(8, b"\x00"))


def parse_ts(cmsgs):
    out = [None, None, None]
    for level, typ, data in cmsgs:
        if level == SOL_SOCKET and typ in (SCM_TIMESTAMPING, 65):
            if len(data) >= 48:
                vals = struct.unpack("=6q", data[:48])
                for i in range(3):
                    sec, nsec = vals[2 * i], vals[2 * i + 1]
                    out[i] = sec * 1_000_000_000 + nsec if (sec or nsec) else None
            elif len(data) >= 24:
                vals = struct.unpack("=6i", data[:24])
                for i in range(3):
                    sec, nsec = vals[2 * i], vals[2 * i + 1]
                    out[i] = sec * 1_000_000_000 + nsec if (sec or nsec) else None
    return out


def linfit(xs, ys):
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx if sxx else 0.0
    intercept = my - slope * mx
    ss_tot = sum((y - my) ** 2 for y in ys)
    ss_res = sum((y - (slope * x + intercept)) ** 2 for x, y in zip(xs, ys))
    r2 = 1.0 - ss_res / ss_tot if ss_tot else 1.0
    return slope, intercept, r2


def sample(args):
    res = {"test": "ts_drift", "status": "FAIL", "frames": 0, "hw_cov": 0.0}
    for i in (args.tx, args.rx):
        if not iface_ok(i):
            res["reason"] = f"接口 {i} 不存在"
            print(json.dumps(res, ensure_ascii=False))
            return 2
    if not args.no_config:
        ok1, o1 = setup_iface(args.tx, args.bitrate)
        ok2, o2 = setup_iface(args.rx, args.bitrate)
        if not (ok1 and ok2):
            res["reason"] = f"接口配置失败: {o1} {o2}"
            print(json.dumps(res, ensure_ascii=False))
            return 2
    try:
        rx = open_raw(args.rx, True)
        tx = open_raw(args.tx, False)
    except OSError as e:
        res["reason"] = f"打开 socket 失败: {e}"
        print(json.dumps(res, ensure_ascii=False))
        return 2

    start = time.clock_gettime_ns(time.CLOCK_REALTIME)
    deadline = time.monotonic() + args.secs
    pairs = []      # (sw, hw)
    sent = lost = 0
    i = 0
    while time.monotonic() < deadline:
        fid = 0x300 + (i & 0x0F)
        tx.send(pack_frame(fid, [i & 0xFF, (i >> 8) & 0xFF, 0xA5]))
        sent += 1
        i += 1
        t_end = time.monotonic() + 0.2
        got = False
        while time.monotonic() < t_end:
            try:
                data, anc, flags, addr = rx.recvmsg(64, socket.CMSG_SPACE(64))
            except socket.timeout:
                continue
            if len(data) < 16:
                continue
            if (struct.unpack_from("=I", data, 0)[0] & CAN_EFF_MASK) != fid:
                continue
            ts = parse_ts(anc)
            sw = ts[0] or ts[1]
            hw = ts[2]
            if sw and hw:
                pairs.append((sw, hw))
            got = True
            break
        if not got:
            lost += 1
        time.sleep(args.gap)
    end = time.clock_gettime_ns(time.CLOCK_REALTIME)
    rx.close()
    tx.close()

    if not pairs:
        res["reason"] = "没有拿到（软件戳, 硬件戳）对——硬件时间戳未生效？"
        print(json.dumps(res, ensure_ascii=False))
        return 2
    offs = [hw - sw for sw, hw in pairs]
    t0 = start
    xs = [(sw - t0) / 1e9 for sw, hw in pairs]
    ys = [o / 1e9 for o in offs]          # 秒
    slope, intercept, r2 = linfit(xs, ys)
    res.update({
        "status": "OK",
        "secs": round((end - start) / 1e9, 1),
        "sent": sent, "lost": lost,
        "frames": len(pairs),
        "hw_cov": round(len(pairs) / sent, 4) if sent else 0.0,
        "med_offset_us": round(statistics.median(offs) / 1000.0, 1),
        "min_offset_us": round(min(offs) / 1000.0, 1),
        "max_offset_us": round(max(offs) / 1000.0, 1),
        "stdev_offset_us": round(statistics.pstdev(offs) / 1000.0, 1) if len(offs) > 1 else 0.0,
        "ppm_in_seg": round(slope * 1e6, 1),
        "r2": round(r2, 4),
    })
    if args.append:
        mid = (start + end) // 2
        with open(args.append, "a", encoding="utf-8") as f:
            f.write(f"{start},{end},{mid},{len(pairs)},{res['hw_cov']},"
                    f"{round(statistics.median(offs))},{res['ppm_in_seg']}\n")
        res["appended"] = args.append
    print(json.dumps(res, ensure_ascii=False))
    return 0


def fit(args):
    rows = []
    with open(args.fit, encoding="utf-8") as f:
        for line in f:
            p = line.strip().split(",")
            if len(p) != 7:
                continue
            rows.append([float(x) for x in p])
    if len(rows) < 3:
        print(json.dumps({"status": "FAIL", "reason": f"有效段数 {len(rows)} < 3"}, ensure_ascii=False))
        return 2
    t_ref = rows[0][2]
    xs = [(r[2] - t_ref) / 1e9 for r in rows]
    ys = [r[5] / 1e9 for r in rows]
    slope, intercept, r2 = linfit(xs, ys)
    out = {
        "status": "OK",
        "segments": len(rows),
        "span_s": round(xs[-1] - xs[0], 1),
        "frames_total": int(sum(r[3] for r in rows)),
        "ppm": round(slope * 1e6, 1),
        "intercept_us": round(intercept * 1e6, 1),
        "r2": round(r2, 4),
        "med_offset_us": [round(r[5] / 1000.0, 1) for r in rows],
        "seg_ppm": [r[6] for r in rows],
    }
    print(json.dumps(out, ensure_ascii=False))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tx", default="can0")
    ap.add_argument("--rx", default="can1")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--secs", type=float, default=50.0)
    ap.add_argument("--gap", type=float, default=0.02, help="发送间隔秒，默认 20 ms")
    ap.add_argument("--no-config", action="store_true")
    ap.add_argument("--append", default="")
    ap.add_argument("--fit", default="")
    args = ap.parse_args()
    if args.fit:
        return fit(args)
    return sample(args)


if __name__ == "__main__":
    sys.exit(main())
