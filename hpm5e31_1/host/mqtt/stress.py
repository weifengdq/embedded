#!/usr/bin/env python3
"""host/mqtt/stress.py - 1000 条消息吞吐与往返时延统计（板端回声）

两个阶段：
  1) throughput：尽快连发 N 条到 hpm5e31/cmd/echo（QoS0/1 可选），统计
     发布速率、回声接收速率、丢包（按 seq 计）、端到端时延分布
  2) rtt：逐条发、逐条等回声，测"真正的"往返时延（不含发布端排队）

用法：
  python3 host/mqtt/stress.py --n 1000 --qos 1
  python3 host/mqtt/stress.py --n 1000 --qos 0 --mode throughput
  python3 host/mqtt/stress.py --n 200 --mode rtt --out logs/xx.json

退出码：0 = PASS（丢包 ≤ 1%），1 = FAIL，2 = 连不上 broker。
"""
import argparse
import json
import statistics
import sys
import time

import mqtt_helper as H


def pct(v, p):
    if not v:
        return 0.0
    k = sorted(v)
    idx = min(len(k) - 1, int(round((p / 100.0) * (len(k) - 1))))
    return k[idx]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=H.BROKER_DEFAULT)
    ap.add_argument("--port", type=int, default=H.PORT_DEFAULT)
    ap.add_argument("--n", type=int, default=1000)
    ap.add_argument("--qos", type=int, default=1, choices=[0, 1])
    ap.add_argument("--mode", default="both", choices=["throughput", "rtt", "both"])
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--rate", type=float, default=0.0, help="发布限速 msg/s，0=不限速")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    recv = {}          # seq -> (recv_monotonic, rtt_ms)
    client = H.make_client("host-stress")
    client._connected_flag = False
    client.max_inflight_messages_set(100)

    def on_connect(c, u, flags, rc):
        if rc == 0:
            c._connected_flag = True
            c.subscribe([(H.TOPIC_ECHO_REPLY, 0)])

    def on_message(c, u, msg):
        t = H.now()
        try:
            obj = json.loads(msg.payload.decode("utf-8", "replace"))
            seq = int(obj["seq"])
            t_send = float(obj["t"])
            recv[seq] = (t, (t - t_send) * 1000.0)
        except Exception:
            recv.setdefault(-1, (t, None))

    client.on_connect = H.on_connect_flex(on_connect)
    client.on_message = on_message
    if not H.connect(client, args.host, args.port):
        print("[host] FAIL 连不上 broker %s:%d" % (args.host, args.port))
        return 2
    time.sleep(0.3)

    report = {"n": args.n, "qos": args.qos, "mode": args.mode,
              "broker": "%s:%d" % (args.host, args.port)}
    ok = True

    if args.mode in ("throughput", "both"):
        recv.clear()
        t_pub0 = H.now()
        for i in range(args.n):
            payload = json.dumps({"seq": i, "t": H.now()})
            client.publish(H.TOPIC_CMD + "echo", payload, qos=args.qos)
            if args.rate > 0:
                # 限速：给上位机留出处理回声的时间，测"稳态"而不是自压测
                next_t = t_pub0 + (i + 1) / args.rate
                while H.now() < next_t:
                    time.sleep(0.0005)
        t_pub1 = H.now()
        t0 = H.now()
        while (H.now() - t0) < args.timeout and len(recv) < args.n:
            time.sleep(0.005)
        t_end = H.now()
        pub_s = t_pub1 - t_pub0
        rx = [recv[k] for k in recv if k >= 0]
        rtts = [r[1] for r in rx if r[1] is not None]
        missing = args.n - len(rx)
        dt = (max(r[0] for r in rx) - min(r[0] for r in rx)) if len(rx) > 1 else 0.0
        report["throughput"] = {
            "publish_seconds": round(pub_s, 3),
            "publish_rate_msg_s": round(args.n / pub_s, 1) if pub_s > 0 else None,
            "recv": len(rx), "loss": missing,
            "loss_pct": round(100.0 * missing / args.n, 2),
            "recv_seconds": round(dt, 3),
            "recv_rate_msg_s": round((len(rx) - 1) / dt, 1) if dt > 0 else None,
            "e2e_rtt_ms": {
                "avg": round(statistics.mean(rtts), 2) if rtts else None,
                "p50": round(pct(rtts, 50), 2), "p95": round(pct(rtts, 95), 2),
                "p99": round(pct(rtts, 99), 2),
                "max": round(max(rtts), 2) if rtts else None,
            },
        }
        tp = report["throughput"]
        print("[host] throughput: pub %d 条用时 %.3fs (%.1f msg/s) | 回声 %d 条, 丢 %d (%.2f%%)"
              % (args.n, pub_s, tp["publish_rate_msg_s"] or 0, len(rx), missing, tp["loss_pct"]))
        print("[host] e2e rtt ms: avg=%s p50=%s p95=%s p99=%s max=%s (含发布端排队)"
              % (tp["e2e_rtt_ms"]["avg"], tp["e2e_rtt_ms"]["p50"], tp["e2e_rtt_ms"]["p95"],
                 tp["e2e_rtt_ms"]["p99"], tp["e2e_rtt_ms"]["max"]))
        ok = ok and (missing <= max(1, args.n // 100))
        print("[host] throughput => %s" % ("PASS" if ok else "FAIL"))

    if args.mode in ("rtt", "both"):
        n = args.n if args.mode == "rtt" else min(200, args.n)
        recv.clear()
        rtts = []
        t0 = H.now()
        lost = 0
        for i in range(n):
            t_send = H.now()
            client.publish(H.TOPIC_CMD + "echo",
                           json.dumps({"seq": i, "t": t_send}), qos=args.qos)
            deadline = H.now() + 1.0
            while i not in recv and H.now() < deadline:
                time.sleep(0.0005)
            if i in recv and recv[i][1] is not None:
                rtts.append((recv[i][0] - t_send) * 1000.0)
            else:
                lost += 1
        total = H.now() - t0
        report["rtt"] = {
            "n": n, "recv": len(rtts), "lost": lost,
            "seconds": round(total, 3),
            "msg_s": round(n / total, 1) if total > 0 else None,
            "avg_ms": round(statistics.mean(rtts), 3) if rtts else None,
            "p50_ms": round(pct(rtts, 50), 3), "p95_ms": round(pct(rtts, 95), 3),
            "max_ms": round(max(rtts), 3) if rtts else None,
            "samples_ms": [round(x, 2) for x in rtts[:50]],
        }
        r = report["rtt"]
        print("[host] rtt: n=%d recv=%d lost=%d 用时=%.3fs (%.1f msg/s) avg=%s p50=%s p95=%s max=%s ms"
              % (n, r["recv"], lost, total, r["msg_s"] or 0, r["avg_ms"], r["p50_ms"],
                 r["p95_ms"], r["max_ms"]))
        ok = ok and (lost == 0)
        print("[host] rtt => %s" % ("PASS" if ok else "FAIL"))

    time.sleep(0.3)
    client.loop_stop()
    client.disconnect()
    report["verdict"] = "PASS" if ok else "FAIL"
    H.save_report(args.out or H.default_out("stress"), report)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())