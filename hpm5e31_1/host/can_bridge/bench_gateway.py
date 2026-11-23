#!/usr/bin/env python3
"""
bench_gateway.py - can_gateway 主机侧验收（python-can + vcan0）

在板端固件 samples/app/can_gateway 运行、主机 vcan0 + cannelloni 隧道建立后运行：
  ./run_tunnel.sh &            # 或 ./udp2vcan.py &
  ./bench_gateway.py           # 默认跑 心跳/往返延迟/丢包 三项

三项测试（都通过才 PASS）：
  1) heartbeat : 2 s 内统计 0x321 心跳（载荷 CA FE BA BE），板端 10 Hz -> 期望 20 帧
  2) latency   : 200 次 0x123 往返（CAN0 回显 0x321），报 avg/p50/p95/p99/max
  3) loss      : 300 帧/s 连续 10 s（3000 帧），统计回显丢失率

阈值可用参数改：--lat-p99-ms、--max-loss-pct、--hb-min。
"""
import argparse
import queue
import statistics
import sys
import threading
import time

try:
    import can
except ImportError:
    sys.exit("缺少 python-can：pip install python-can")

ID_REQ = 0x123
ID_RES = 0x321
HB_MARK = bytes([0xCA, 0xFE, 0xBA, 0xBE])


def seq_payload(i):
    return int(i & 0xFFFFFFFF).to_bytes(4, "little") + bytes([0x11, 0x22, 0x33, 0x44])


class Reader(threading.Thread):
    """后台收帧线程：把 0x321 帧按 (时间, 载荷) 放进队列。"""

    def __init__(self, bus):
        super().__init__(daemon=True)
        self.bus = bus
        self.q = queue.Queue()
        self.stop_flag = threading.Event()
        self.n_rx = 0

    def run(self):
        while not self.stop_flag.is_set():
            try:
                msg = self.bus.recv(timeout=0.02)
            except can.CanError:
                continue
            if msg is None:
                continue
            self.n_rx += 1
            if msg.arbitration_id == ID_RES and not msg.is_extended_id:
                self.q.put((time.perf_counter(), bytes(msg.data)))

    def stop(self):
        self.stop_flag.set()


def test_heartbeat(reader, bus, secs, hb_min):
    print("\n[1/3] heartbeat: 监听 %.1f s 的 0x321（载荷 %s）..." % (secs, HB_MARK.hex()))
    # 排空旧数据
    while not reader.q.empty():
        try:
            reader.q.get_nowait()
        except queue.Empty:
            break
    n = 0
    t_end = time.perf_counter() + secs
    while time.perf_counter() < t_end:
        try:
            _t, data = reader.q.get(timeout=0.05)
        except queue.Empty:
            continue
        if len(data) >= 8 and data[4:8] == HB_MARK:
            n += 1
    hz = n / secs
    ok = n >= hb_min
    print("      收到心跳 %d 帧 / %.1f s = %.1f Hz（阈值 >= %d） -> %s"
          % (n, secs, hz, hb_min, "PASS" if ok else "FAIL"))
    return ok, hz


def test_latency(reader, bus, n, interval_ms, timeout_ms):
    print("\n[2/3] latency: %d 次 0x%03X -> 0x%03X 往返，间隔 %d ms ..."
          % (n, ID_REQ, ID_RES, interval_ms))
    while not reader.q.empty():
        try:
            reader.q.get_nowait()
        except queue.Empty:
            break
    lat = []
    lost = 0
    for i in range(n):
        payload = seq_payload(i)
        bus.send(can.Message(arbitration_id=ID_REQ, is_extended_id=False,
                             data=payload, is_fd=False))
        t0 = time.perf_counter()
        deadline = t0 + timeout_ms / 1000.0
        got = None
        while time.perf_counter() < deadline:
            try:
                ts, data = reader.q.get(timeout=max(0.0, deadline - time.perf_counter()))
            except queue.Empty:
                break
            if data == payload:
                got = ts
                break
        if got is None:
            lost += 1
        else:
            lat.append((got - t0) * 1000.0)
        if interval_ms > 0:
            time.sleep(interval_ms / 1000.0)

    if not lat:
        print("      全部超时（%d/%d 丢失） -> FAIL" % (lost, n))
        return False, {}
    lat_sorted = sorted(lat)
    p = lambda q: lat_sorted[min(len(lat_sorted) - 1, int(len(lat_sorted) * q))]
    stats = dict(n=len(lat), lost=lost, avg=statistics.fmean(lat), p50=p(0.50),
                 p95=p(0.95), p99=p(0.99), mx=lat_sorted[-1], mn=lat_sorted[0])
    print("      ok=%d lost=%d  avg=%.2f ms  p50=%.2f  p95=%.2f  p99=%.2f  max=%.2f  min=%.2f ms"
          % (stats["n"], lost, stats["avg"], stats["p50"], stats["p95"], stats["p99"],
             stats["mx"], stats["mn"]))
    stats["samples"] = lat
    return True, stats


def test_loss(reader, bus, rate, secs, timeout_ms):
    total = int(rate * secs)
    print("\n[3/3] loss: %d 帧/s 连续 %d s（共 %d 帧）..." % (rate, secs, total))
    while not reader.q.empty():
        try:
            reader.q.get_nowait()
        except queue.Empty:
            break
    interval = 1.0 / rate
    t_next = time.perf_counter()
    for i in range(total):
        bus.send(can.Message(arbitration_id=ID_REQ, is_extended_id=False,
                             data=seq_payload(i), is_fd=False))
        t_next += interval
        now = time.perf_counter()
        if t_next > now:
            time.sleep(t_next - now)
        else:
            t_next = now
    # 等最后一批回显（板上 CAN 与 UDP 都有排队）
    time.sleep(timeout_ms / 1000.0)
    got = set()
    while not reader.q.empty():
        try:
            _ts, data = reader.q.get_nowait()
        except queue.Empty:
            break
        if len(data) == 8 and data[4:8] == bytes([0x11, 0x22, 0x33, 0x44]):
            got.add(int.from_bytes(data[0:4], "little"))
    recv = len([i for i in range(total) if i in got])
    lost = total - recv
    pct = lost * 100.0 / total
    print("      发出 %d，回显 %d，丢失 %d = %.2f%%" % (total, recv, lost, pct))
    return pct


def main():
    ap = argparse.ArgumentParser(description="can_gateway 主机侧验收（心跳/延迟/丢包）")
    ap.add_argument("--iface", default="vcan0")
    ap.add_argument("--hb-secs", type=float, default=2.0)
    ap.add_argument("--hb-min", type=int, default=15, help="心跳最少帧数（2 s @10 Hz = 20）")
    ap.add_argument("--lat-n", type=int, default=200)
    ap.add_argument("--lat-interval-ms", type=float, default=20.0)
    ap.add_argument("--lat-timeout-ms", type=float, default=200.0)
    ap.add_argument("--lat-p99-ms", type=float, default=30.0)
    ap.add_argument("--loss-rate", type=int, default=300)
    ap.add_argument("--loss-secs", type=float, default=10.0)
    ap.add_argument("--loss-timeout-ms", type=float, default=300.0)
    ap.add_argument("--max-loss-pct", type=float, default=1.0)
    ap.add_argument("--skip", default="", help="跳过测试，逗号分隔：hb,lat,loss")
    ap.add_argument("--json-out", default="", help="把原始延迟样本与统计写成 JSON（供 tools/figs 画图）")
    ap.add_argument("--tag", default="", help="JSON 里的场景标签，如 t1000/t100000")
    args = ap.parse_args()

    skip = set(s.strip() for s in args.skip.split(",") if s.strip())
    bus = can.Bus(interface="socketcan", channel=args.iface)
    reader = Reader(bus)
    reader.start()
    results = {}
    try:
        if "hb" not in skip:
            ok, hz = test_heartbeat(reader, bus, args.hb_secs, args.hb_min)
            results["heartbeat"] = ok
            results["hb_hz"] = hz
        if "lat" not in skip:
            ok, st = test_latency(reader, bus, args.lat_n, args.lat_interval_ms,
                                  args.lat_timeout_ms)
            results["latency"] = bool(ok and st.get("p99", 1e9) <= args.lat_p99_ms)
            if st:
                results["lat_p99"] = st.get("p99", 0)
                results["lat_n"] = st.get("n", 0)
                results["lat_lost"] = st.get("lost", 0)
                results["lat_avg"] = st.get("avg", 0)
                results["lat_p50"] = st.get("p50", 0)
                results["lat_p95"] = st.get("p95", 0)
                results["lat_max"] = st.get("mx", 0)
                results["lat_min"] = st.get("mn", 0)
                results["lat_samples"] = st.get("samples", [])
        if "loss" not in skip:
            pct = test_loss(reader, bus, args.loss_rate, args.loss_secs, args.loss_timeout_ms)
            results["loss"] = pct <= args.max_loss_pct
            results["loss_pct"] = pct
            results["loss_total"] = int(args.loss_rate * args.loss_secs)
    finally:
        reader.stop()
        bus.shutdown()

    if args.json_out:
        import json
        doc = {
            "iface": args.iface,
            "tag": args.tag,
            "heartbeat_hz": results.get("hb_hz"),
            "latency": {k: v for k, v in results.items() if k.startswith("lat_") and k != "lat_samples"},
            "latency_samples": results.get("lat_samples", []),
            "loss": {"total": results.get("loss_total"), "pct": results.get("loss_pct")},
            "verdict": "PASS" if all(bool(results.get(k)) for k in ("heartbeat", "latency", "loss") if k in results) else "FAIL",
        }
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(doc, f, ensure_ascii=False, indent=1)
        print("  JSON 已写入 %s" % args.json_out)

    print("\n================ 汇总 ================")
    all_ok = True
    names = {"heartbeat": "心跳 10 Hz", "latency": "往返延迟 p99<=%.0f ms" % args.lat_p99_ms,
             "loss": "丢包率 <= %.2f%%" % args.max_loss_pct}
    for k, label in names.items():
        if k in results:
            ok = bool(results[k])
            all_ok = all_ok and ok
            print("  %-28s : %s" % (label, "PASS" if ok else "FAIL"))
    if "lat_p99" in results:
        print("  p99 延迟                     : %.2f ms" % results["lat_p99"])
    if "loss_pct" in results:
        print("  实测丢包率                   : %.2f %%" % results["loss_pct"])
    print("  总判定                       : %s" % ("PASS" if all_ok else "FAIL"))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())