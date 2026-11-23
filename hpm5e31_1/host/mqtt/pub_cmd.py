#!/usr/bin/env python3
"""host/mqtt/pub_cmd.py - 下行命令发布 + 回执/回声时延测量

支持的命令（对应板端 hpm5e31/cmd/<x>）：
  led on|off|toggle     控制 PA02 用户灯
  period <ms>           遥测周期（100..60000）
  cansend <id,hex..>    从 CAN0 发一帧（CAN0<->CAN1 板内互连）
  keepalive <s>         改 keepalive（下次 CONNECT 生效，配合 reconnect）
  reconnect             优雅断开后立刻重连（无遗嘱）
  kill                  直接断链（broker 应发遗嘱，验证 LWT）
  reboot                板端软复位（ppor_sw_reset）
  bench <n>,<qos>       板端向 hpm5e31/bench 连发 n 条，测板->主机吞吐
  echo                  回声时延（--echo-n 条，QoS 可配）

用法：
  python3 host/mqtt/pub_cmd.py --cmd led --arg on
  python3 host/mqtt/pub_cmd.py --cmd echo --echo-n 20
  python3 host/mqtt/pub_cmd.py --cmd bench --arg 1000,1 --secs 20

退出码：0 = PASS/已收到预期回执，1 = FAIL，2 = 连不上 broker。
"""
import argparse
import json
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
    ap.add_argument("--cmd", default="led")
    ap.add_argument("--arg", default="on")
    ap.add_argument("--echo-n", type=int, default=20)
    ap.add_argument("--secs", type=float, default=8.0, help="等回执/统计窗口")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    rec = {"acks": [], "echo": [], "bench": [], "bench_done": []}
    client = H.make_client("host-pub-cmd")
    client._connected_flag = False

    def on_connect(c, u, flags, rc):
        if rc == 0:
            c._connected_flag = True
            c.subscribe([(H.TOPIC_ACK, 1), (H.TOPIC_ECHO_REPLY, 0),
                         (H.TOPIC_BENCH, 0), (H.TOPIC_BENCH_DONE, 0)])

    def on_message(c, u, msg):
        t = H.now()
        try:
            obj = json.loads(msg.payload.decode("utf-8", "replace"))
        except Exception:
            obj = None
        if msg.topic == H.TOPIC_ACK:
            rec["acks"].append({"t": t, "obj": obj})
        elif msg.topic == H.TOPIC_ECHO_REPLY:
            rtt = None
            if isinstance(obj, dict) and "t" in obj:
                rtt = (t - float(obj["t"])) * 1000.0
            rec["echo"].append({"t": t, "rtt_ms": rtt, "bytes": len(msg.payload)})
        elif msg.topic == H.TOPIC_BENCH:
            rec["bench"].append({"t": t, "bytes": len(msg.payload)})
        elif msg.topic == H.TOPIC_BENCH_DONE:
            rec["bench_done"].append(obj)

    client.on_connect = H.on_connect_flex(on_connect)
    client.on_message = on_message
    if not H.connect(client, args.host, args.port):
        print("[host] FAIL 连不上 broker %s:%d" % (args.host, args.port))
        return 2
    time.sleep(0.3)                      # 等订阅生效

    report = {"cmd": args.cmd, "arg": args.arg, "broker": "%s:%d" % (args.host, args.port)}
    ok = True
    topic = H.TOPIC_CMD + args.cmd

    if args.cmd == "echo":
        n = args.echo_n
        t_send = []
        print("[host] echo: 发 %d 条 QoS1 到 %s，收回声测 RTT" % (n, topic))
        for i in range(n):
            payload = json.dumps({"seq": i, "t": H.now()})
            client.publish(topic, payload, qos=1)
            t_send.append(H.now())
            time.sleep(0.002)
        t0 = H.now()
        while (H.now() - t0) < args.secs and len(rec["echo"]) < n:
            time.sleep(0.01)
        rtts = [e["rtt_ms"] for e in rec["echo"] if e["rtt_ms"] is not None]
        loss = n - len(rec["echo"])
        report.update({"sent": n, "recv": len(rec["echo"]), "loss": loss,
                       "rtt_ms": {"avg": round(sum(rtts) / len(rtts), 3) if rtts else None,
                                  "p50": round(pct(rtts, 50), 3), "p95": round(pct(rtts, 95), 3),
                                  "max": round(max(rtts), 3) if rtts else None,
                                  "samples": [round(x, 2) for x in rtts]}})
        print("[host] echo recv=%d/%d loss=%d rtt avg=%s p50=%s p95=%s max=%s ms"
              % (len(rec["echo"]), n, loss,
                 report["rtt_ms"]["avg"], report["rtt_ms"]["p50"], report["rtt_ms"]["p95"],
                 report["rtt_ms"]["max"]))
        ok = (loss == 0) and (report["rtt_ms"]["avg"] is not None) and (report["rtt_ms"]["avg"] < 100.0)
        print("[host] echo => %s" % ("PASS" if ok else "FAIL"))

    elif args.cmd == "bench":
        try:
            n_str, qos_str = args.arg.split(",")
            bn, bq = int(n_str), int(qos_str)
        except Exception:
            print("[host] bench 参数格式应为 '<n>,<qos>'，例如 1000,1")
            return 1
        t_pub = H.now()
        client.publish(topic, args.arg, qos=1)
        t0 = H.now()
        while (H.now() - t0) < args.secs and not rec["bench_done"]:
            time.sleep(0.02)
        rx = rec["bench"]
        dt = (rx[-1]["t"] - rx[0]["t"]) if len(rx) > 1 else 0.0
        rate = (len(rx) - 1) / dt if dt > 0 else 0.0
        report.update({"sent_target": bn, "qos": bq, "recv": len(rx), "loss": bn - len(rx),
                       "rx_seconds": round(dt, 3), "rx_rate_msg_s": round(rate, 1),
                       "board": rec["bench_done"][-1] if rec["bench_done"] else None})
        print("[host] bench n=%d qos=%d recv=%d loss=%d 用时=%.3fs 接收速率=%.1f msg/s"
              % (bn, bq, len(rx), bn - len(rx), dt, rate))
        if rec["bench_done"]:
            print("[host] board 自报: %s" % rec["bench_done"][-1])
        ok = bool(rec["bench_done"]) and (len(rx) == bn)
        print("[host] bench => %s" % ("PASS" if ok else "FAIL"))

    else:
        client.publish(topic, args.arg, qos=1)
        t_pub = H.now()
        acked = None
        t0 = H.now()
        while (H.now() - t0) < args.secs:
            for a in rec["acks"]:
                if isinstance(a["obj"], dict) and a["obj"].get("cmd") == args.cmd:
                    acked = a
                    break
            if acked:
                break
            time.sleep(0.01)
        if acked:
            rtt = (acked["t"] - t_pub) * 1000.0
            report.update({"pub_topic": topic, "payload": args.arg, "ack": acked["obj"],
                           "ack_rtt_ms": round(rtt, 3)})
            print("[host] ack %s  RTT=%.2f ms" % (json.dumps(acked["obj"], ensure_ascii=False), rtt))
        else:
            # reboot/kill 类命令回执可能来不及发出，允许"无回执但命令已发布"
            report.update({"pub_topic": topic, "payload": args.arg, "ack": None})
            if args.cmd in ("reboot", "kill"):
                print("[host] 命令 %s 已发布（此类命令回执可能来不及/被断链吞掉）" % args.cmd)
                ok = True
            else:
                print("[host] FAIL 未收到 %s 的回执" % args.cmd)
                ok = False
        print("[host] cmd=%s => %s" % (args.cmd, "PASS" if ok else "FAIL"))

    time.sleep(0.2)
    client.loop_stop()
    client.disconnect()
    out = args.out or H.default_out("pub-" + args.cmd)
    report["verdict"] = "PASS" if ok else "FAIL"
    H.save_report(out, report)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
