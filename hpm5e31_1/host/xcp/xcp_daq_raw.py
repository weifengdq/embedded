#!/usr/bin/env python3
"""xcp_daq_raw.py —— 用自写 XCP 客户端（python-can）在 vcan0 上跑 DAQ 并统计

链路（Bench B）：主机 vcan0 <--cannelloni/UDP--> 板 CAN1(网关) <--> 总线 <--> CAN0(XCP 从站)
板端：samples/app/xcp_can_daq，shell 里 gw on 打开网关。

用法：
  python3 host/xcp/xcp_daq_raw.py --a2l samples/app/xcp_can_daq/xcp_can_daq.a2l --secs 8

输出：CONNECT/GET_STATUS/GET_ID、DAQ 配置结果、采样率/丢帧/时间戳抖动/端到端时延、CSV。
"""
from __future__ import annotations

import argparse
import csv
import os
import statistics
import sys
import time

import can

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xcp_client import XcpClient  # noqa: E402
from xcp_common import fmt_addr, load_a2l  # noqa: E402

# 与固件/README 的 ODT 计划一致（3 ODT，ODT0 带时间戳）
DAQ_PLAN = [("g_adc_raw", 2), ("g_u8_cnt", 1), ("g_tick_cnt", 4),
            ("g_cpu_load_pm", 2), ("g_pid_mid", 4)]
ODT_SPLIT = [[0, 1], [2, 3], [4]]
N_ODT = len(ODT_SPLIT)


def u32(b):
    return int.from_bytes(b[:4], "little")


def main():
    ap = argparse.ArgumentParser(description="XCP DAQ over vcan0 (自写客户端)")
    ap.add_argument("--a2l", required=True)
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--cmd-id", default="0x555")
    ap.add_argument("--res-id", default="0x556")
    ap.add_argument("--secs", type=float, default=8.0)
    ap.add_argument("--csv", default="/tmp/xcp_daq_raw.csv")
    args = ap.parse_args()

    addrs = load_a2l(args.a2l)
    missing = [n for n, _ in DAQ_PLAN if n not in addrs]
    if missing:
        sys.exit("A2L 缺少变量: " + ", ".join(missing))
    print("A2L 地址:")
    for name, _ in DAQ_PLAN:
        a = addrs[name]
        print("  %-16s %s  %-8s %d B" % (name, fmt_addr(a["address"]), a["type"], a["size"]))

    xcp = XcpClient(args.channel, int(args.cmd_id, 0), int(args.res_id, 0))
    print("\n[1] CONNECT %s (CMD=0x%03X RES=0x%03X)"
          % (args.channel, int(args.cmd_id, 0), int(args.res_id, 0)))
    info = xcp.connect()
    print("    resource=0x%02X commBasic=0x%02X MAX_CTO=%d MAX_DTO=%d proto=0x%02X trans=0x%02X"
          % (info["resource"], info["comm_mode_basic"], info["max_cto"], info["max_dto"],
             info["protocol"], info["transport"]))
    st = xcp.get_status()
    print("    GET_STATUS session=0x%04X protection=0x%02X state=%d"
          % (st["session_status"], st["protection"], st["state"]))
    print("    GET_ID: %s" % xcp.get_id())
    print("    GET_DAQ_CLOCK = %d us" % xcp.get_daq_clock())

    print("\n[2] DAQ 配置（动态列表：1 列表 / %d ODT / 时间戳）" % N_ODT)
    xcp.free_daq()
    xcp.alloc_daq(1)
    xcp.alloc_odt(0, N_ODT)
    for odt in range(N_ODT):
        xcp.alloc_odt_entry(0, odt, len(ODT_SPLIT[odt]))
    for odt in range(N_ODT):
        xcp.set_daq_ptr(0, odt, 0)
        for idx in ODT_SPLIT[odt]:
            name, size = DAQ_PLAN[idx]
            xcp.write_daq(size, addrs[name]["address"])
    xcp.set_daq_list_mode(0x10, 0, 0, 1, 0)
    first_pid = xcp.start_stop_daq_list(2, 0)
    print("    first PID = %d（PID = 绝对 ODT 号）" % first_pid)

    xcp.start_stop_synch(1)
    print("\n[3] 采集 %.1f s ..." % args.secs)
    ts_list, ticks, recv_t, recv_t0 = [], [], [], []
    pid_counts = [0] * N_ODT
    t_end = time.monotonic() + args.secs
    while time.monotonic() < t_end:
        msg = xcp.recv_raw(0.2)
        if msg is None or msg.arbitration_id != xcp.res_id:
            continue
        d = bytes(msg.data)
        if (not d) or d[0] >= 0xFC:
            continue
        odt = d[0] - first_pid
        if odt < 0 or odt >= N_ODT:
            continue
        pid_counts[odt] += 1
        recv_t.append(msg.timestamp)
        if odt == 0:
            ts_list.append(u32(d[1:5]))
            recv_t0.append(msg.timestamp)
        elif odt == 1:
            ticks.append(u32(d[1:5]))
    xcp.start_stop_synch(0)
    xcp.disconnect()
    xcp.close()

    dur = recv_t[-1] - recv_t[0] if len(recv_t) > 1 else 1e-9
    evt = len(ts_list)
    dt = [ts_list[i + 1] - ts_list[i] for i in range(len(ts_list) - 1)]
    lost = 0
    for i in range(1, len(ticks)):
        d = ticks[i] - ticks[i - 1]
        if d > 1:
            lost += d - 1
    # 端到端时延：只对 ODT0（带时间戳的帧）逐帧配对，时延 = 主机到达 - 从站采样时刻
    # 绝对偏置含两台设备时基差与网关 UDP 组包时延，因此报"相对均值"的抖动与范围更有意义。
    lat = []
    if len(ts_list) > 1 and len(recv_t0) == len(ts_list):
        t0_arr, t0_ts = recv_t0[0], ts_list[0]
        for i in range(len(ts_list)):
            lat.append((recv_t0[i] - t0_arr) * 1e6 - (ts_list[i] - t0_ts))
    jit = [lat[i] - statistics.fmean(lat) for i in range(len(lat))] if lat else []

    print("\n[4] 统计")
    print("  采样时长(s)            %.3f" % dur)
    print("  list0 事件数           %d" % evt)
    print("  实测事件率(Hz)         %.1f" % (evt / dur if dur > 0 else 0))
    print("  DAQ 帧总数             %d" % len(recv_t))
    print("  帧率(frame/s)          %.0f" % (len(recv_t) / dur if dur > 0 else 0))
    print("  事件丢失(按 tick 计数) %d" % lost)
    if dt:
        print("  时间戳间隔 mean(us)    %.1f" % statistics.fmean(dt))
        print("  时间戳间隔 min/max(us) %d / %d" % (min(dt), max(dt)))
        if len(dt) > 1:
            print("  时间戳间隔 sd(us)      %.2f" % statistics.pstdev(dt))
    if lat:
        print("  端到端时延 mean(us)    %.0f" % statistics.fmean(lat))
        print("  时延抖动 sd(us)        %.0f" % (statistics.pstdev(lat) if len(lat) > 1 else 0))
        print("  时延 max-min(us)       %.0f" % (max(lat) - min(lat)))
    print("  各 ODT 帧数: %s" % dict(zip(["ODT0", "ODT1", "ODT2"], pid_counts)))

    os.makedirs(os.path.dirname(args.csv) or ".", exist_ok=True)
    with open(args.csv, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["index", "arrival_s", "timestamp_us", "latency_us"])
        for i in range(len(recv_t)):
            w.writerow([i, "%.6f" % recv_t[i],
                        ts_list[i] if i < len(ts_list) else "",
                        "%.1f" % lat[i] if i < len(lat) else ""])
    print("  CSV: %s" % args.csv)

    ok = lost == 0 and evt > 0 and 900 < (evt / dur) < 1100
    print("\nDAQ %s (lost=%d rate=%.1f Hz)" % ("PASS" if ok else "FAIL", lost, evt / dur if dur else 0))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
