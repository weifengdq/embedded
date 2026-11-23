#!/usr/bin/env python3
"""host/mqtt/sub_check.py - 订阅 uplink 主题并校验（板端 mqtt_telemetry）

校验内容：
  1) hpm5e31/telemetry 周期上报：条数、速率、seq 连续性、字段完整性、载荷字节数
  2) hpm5e31/status    retained 在线状态：连接后能立刻收到 retain=1 的 online:true
  3) hpm5e31/ack       命令回执（如果同时跑了 pub_cmd.py，这里能看到历史回执不保留，仅统计）

用法：
  python3 host/mqtt/sub_check.py --secs 12
  python3 host/mqtt/sub_check.py --secs 30 --out logs/xx.json

退出码：0 = PASS，1 = FAIL，2 = 连接失败。
"""
import argparse
import json
import sys
import time

import mqtt_helper as H

REQUIRED_FIELDS = ["dev", "seq", "up_ms", "can_tx", "can_rx", "adc", "temp_mc",
                   "loop_us", "loop_max_us", "mqtt_rx", "mqtt_tx"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=H.BROKER_DEFAULT)
    ap.add_argument("--port", type=int, default=H.PORT_DEFAULT)
    ap.add_argument("--secs", type=float, default=12.0)
    ap.add_argument("--min-samples", type=int, default=5)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    rec = {"telemetry": [], "status": [], "acks": []}
    client = H.make_client("host-sub-check")
    client._connected_flag = False

    def on_connect(c, u, flags, rc):
        if rc == 0:
            c._connected_flag = True
            c.subscribe([(H.TOPIC_TELE, 1), (H.TOPIC_STATUS, 1), (H.TOPIC_ACK, 1)])

    def on_message(c, u, msg):
        t = H.now()
        try:
            obj = json.loads(msg.payload.decode("utf-8", "replace"))
        except Exception:
            obj = None
        item = {"t": t, "qos": msg.qos, "retain": msg.retain,
                "bytes": len(msg.payload), "obj": obj}
        if msg.topic == H.TOPIC_TELE:
            rec["telemetry"].append(item)
        elif msg.topic == H.TOPIC_STATUS:
            rec["status"].append(item)
        else:
            rec["acks"].append(item)

    client.on_connect = H.on_connect_flex(on_connect)
    client.on_message = on_message
    client.on_disconnect = H.on_disconnect_flex(lambda c, u, rc: print("[host] disconnected rc=%d" % rc))

    if not H.connect(client, args.host, args.port):
        print("[host] FAIL 连不上 broker %s:%d" % (args.host, args.port))
        return 2

    print("[host] connected broker %s:%d, 采 %0.1f s ..." % (args.host, args.port, args.secs))
    time.sleep(args.secs)
    client.loop_stop()
    client.disconnect()

    tele = rec["telemetry"]
    ok = True
    report = {"broker": "%s:%d" % (args.host, args.port), "seconds": args.secs,
              "telemetry_count": len(tele), "status": [], "checks": {}}

    # ---- telemetry ----
    if len(tele) == 0:
        print("[host] FAIL 没收到 telemetry（板端是否在线？topic=%s）" % H.TOPIC_TELE)
        return 1
    seqs = [x["obj"]["seq"] for x in tele if isinstance(x["obj"], dict) and "seq" in x["obj"]]
    ts = [x["t"] for x in tele]
    gaps = 0
    for a, b in zip(seqs, seqs[1:]):
        if b != a + 1:
            gaps += (b - a - 1) if b > a else 1
    intervals = [(b - a) * 1000.0 for a, b in zip(ts, ts[1:])]
    span = (ts[-1] - ts[0]) if len(ts) > 1 else 0.0
    rate = (len(tele) - 1) / span if span > 0 else 0.0
    missing = []
    for x in tele:
        if not isinstance(x["obj"], dict):
            missing.append("non-json")
            break
        miss = [f for f in REQUIRED_FIELDS if f not in x["obj"]]
        if miss:
            missing = miss
            break
    sizes = [x["bytes"] for x in tele]
    print("[host] telemetry: n=%d rate=%.3f Hz span=%.2fs payload=%dB..%dB qos=%s"
          % (len(tele), rate, span, min(sizes), max(sizes), tele[0]["qos"]))
    if intervals:
        print("[host] interval ms: avg=%.1f min=%.1f max=%.1f"
              % (sum(intervals) / len(intervals), min(intervals), max(intervals)))
    print("[host] seq: first=%s last=%s gaps=%d" % (seqs[0] if seqs else "-", seqs[-1] if seqs else "-", gaps))

    c1 = len(tele) >= args.min_samples
    c2 = not missing
    c3 = gaps <= 1                      # 首尾边界允许 1 条
    c4 = 0.8 <= rate <= 1.2             # 默认 1000 ms 周期
    report["checks"]["samples>=%d" % args.min_samples] = c1
    report["checks"]["fields"] = c2
    report["checks"]["seq_continuous"] = c3
    report["checks"]["rate_1hz"] = c4
    report["rate_hz"] = round(rate, 3)
    report["gaps"] = gaps
    report["last"] = tele[-1]["obj"]

    # ---- status retained ----
    for s in rec["status"]:
        print("[host] status: retain=%s qos=%s %s" % (s["retain"], s["qos"], s["obj"]))
        report["status"].append(s["obj"])
    online = [s for s in rec["status"]
              if isinstance(s["obj"], dict) and s["obj"].get("online") is True]
    retained = [s for s in rec["status"] if s["retain"]]
    c5 = (len(online) > 0) and (len(retained) > 0)
    report["checks"]["status_retained_online"] = c5

    # ---- acks（可选） ----
    if rec["acks"]:
        print("[host] acks: %d 条, 最后一条 %s" % (len(rec["acks"]), rec["acks"][-1]["obj"]))
        report["acks"] = [a["obj"] for a in rec["acks"]]

    for k, v in report["checks"].items():
        ok = ok and v
        print("[host]   %-28s %s" % (k, "OK" if v else "FAIL"))
    if missing:
        print("[host] missing fields: %s" % missing)

    out = args.out or H.default_out("sub-check")
    report["verdict"] = "PASS" if ok else "FAIL"
    H.save_report(out, report)
    print("[host] %s => %s" % ("sub_check", report["verdict"]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
