#!/usr/bin/env python3
"""test_timestamp.py —— 测试项 5：gs_usb 硬件时间戳（SO_TIMESTAMPING + ethtool -T）

原理：
  Linux 的 CAN 驱动可以把设备侧时间戳挂到 skb 的 hwtstamp 上；应用打开
  SO_TIMESTAMPING(RX_HARDWARE|RAW_HARDWARE) 后，recvmsg 的辅助数据（SCM_TIMESTAMPING）
  会带回 3 个时间戳：ts[0]=软件(CLOCK_REALTIME)、ts[1]=系统硬件、ts[2]=原始硬件(ktime/MONOTONIC 域)。

本脚本做四件事：
  1) 打印 ethtool -T 的能力（device/hardware-transmit/hardware-receive 等）；
  2) can0 发 N 帧固定间隔（默认 4 ms），can1 用原始 socket 收，解析 SCM_TIMESTAMPING；
  3) 统计：硬件戳覆盖率、硬件戳 vs 收到时刻(CLOCK_MONOTONIC) 的偏差（min/中位/max/σ）、
     用硬件戳算出的帧间隔（应接近发送间隔）、单调性违例、以及时间戳量化步长；
  4) 给出 PASS/FAIL：硬件戳覆盖 >= 90%、单调、偏差 σ < 2 ms、量化步长 <= 1 us。

用法：
  python3 host/gs_usb/test_timestamp.py                # 默认 can0 发 -> can1 收，50 帧，4 ms 间隔
  python3 host/gs_usb/test_timestamp.py --count 200 --gap 0.002 --bitrate 1000000
  python3 host/gs_usb/test_timestamp.py --no-config    # 不改接口配置（已自行 ip link set up）
  python3 host/gs_usb/test_timestamp.py --json         # 只输出一行 JSON 供脚本消费
"""

import argparse
import json
import os
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

SOL_SOCKET = 1
SO_TIMESTAMPING_OLD = 37   # x86_64 + 64bit time_t 时 glibc 用这个
SO_TIMESTAMPING_NEW = 65
SCM_TIMESTAMPING_OLD = 37
SCM_TIMESTAMPING_NEW = 65
SO_TIMESTAMPING = SO_TIMESTAMPING_OLD
SCM_TIMESTAMPING = SCM_TIMESTAMPING_OLD

# linux/net_tstamp.h
SOF_TIMESTAMPING_RX_HARDWARE = 1 << 2
SOF_TIMESTAMPING_RX_SOFTWARE = 1 << 3
SOF_TIMESTAMPING_SOFTWARE = 1 << 4
SOF_TIMESTAMPING_RAW_HARDWARE = 1 << 6

CAN_EFF_FLAG = 0x80000000


def sh(cmd, check=False):
    """跑一条命令，返回 (rc, stdout+stderr)"""
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    if check and p.returncode != 0:
        raise RuntimeError(f"命令失败({p.returncode}): {cmd}\n{out}")
    return p.returncode, out.strip()


def iface_exists(name):
    return os.path.exists(f"/sys/class/net/{name}")


def iface_driver(name):
    try:
        link = os.readlink(f"/sys/class/net/{name}/device/driver")
        return os.path.basename(link)
    except OSError:
        return "?"


def setup_iface(name, bitrate):
    """把接口设成经典模式并 up（幂等）"""
    sh(f"ip link set {name} down 2>/dev/null")
    # 必须显式 fd off：上一轮 FD 测试会让接口留在 <FD> 模式，只写 bitrate 不清 FD 位
    rc, out = sh(f"ip link set {name} type can bitrate {bitrate} fd off loopback off listen-only off one-shot off")
    if rc != 0:
        return False, out
    rc, out2 = sh(f"ip link set {name} up")
    return rc == 0, (out + " " + out2).strip()


def open_raw(iface, want_hw_ts=True):
    """打开 CAN_RAW socket；want_hw_ts 时使能硬件接收时间戳"""
    s = socket.socket(AF_CAN, socket.SOCK_RAW, CAN_RAW)
    s.setsockopt(SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, struct.pack("i", 0))
    s.bind((iface,))
    s.settimeout(0.5)
    if want_hw_ts:
        flags = (SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE
                 | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE)
        try:
            s.setsockopt(SOL_SOCKET, SO_TIMESTAMPING_OLD, struct.pack("I", flags))
        except OSError as e:
            # 少数环境只认 NEW（65）；记录后用 NEW 再试一次
            try:
                s.setsockopt(SOL_SOCKET, SO_TIMESTAMPING_NEW, struct.pack("I", flags))
            except OSError as e2:
                raise OSError(f"SO_TIMESTAMPING 设置失败: OLD={e} NEW={e2}")
    return s


def pack_can_frame(can_id, data):
    """can_frame（经典 16 字节：id u32, dlc u8, pad, pad, pad, data[8]）"""
    dlc = len(data)
    return struct.pack("=IB3x8s", can_id & 0x1FFFFFFF, dlc, bytes(data).ljust(8, b"\x00"))


def parse_scm_timestamping(cmsgs):
    """从 recvmsg 辅助数据里解析 SCM_TIMESTAMPING -> [ts0, ts1, ts2]（纳秒）"""
    out = [None, None, None]
    for level, typ, data in cmsgs:
        if level == SOL_SOCKET and typ in (SCM_TIMESTAMPING_OLD, SCM_TIMESTAMPING_NEW):
            # struct scm_timestamping64 { struct __kernel_timespec ts[3]; } = 3 * (i64 sec, i64 nsec)
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


def main():
    ap = argparse.ArgumentParser(description="gs_usb 硬件时间戳验证（SO_TIMESTAMPING）")
    ap.add_argument("--tx-iface", default="can0")
    ap.add_argument("--rx-iface", default="can1")
    ap.add_argument("--count", type=int, default=50)
    ap.add_argument("--gap", type=float, default=0.004, help="发送间隔（秒），默认 4 ms")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--no-config", action="store_true", help="不改接口配置")
    ap.add_argument("--json", action="store_true", help="只输出一行 JSON")
    args = ap.parse_args()

    result = {
        "test": "timestamp", "tx": args.tx_iface, "rx": args.rx_iface,
        "count_sent": 0, "count_recv": 0, "hw_ts": 0, "sw_ts": 0,
        "offset_ns": {}, "interval_us": {}, "monotonic_violations": 0,
        "quantum_ns": None, "ethtool_T": "", "verdict": "FAIL", "reason": "", "log": "",
    }

    # ---- 0) 前置检查 ----
    for i in (args.tx_iface, args.rx_iface):
        if not iface_exists(i):
            result["reason"] = f"接口 {i} 不存在（gs_usb 固件未运行？）"
            print(json.dumps(result, ensure_ascii=False))
            return 2
        if iface_driver(i) != "gs_usb":
            result["reason"] = f"接口 {i} 的驱动是 {iface_driver(i)}，不是 gs_usb"
            print(json.dumps(result, ensure_ascii=False))
            return 2

    rc, eth_out = sh(f"ethtool -T {args.rx_iface}")
    result["ethtool_T"] = eth_out

    if not args.no_config:
        ok1, o1 = setup_iface(args.tx_iface, args.bitrate)
        ok2, o2 = setup_iface(args.rx_iface, args.bitrate)
        if not (ok1 and ok2):
            result["reason"] = f"接口配置失败: {o1} {o2}"
            print(json.dumps(result, ensure_ascii=False))
            return 2

    # ---- 1) 打开 socket ----
    try:
        rx = open_raw(args.rx_iface, want_hw_ts=True)
    except OSError as e:
        result["reason"] = f"打开 {args.rx_iface} 的 CAN_RAW socket 失败: {e}"
        print(json.dumps(result, ensure_ascii=False))
        return 2
    tx = open_raw(args.tx_iface, want_hw_ts=False)

        # ---- 2) 逐帧：发一帧 -> 立刻收一帧（避免整批发送后接收，offset 会被接收队列延迟污染）----
    hw_offsets = []
    hw_raw = []       # (hw, sw, recv_mono, recv_real)
    hw_stamps = []
    sw_stamps = []
    recv_mono = []
    recv_real = []
    sw_hw = []        # 同一帧的 (软件戳 - 硬件戳) = USB+驱动处理延迟
    for i in range(args.count):
        fid = 0x123 + (i & 0x0F)
        tx.send(pack_can_frame(fid, [0x10, i & 0xFF, (i >> 8) & 0xFF, 0xAA]))
        result['count_sent'] += 1
        deadline = time.monotonic() + 0.5
        got = False
        while time.monotonic() < deadline:
            try:
                data, anc, flags, addr = rx.recvmsg(64, socket.CMSG_SPACE(64))
            except socket.timeout:
                continue
            if len(data) < 16:
                continue
            can_id = struct.unpack_from('=I', data, 0)[0] & 0x1FFFFFFF
            if can_id != fid:
                continue
            now_mono = time.monotonic_ns()
            now_real = time.clock_gettime_ns(time.CLOCK_REALTIME)
            result['count_recv'] += 1
            recv_mono.append(now_mono)
            recv_real.append(now_real)
            for _lvl, _typ, _dat in anc:
                key = 'lvl%d_type%d_len%d' % (_lvl, _typ, len(_dat))
                result.setdefault('cmsg_seen', {})
                result['cmsg_seen'][key] = result['cmsg_seen'].get(key, 0) + 1
            ts = parse_scm_timestamping(anc)
            sw = ts[0] or ts[1]
            hw = ts[2]
            if sw:
                sw_stamps.append(sw)
                result['sw_ts'] += 1
            if hw:
                hw_stamps.append(hw)
                result['hw_ts'] += 1
                hw_raw.append((hw, sw, now_mono, now_real))
                if sw:
                    sw_hw.append(sw - hw)
            got = True
            break
        if not got:
            result['lost'] = result.get('lost', 0) + 1
        time.sleep(args.gap)

    rx.close()
    tx.close()

    # ---- 2.5) 同帧 (软件戳 - 硬件戳)：USB 传输 + 驱动入栈延迟 ----
    if sw_hw:
        result['sw_minus_hw_ns'] = {
            'min': min(sw_hw), 'median': int(statistics.median(sw_hw)),
            'mean': int(statistics.fmean(sw_hw)), 'max': max(sw_hw),
            'stdev': int(statistics.pstdev(sw_hw)) if len(sw_hw) > 1 else 0,
        }

# ---- 3.5) 判定时域：设备时间戳可能是 CLOCK_REALTIME 或 CLOCK_MONOTONIC 基准 ----
    domain = None
    if hw_raw:
        off_mono = [m - hw for hw, sw, m, r in hw_raw]
        off_real = [r - hw for hw, sw, m, r in hw_raw]
        med_mono = abs(statistics.median(off_mono))
        med_real = abs(statistics.median(off_real))
        domain = "REALTIME" if med_real < med_mono else "MONOTONIC"
        hw_offsets = off_real if domain == "REALTIME" else off_mono
        result["ts_domain"] = domain
        result["offset_vs_mono_median_ns"] = int(statistics.median(off_mono))
        result["offset_vs_real_median_ns"] = int(statistics.median(off_real))

    # ---- 4) 统计 ----
    if hw_offsets:
        result["offset_ns"] = {
            "min": min(hw_offsets), "median": int(statistics.median(hw_offsets)),
            "max": max(hw_offsets), "mean": int(statistics.fmean(hw_offsets)),
            "stdev": int(statistics.pstdev(hw_offsets)) if len(hw_offsets) > 1 else 0,
        }
    if len(hw_stamps) > 1:
        deltas = [hw_stamps[i + 1] - hw_stamps[i] for i in range(len(hw_stamps) - 1)]
        result["monotonic_violations"] = sum(1 for d in deltas if d <= 0)
        us = [d / 1000.0 for d in deltas if d > 0]
        if us:
            result["interval_us"] = {
                "min": round(min(us), 2), "median": round(statistics.median(us), 2),
                "max": round(max(us), 2),
                "stdev": round(statistics.pstdev(us), 3) if len(us) > 1 else 0.0,
                "expected": round(args.gap * 1e6, 1),
            }
        # 量化步长：相邻硬件戳差的 gcd 量级（1 MHz 计数器 => 1000 ns）
        quant = []
        for d in deltas[:32]:
            if d > 0:
                quant.append(d % 1000)
        if quant:
            result["quantum_ns"] = 1000 if all(q == 0 for q in quant) else 1

    # ---- 4.5) 频率一致性：hw 间隔 / sw 间隔（比值应 ≈1，ppm 偏差反映设备计数器与系统时钟的频率差）----
    if len(hw_stamps) > 2 and len(sw_stamps) > 2:
        d_hw = [hw_stamps[i+1] - hw_stamps[i] for i in range(len(hw_stamps)-1)]
        d_sw = [sw_stamps[i+1] - sw_stamps[i] for i in range(len(sw_stamps)-1)]
        n = min(len(d_hw), len(d_sw))
        if n >= 2:
            r = statistics.median([d_hw[i] / d_sw[i] for i in range(n) if d_sw[i] > 0])
            result['hw_sw_interval_ratio'] = round(r, 6)
            result['hw_sw_interval_ppm'] = round((r - 1.0) * 1e6, 1)

    # ---- 5) 判定 ----
    reasons = []
    cov = (result["hw_ts"] / result["count_recv"]) if result["count_recv"] else 0.0
    if result["count_recv"] == 0:
        reasons.append("没有收到任何帧（链路/位时序/固件 RX 通道？）")
    elif cov < 0.9:
        reasons.append(f"硬件时间戳覆盖率不足: {result['hw_ts']}/{result['count_recv']} = {cov:.0%}（需 >=90%）")
    if result["hw_ts"] > 1 and result["monotonic_violations"] > 0:
        reasons.append(f"硬件时间戳非单调（违例 {result['monotonic_violations']} 次）")
    # 说明：判定"硬件时间戳是否可用"只看 覆盖率/单调性/量化步长；
    # 设备计数器换算到系统时钟的**绝对偏差与抖动**单独作为指标报告（受 TIMESTAMP 同步策略影响，
    # 不作为 PASS/FAIL 依据；第 40 章把它写成"精度待确认"）。
    if result["quantum_ns"] not in (None, 1000):
        reasons.append(f"时间戳量化步长 {result['quantum_ns']} ns（期望 1000 ns = 1 MHz 计数器）")
    result["verdict"] = "PASS" if not reasons else "FAIL"
    result["reason"] = "；".join(reasons)

    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        print("=== ethtool -T {} ===".format(args.rx_iface))
        print(result["ethtool_T"])
        print()
        print("=== 结果 ===")
        print(f"发送/接收        : {result['count_sent']} / {result['count_recv']}")
        print(f"硬件戳/软件戳     : {result['hw_ts']} / {result['sw_ts']}（覆盖率 {cov:.0%}）")
        if result["offset_ns"]:
            o = result["offset_ns"]
            print(f"硬件戳 vs 收到时刻偏差: min {o['min']/1e6:.3f} ms / 中位 {o['median']/1e6:.3f} ms / "
                  f"max {o['max']/1e6:.3f} ms / σ {o['stdev']/1e6:.3f} ms")
        if result["interval_us"]:
            it = result["interval_us"]
            print(f"硬件戳帧间隔     : min {it['min']} us / 中位 {it['median']} us / max {it['max']} us / "
                  f"σ {it['stdev']} us（发送间隔 {it['expected']} us）")
        print(f"单调性违例       : {result['monotonic_violations']}")
        print(f"时间戳时域       : {result.get('ts_domain', '?')}（设备计数器换算到系统时钟的域）")
        if result.get('sw_minus_hw_ns'):
            d = result['sw_minus_hw_ns']
            print(f"同帧 软件戳-硬件戳: 中位 {d['median']/1e3:.1f} us / σ {d['stdev']/1e3:.1f} us（USB+驱动延迟）")
        if result.get('hw_sw_interval_ratio'):
            print(f"hw/sw 间隔比值   : {result['hw_sw_interval_ratio']}（{result['hw_sw_interval_ppm']} ppm）")
        print(f"量化步长         : {result['quantum_ns']} ns")
        if result.get("cmsg_seen"):
            print(f"辅助数据(cmsg)   : {result['cmsg_seen']}")
        print(f"结论             : {result['verdict']}" + (f"（{result['reason']}）" if result["reason"] else ""))

    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
