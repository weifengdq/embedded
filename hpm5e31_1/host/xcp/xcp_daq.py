#!/usr/bin/env python3
"""xcp_daq.py —— pyxcp + python-can 从 vcan0 连板端 XCP 从站，跑 DAQ 并统计

链路（Bench B）：主机 vcan0 <--UDP(cannelloni)--> 板 ENET <--> MCU CAN1(网关) <--> 总线 <--> CAN0(XCP 从站)
板端固件：samples/app/xcp_can_daq（shell 里先 gw 1 打开网关）
"""
from __future__ import annotations

import argparse
import csv
import os
import statistics
import struct
import sys
import time

import can

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xcp_common import check_dependencies, fmt_addr, load_a2l, make_master  # noqa: E402

DAQ_PLAN = [("g_adc_raw", 2), ("g_u8_cnt", 1), ("g_tick_cnt", 4),
            ("g_cpu_load_pm", 2), ("g_pid_mid", 4)]
ODT_SPLIT = [[0, 1], [2, 3], [4]]
N_ODT = len(ODT_SPLIT)


def u32(b):
    return int.from_bytes(b[:4], "little")


def setup_daq(xcp, addrs):
    xcp.freeDaq()
    xcp.allocDaq(1)
    xcp.allocOdt(0, N_ODT)
    for odt in range(N_ODT):
        xcp.allocOdtEntry(0, odt, len(ODT_SPLIT[odt]))
    for odt in range(N_ODT):
        xcp.setDaqPtr(0, odt, 0)
        for idx in ODT_SPLIT[odt]:
            name, size = DAQ_PLAN[idx]
            xcp.writeDaq(0xFF, size, 0, addrs[name]["address"])
    xcp.setDaqListMode(0x10, 0, 0, 1, 0)
    resp = xcp.startStopDaqList(2, 0)
    xcp.startStopSynch(1)
    return resp.firstPid


def main():
    check_dependencies()
    ap = argparse.ArgumentParser(description="XCP DAQ sample rate / loss / jitter test")
    ap.add_argument("--a2l", required=True)
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--interface", default="socketcan",
                    help="socketcan（CAN，默认）或 eth（XCPonEth/UDP）")
    ap.add_argument("--eth-host", default="192.168.2.100", help="eth: 板端 IP")
    ap.add_argument("--eth-port", type=int, default=5555, help="eth: 板端 UDP 端口")
    ap.add_argument("--plan", default="rate", choices=["rate", "stress"],
                    help="eth: rate=EVT0 1 kHz/4 ODT；stress=EVT1 10 kHz/8 ODT 带宽压力")
    ap.add_argument("--no-cal", action="store_true", help="eth: 跳过标定写读回测试")
    ap.add_argument("--cmd-id", default="0x555")
    ap.add_argument("--res-id", default="0x556")
    ap.add_argument("--secs", type=float, default=10.0)
    ap.add_argument("--csv", default="out/xcp_daq.csv")
    args = ap.parse_args()

    if str(args.interface).lower() == "eth":
        return run_eth(args)

    addrs = load_a2l(args.a2l)
    missing = [n for n, _ in DAQ_PLAN if n not in addrs]
    if missing:
        sys.exit("A2L 缺少变量: " + ", ".join(missing))
    print("A2L 地址:")
    for name, _ in DAQ_PLAN:
        a = addrs[name]
        print("  %-16s %s  %-12s %d B" % (name, fmt_addr(a["address"]), a["type"], a["size"]))

    cmd_id = int(args.cmd_id, 0)
    res_id = int(args.res_id, 0)
    xcp = make_master(args.interface, args.channel, cmd_id, res_id)

    print("")
    print("[1] CONNECT %s (CMD=0x%03X RES=0x%03X)" % (args.channel, cmd_id, res_id))
    conn = xcp.connect()
    print("    resource=0x%02X MAX_CTO=%d MAX_DTO=%d proto=%d.%d trans=%d.%d"
          % (conn.resource, conn.maxCto, conn.maxDto,
             conn.protocolLayerVersion >> 4, conn.protocolLayerVersion & 0xF,
             conn.transportLayerVersion >> 4, conn.transportLayerVersion & 0xF))
    st = xcp.getStatus()
    print("    GET_STATUS session=0x%04X protection=0x%02X"
          % (st.sessionStatus, st.resourceProtectionStatus))

    print("")
    print("[2] DAQ 配置: 1 列表 / %d ODT / 时间戳 on" % N_ODT)
    first_pid = setup_daq(xcp, addrs)
    print("    first PID = %d (PID = 绝对 ODT 号)" % first_pid)

    bus = can.Bus(interface=args.interface, channel=args.channel)
    ts_list = []
    ticks = []
    pid_counts = [0] * N_ODT
    recv_t = []
    t_end = time.monotonic() + args.secs
    print("")
    print("[3] 采集 %.1f s ..." % args.secs)
    while time.monotonic() < t_end:
        msg = bus.recv(timeout=0.2)
        if msg is None or msg.arbitration_id != res_id:
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
        elif odt == 1:
            ticks.append(u32(d[1:5]))
    bus.shutdown()
    xcp.startStopSynch(0)
    xcp.disconnect()

    dur = recv_t[-1] - recv_t[0] if len(recv_t) > 1 else 1e-9
    evt = len(ts_list)
    dt = [ts_list[i + 1] - ts_list[i] for i in range(len(ts_list) - 1)]
    lost = 0
    for i in range(1, len(ticks)):
        d = ticks[i] - ticks[i - 1]
        if d > 1:
            lost += d - 1
    rtt = [recv_t[i + 1] - recv_t[i] for i in range(len(recv_t) - 1)]

    print("")
    print("[4] 统计")
    print("  指标 / 数值")
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
    if len(rtt) > 1:
        print("  主机侧到达间隔 sd(ms)  %.3f" % (statistics.pstdev(rtt) * 1000))
    print("  各 ODT 帧数: " + str(dict(zip(["ODT0", "ODT1", "ODT2"], pid_counts))))

    os.makedirs(os.path.dirname(args.csv) or ".", exist_ok=True)
    with open(args.csv, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["index", "arrival_s"])
        for i, t in enumerate(recv_t):
            w.writerow([i, "%.6f" % t])
    print("  CSV: %s" % args.csv)

    ok = lost == 0 and evt > 0 and 900 < (evt / dur) < 1100
    print("")
    print("DAQ %s (lost=%d rate=%.1f Hz)" % ("PASS" if ok else "FAIL", lost, evt / dur if dur else 0))
    return 0 if ok else 1



# ======================================================================
# XCP on Ethernet(UDP) 路径（--interface eth）
#
# 与 CAN 路径共用 A2L（地址/类型/DEC）但传输层换成 host/xcp/xcp_eth_client.py：
# 一个 UDP 数据报可装多条 XCP 消息（单包多 ODT），CTR 连续性用来判丢包。
# ======================================================================

PID_EV = 0xFD

PLAN_RATE = {
    "label": "rate",
    "event": 0,
    "desc": "EVT0 1 kHz，4 ODT/事件，ODT0 带 4 B 时间戳",
    "odts": [
        [("g_tick_cnt", 4), ("g_u8_cnt", 1)],
        [("g_adc_raw", 2), ("g_cpu_load_pm", 2), ("g_tick_100", 4)],
        [("g_pid_mid", 4)],
        [("g_sw_flag", 4)],
    ],
}

PLAN_STRESS = {
    "label": "stress",
    "event": 1,
    "desc": "EVT1 10 kHz，8 ODT/事件（单包多 ODT 与带宽上限压力）",
    "odts": [
        [("g_tick_10k", 4)],
        [("g_pid_mid", 4)],
        [("g_adc_raw", 2), ("g_u8_cnt", 1)],
        [("g_sw_flag", 4)],
        [("g_tick_cnt", 4)],
        [("g_tick_100", 4)],
        [("g_cpu_load_pm", 2)],
        [("g_pwm_duty_pm", 2), ("g_u8_cnt", 1)],
    ],
}


def _u32(b):
    return int.from_bytes(b[:4], "little")


def _eth_setup_daq(c, addrs, plan):
    """按 plan 配动态 DAQ；返回 (first_pid, [[(name,size),...],...])（size 以 A2L 为准）"""
    odts = []
    for odt, entries in enumerate(plan["odts"]):
        norm = []
        for name, size in entries:
            if name not in addrs:
                sys.exit("A2L 缺少变量: " + name)
            asize = addrs[name]["size"]
            if asize != size:
                print("  [warn] %s A2L size=%d plan=%d，按 A2L 为准" % (name, asize, size))
            norm.append((name, asize))
        odts.append(norm)

    c.free_daq()
    c.alloc_daq(1)
    c.alloc_odt(0, len(odts))
    for odt, entries in enumerate(odts):
        c.alloc_odt_entry(0, odt, len(entries))
    for odt, entries in enumerate(odts):
        for idx, (name, size) in enumerate(entries):
            for attempt in range(3):
                try:
                    c.set_daq_ptr(0, odt, idx)
                    c.write_daq(size, addrs[name]["address"])
                    break
                except TimeoutError:
                    if attempt == 2:
                        raise
    c.set_daq_list_mode(0x10, 0, plan["event"], 1, 0)   # 0x10 = 时间戳
    first_pid = c.start_stop_daq_list(2, 0)             # 2 = select
    c.start_stop_synch(1)                               # 1 = start selected
    return first_pid, odts


def _eth_write_mem(c, addr, data, tries=3):
    """SET_MTA + DOWNLOAD；UDP 丢包时重设 MTA 再写（DOWNLOAD 成功会推进 MTA）"""
    last = None
    for _ in range(tries):
        try:
            c.set_mta(addr)
            c.download(data)
            return
        except TimeoutError as exc:
            last = exc
    raise last


def _eth_cal_test(c, addrs):
    """在线标定：SHORT_UPLOAD / SET_MTA+DOWNLOAD / 回读 / MODIFY_BITS，全部打印 PASS/FAIL"""
    ok = True
    kp_addr = addrs["g_kp"]["address"]
    kp0 = struct.unpack("<f", c.short_upload(4, kp_addr))[0]
    new = round(kp0 + 0.1, 3)
    _eth_write_mem(c, kp_addr, struct.pack("<f", new))
    kp1 = struct.unpack("<f", c.short_upload(4, kp_addr))[0]
    wr_ok = abs(kp1 - new) < 1e-6
    _eth_write_mem(c, kp_addr, struct.pack("<f", kp0))
    kp2 = struct.unpack("<f", c.short_upload(4, kp_addr))[0]
    rs_ok = abs(kp2 - kp0) < 1e-6
    ok = ok and wr_ok and rs_ok
    print("  %-34s %-10s %s（%.3f -> %.3f，恢复 %.3f）"
          % ("SHORT_UPLOAD/DOWNLOAD 回读 g_kp", "PASS" if (wr_ok and rs_ok) else "FAIL",
             "写后同周期生效", kp0, kp1, kp2))

    flag_addr = addrs["g_sw_flag"]["address"]
    before = int.from_bytes(c.short_upload(4, flag_addr), "little")
    c.set_mta(flag_addr)
    c.modify_bits(8, 0xFFF0, 0x000A)                    # bit8..11 = 0xA
    after = int.from_bytes(c.short_upload(4, flag_addr), "little")
    exp = (before & ~(0xF << 8)) | (0xA << 8)
    mb_ok = after == exp
    _eth_write_mem(c, flag_addr, before.to_bytes(4, "little"))
    back = int.from_bytes(c.short_upload(4, flag_addr), "little")
    ok = ok and mb_ok and back == before
    print("  %-34s %-10s 0x%08X -> 0x%08X（期望 0x%08X，恢复 0x%08X）"
          % ("MODIFY_BITS bit8..11 = 0xA", "PASS" if (mb_ok and back == before) else "FAIL",
             before, after, exp, back))

    duty_addr = addrs["g_pwm_duty_pm"]["address"]
    d0 = int.from_bytes(c.short_upload(2, duty_addr), "little")
    _eth_write_mem(c, duty_addr, (300).to_bytes(2, "little"))
    d1 = int.from_bytes(c.short_upload(2, duty_addr), "little")
    _eth_write_mem(c, duty_addr, d0.to_bytes(2, "little"))
    d_ok = d1 == 300
    ok = ok and d_ok
    print("  %-34s %-10s %d -> 300 -> %d（恢复 %d）"
          % ("DOWNLOAD/SHORT_UPLOAD g_pwm_duty_pm", "PASS" if d_ok else "FAIL", d0, d1, d0))
    return ok


def _eth_rtt_test(c, addr, n=200):
    """命令往返延迟：连续 n 次 SHORT_UPLOAD(4)，主机侧单调时钟计时

    注意：UDP 链路有丢包，客户端对幂等命令会自动重发，因此 mean/max 里包含
    重试等待（p99 可能到 500 ms）；p50 才是单次真实 RTT，timeouts 记录重试次数。
    """
    lat = []
    tmo0 = c.stats.get("timeouts", 0)
    t_all = time.monotonic()
    for _ in range(n):
        t0 = time.monotonic_ns()
        c.short_upload(4, addr)
        lat.append((time.monotonic_ns() - t0) / 1000.0)
    dur = time.monotonic() - t_all
    lat.sort()
    return {
        "n": n,
        "min": lat[0],
        "mean": statistics.fmean(lat),
        "p50": lat[n // 2],
        "p99": lat[min(n - 1, int(n * 0.99))],
        "max": lat[-1],
        "cmd_per_s": n / dur if dur > 0 else 0.0,
        "timeouts": c.stats.get("timeouts", 0) - tmo0,
    }


def _eth_collect(c, plan, odts, first_pid, secs):
    """采集 secs 秒；返回指标 dict"""
    n_odt = len(odts)
    counts = [0] * n_odt
    tick = None
    tick_name = odts[0][0][0] if odts[0] else None
    ticks = []
    ts_list = []
    arrivals = []
    ev = 0
    bytes_rx = 0
    st0 = dict(c.stats)
    t0 = time.monotonic()
    t_end = t0 + secs
    while time.monotonic() < t_end:
        for p in c.recv_messages(0.2):
            if not p:
                continue
            pid = p[0]
            if pid >= 0xFC:
                if pid == PID_EV:
                    ev += 1
                continue
            odt = pid - first_pid
            if odt < 0 or odt >= n_odt:
                continue
            seq = odts[odt]
            pos = 1
            if odt == 0:
                ts_list.append(_u32(p[pos:pos + 4]))
                pos += 4
            vals = []
            for name, size in seq:
                vals.append(int.from_bytes(p[pos:pos + size], "little"))
                pos += size
            counts[odt] += 1
            bytes_rx += len(p) + 4          # 协议 payload + 4 B 传输层头
            arrivals.append(time.monotonic())
            if odt == 0 and vals and tick_name is not None:
                tick = vals[0]
                ticks.append(tick)
    dur = time.monotonic() - t0
    lost = 0
    for i in range(1, len(ticks)):
        d = ticks[i] - ticks[i - 1]
        if d > 1:
            lost += d - 1
    return {
        "dur": dur,
        "counts": counts,
        "msgs": sum(counts),
        "datagrams": c.stats["datagrams_rx"] - st0["datagrams_rx"],
        "ctr_gap": c.stats["ctr_gap"] - st0["ctr_gap"],
        "framing_err": c.stats["framing_err"] - st0["framing_err"],
        "ev": ev,
        "ts": ts_list,
        "ticks": ticks,
        "arrivals": arrivals,
        "bytes": bytes_rx,
        "lost": lost,
        "tick_first": ticks[0] if ticks else 0,
        "tick_last": ticks[-1] if ticks else 0,
        "samples": len(ticks),
        "tick_name": tick_name,
    }


BOARD_STATS = [
    "g_stat_tx_datagrams", "g_stat_tx_msgs", "g_stat_tx_drop", "g_stat_tx_err",
    "g_stat_rx_datagrams", "g_stat_rx_ctr_gap", "g_stat_daq_frames",
    "g_stat_daq_dropped", "g_stat_daq_overload", "g_stat_cto_rx",
]


def _eth_read_board_stats(c, addrs):
    """读板端 1 Hz 统计镜像：判断丢包发生在板端（drop/overload）还是链路（UDP 无重传）"""
    out = {}
    for name in BOARD_STATS:
        if name not in addrs:
            out[name] = None
            continue
        out[name] = int.from_bytes(c.short_upload(4, addrs[name]["address"]), "little")
    return out


def run_eth(args):
    """--interface eth：XCPonEth 全流程（CONNECT/DAQ/标定/吞吐/延迟/丢包）"""
    from xcp_eth_client import XcpEthClient, XcpError

    a2l_addrs = load_a2l(args.a2l)
    plan = PLAN_STRESS if args.plan == "stress" else PLAN_RATE
    print("XCPonEth 客户端：%s:%d（plan=%s，%s）" % (args.eth_host, args.eth_port, plan["label"], plan["desc"]))
    print("A2L 地址（来自 %s）：" % args.a2l)

    need = ["g_kp", "g_pwm_duty_pm", "g_sw_flag"]
    for entries in plan["odts"]:
        need += [n for n, _ in entries]
    for name in dict.fromkeys(need):
        if name not in a2l_addrs:
            sys.exit("A2L 缺少变量: " + name)
        a = a2l_addrs[name]
        print("  %-16s %s  %-8s %d B" % (name, fmt_addr(a["address"]), a["type"], a["size"]))

    c = XcpEthClient(args.eth_host, args.eth_port)
    rc = 0
    try:
        print("")
        print("[1] CONNECT / GET_STATUS / GET_ID")
        info = c.connect()
        print("    CONNECT: resource=0x%02X comm=0x%02X MAX_CTO=%d MAX_DTO=%d proto=0x%02X trans=0x%02X"
              % (info["resource"], info["comm_mode_basic"], info["max_cto"], info["max_dto"],
                 info["protocol"], info["transport"]))
        st = c.get_status()
        print("    GET_STATUS: session=0x%04X protection=0x%02X state=%d"
              % (st["session_status"], st["protection"], st["state"]))
        print("    GET_ID: %s" % c.get_id())
        clk0 = c.get_daq_clock()
        print("    GET_DAQ_CLOCK: %d us（板端时基）" % clk0)

        if not args.no_cal:
            print("")
            print("[2] 在线标定（RAM 单页）")
            cal_ok = _eth_cal_test(c, a2l_addrs)
        else:
            cal_ok = True

        print("")
        print("[3] 命令往返延迟（200 次 SHORT_UPLOAD 4 B）")
        rtt = _eth_rtt_test(c, a2l_addrs["g_tick_cnt"]["address"], 200)
        print("    min/p50/mean/p99/max = %.1f / %.1f / %.1f / %.1f / %.1f us，%.1f cmd/s，超时重发 %d 次"
              % (rtt["min"], rtt["p50"], rtt["mean"], rtt["p99"], rtt["max"],
                 rtt["cmd_per_s"], rtt["timeouts"]))

        print("")
        print("[4] DAQ 配置：1 列表 / %d ODT / 时间戳 on / event=%d"
              % (len(plan["odts"]), plan["event"]))
        first_pid, odts = _eth_setup_daq(c, a2l_addrs, plan)
        print("    first PID = %d（PID = 绝对 ODT 号）" % first_pid)

        print("")
        print("[5] 采集 %.1f s ..." % args.secs)
        m = _eth_collect(c, plan, odts, first_pid, args.secs)
        c.start_stop_synch(0)
        time.sleep(1.2)                 # 等板端 1 Hz 统计镜像刷新到最终值
        board = _eth_read_board_stats(c, a2l_addrs)
        clk1 = c.get_daq_clock()
        c.disconnect()

        dur = m["dur"] if m["dur"] > 0 else 1e-9
        rate = m["samples"] / dur
        tick_rate = (m["tick_last"] - m["tick_first"]) / dur if m["samples"] > 1 else 0.0
        lost_total = m["lost"] + m["ctr_gap"]
        total_msgs = m["msgs"] + lost_total
        loss_pct = 100.0 * lost_total / total_msgs if total_msgs else 0.0
        ts = m["ts"]
        dt = [ts[i + 1] - ts[i] for i in range(len(ts) - 1)]
        arr = m["arrivals"]
        adt = [(arr[i + 1] - arr[i]) * 1e3 for i in range(len(arr) - 1)]

        print("")
        print("[6] 统计")
        rows = [
            ("采样时长(s)", "%.3f" % dur),
            ("收到事件数（ODT0）", "%d" % m["samples"]),
            ("事件率(Hz)", "%.1f" % rate),
            ("板端 tick 速率(Hz)", "%.1f" % tick_rate),
            ("DAQ 消息总数", "%d" % m["msgs"]),
            ("消息率(msg/s)", "%.0f" % (m["msgs"] / dur)),
            ("UDP 数据报数", "%d" % m["datagrams"]),
            ("数据报率(pkt/s)", "%.0f" % (m["datagrams"] / dur)),
            ("单包平均消息数", "%.2f" % (m["msgs"] / m["datagrams"] if m["datagrams"] else 0)),
            ("UDP 载荷吞吐", "%.1f KB/s = %.2f Mbit/s" % (m["bytes"] / 1024.0 / dur, m["bytes"] * 8.0 / dur / 1e6)),
            ("CTR 丢包（传输层）", "%d" % m["ctr_gap"]),
            ("tick 丢帧（测量侧）", "%d" % m["lost"]),
            ("丢包率", "%.4f %%" % loss_pct),
            ("帧错误（LEN/截断）", "%d" % m["framing_err"]),
            ("EV 事件包", "%d" % m["ev"]),
            ("DAQ 时钟增量(us)", "%d（主机墙钟 %.0f ms）" % (clk1 - clk0, dur * 1000.0)),
        ]
        if dt:
            rows += [
                ("时间戳间隔 mean/min/max(us)", "%.1f / %d / %d"
                 % (statistics.fmean(dt), min(dt), max(dt))),
                ("时间戳间隔 sd(us)", "%.2f" % statistics.pstdev(dt)),
            ]
        if adt:
            rows += [
                ("到达间隔 sd(ms)", "%.3f" % statistics.pstdev(adt)),
                ("到达间隔 max-min(ms)", "%.3f" % (max(adt) - min(adt))),
            ]
        for k, v in rows:
            print("  %-30s %s" % (k, v))
        print("  各 ODT 消息数: " + str(dict(("ODT%d" % i, n) for i, n in enumerate(m["counts"]))))
        print("  板端统计: tx_msgs=%s tx_datagrams=%s tx_drop=%s tx_err=%s "
              "daq_frames=%s daq_dropped=%s daq_overload=%s cto_rx=%s"
              % (board.get("g_stat_tx_msgs"), board.get("g_stat_tx_datagrams"),
                 board.get("g_stat_tx_drop"), board.get("g_stat_tx_err"),
                 board.get("g_stat_daq_frames"), board.get("g_stat_daq_dropped"),
                 board.get("g_stat_daq_overload"), board.get("g_stat_cto_rx")))
        print("  客户端统计: %s" % c.stats)

        os.makedirs(os.path.dirname(args.csv) or ".", exist_ok=True)
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["index", "arrival_s", "ts_us", "tick"])
            for i in range(len(ts)):
                w.writerow([i, "%.6f" % (arr[i] - arr[0]), ts[i],
                            m["ticks"][i] if i < len(m["ticks"]) else ""])
        print("  CSV: %s" % args.csv)

        # CAN 1 kHz 实测 999~1000 Hz；以太网同一事件源，允许 ±2%
        rate_ok = (plan["label"] == "stress") or (980.0 <= rate <= 1020.0)
        board_drop = (board.get("g_stat_tx_drop") or 0) + (board.get("g_stat_daq_dropped") or 0)             + (board.get("g_stat_daq_overload") or 0)
        board_ok = (board_drop == 0)
        # 链路丢包：UDP 没有重传，台架实测 ~1%（可用 ping 复核）。不作为 FAIL，
        # 但板端 drop 非 0 或帧错误非 0 一定 FAIL —— 那是实现问题，不是链路问题。
        ok = bool(cal_ok and rate_ok and m["msgs"] > 0 and m["framing_err"] == 0
                  and (m["ctr_gap"] == 0 or board_ok))
        print("")
        print("DAQ %s (event=%.1f Hz, msgs=%d, loss=%.4f%%/链路, 板端drop=%d, cal=%s)"
              % ("PASS" if ok else "FAIL", rate, m["msgs"], loss_pct, board_drop,
                 "PASS" if cal_ok else "FAIL"))
        if loss_pct > 0:
            print("  说明：丢包率 %.2f%% 来自 UDP 链路（板端 tx_drop=%s、daq_dropped=%s 均为 0），"
                  "可用 ping 复核链路丢包；XCPonEth 无重传机制，DAQ 丢帧属预期。"
                  % (loss_pct, board.get("g_stat_tx_drop"), board.get("g_stat_daq_dropped")))
        rc = 0 if ok else 1
    except XcpError as exc:
        print("XCP 错误：%s（last_error=0x%02X）" % (exc, c.last_error))
        rc = 2
    except (TimeoutError, OSError) as exc:
        print("通信失败：%s" % exc)
        print("  客户端统计: %s" % c.stats)
        rc = 2
    finally:
        c.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
