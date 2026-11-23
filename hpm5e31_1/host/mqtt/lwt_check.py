#!/usr/bin/env python3
"""host/mqtt/lwt_check.py - 遗嘱(LWT)/断线重连/keepalive 保活验证

三种模式：
  kill      ：订阅 hpm5e31/status -> 等 online:true -> 发 cmd/kill（板端 tcp_abort）
              -> 量 broker 多快发出遗嘱 online:false, reason=lwt -> 再等板端自动重连 online:true
  reboot    ：先把 keepalive 降到 10 s 并重连，再发 cmd/reboot（软复位）
              -> broker 在 ~1.5×keepalive 后发现死链并发遗嘱 -> 板子重启后重新 online
  keepalive ：keepalive=10 s 下保活 35 s，统计遥测连续性；若给 --broker-log，
              解析 broker 日志里的 PINGREQ/PINGRESP 条数（证明是板端在保活）

用法：
  python3 host/mqtt/lwt_check.py --mode kill
  python3 host/mqtt/lwt_check.py --mode reboot --secs 40 --broker-log $HOME/tmp2/mosq_test.log
  python3 host/mqtt/lwt_check.py --mode keepalive --secs 35 --broker-log $HOME/tmp2/mosq_test.log

退出码：0 = PASS，1 = FAIL，2 = 连不上 broker。
"""
import argparse
import json
import os
import re
import sys
import time

import mqtt_helper as H


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=H.BROKER_DEFAULT)
    ap.add_argument("--port", type=int, default=H.PORT_DEFAULT)
    ap.add_argument("--mode", default="kill", choices=["kill", "reboot", "keepalive"])
    ap.add_argument("--secs", type=float, default=40.0)
    ap.add_argument("--broker-log", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    events = []          # (t, kind, obj)
    tele = []
    client = H.make_client("host-lwt-check")
    client._connected_flag = False

    def on_connect(c, u, flags, rc):
        if rc == 0:
            c._connected_flag = True
            c.subscribe([(H.TOPIC_STATUS, 1), (H.TOPIC_TELE, 1), (H.TOPIC_ACK, 1)])

    def on_message(c, u, msg):
        t = H.now()
        try:
            obj = json.loads(msg.payload.decode("utf-8", "replace"))
        except Exception:
            obj = None
        if msg.topic == H.TOPIC_STATUS:
            events.append((t, "status", obj))
        elif msg.topic == H.TOPIC_TELE:
            tele.append((t, obj))

    client.on_connect = H.on_connect_flex(on_connect)
    client.on_message = on_message
    if not H.connect(client, args.host, args.port):
        print("[host] FAIL 连不上 broker %s:%d" % (args.host, args.port))
        return 2
    time.sleep(0.3)

    def wait_for(pred, timeout):
        t0 = H.now()
        while (H.now() - t0) < timeout:
            for (t, kind, obj) in events:
                if pred(kind, obj):
                    return t, obj
            time.sleep(0.01)
        return None, None

    def online_pred(kind, obj):
        return kind == "status" and isinstance(obj, dict) and obj.get("online") is True

    def offline_pred(kind, obj):
        return kind == "status" and isinstance(obj, dict) and obj.get("online") is False

    report = {"mode": args.mode, "broker": "%s:%d" % (args.host, args.port)}
    ok = True

    t, obj = wait_for(online_pred, 8.0)
    if t is None:
        print("[host] FAIL 8 s 内没等到 retained online:true（板端没连上？）")
        return 1
    print("[host] 初始在线: %s" % json.dumps(obj, ensure_ascii=False))

    if args.mode == "kill":
        t_kill = H.now()
        client.publish(H.TOPIC_CMD + "kill", "kill", qos=1)
        print("[host] 已发 cmd/kill（板端 tcp_abort，不打招呼）")
        t, obj = wait_for(offline_pred, 10.0)
        if t is None:
            ok = False
            print("[host] FAIL 10 s 内没收到遗嘱")
        else:
            dt = (t - t_kill) * 1000.0
            print("[host] LWT: %s  延迟 %.1f ms" % (json.dumps(obj, ensure_ascii=False), dt))
            report["lwt_ms"] = round(dt, 1)
            report["lwt_payload"] = obj
        t2, obj2 = None, None
        for (te, kind, o) in events:
            if te > (t or 0) and online_pred(kind, o):
                t2, obj2 = te, o
                break
        if t2 is None:
            ok = False
            print("[host] FAIL 板端没有自动重连")
        else:
            dt2 = (t2 - t) * 1000.0 if t else 0.0
            print("[host] 自动重连 online:true，距遗嘱 %.1f ms" % dt2)
            report["reconnect_ms"] = round(dt2, 1)

    elif args.mode == "reboot":
        client.publish(H.TOPIC_CMD + "keepalive", "10", qos=1)
        time.sleep(0.5)
        client.publish(H.TOPIC_CMD + "reconnect", "1", qos=1)
        time.sleep(1.5)
        events.clear()
        wait_for(online_pred, 8.0)
        t_reboot = H.now()
        client.publish(H.TOPIC_CMD + "reboot", "1", qos=1)
        print("[host] 已发 cmd/reboot（板端 ppor_sw_reset），等 broker 依 keepalive=10s 发现死链")
        t, obj = wait_for(offline_pred, args.secs)
        if t is None:
            ok = False
            print("[host] FAIL %.0f s 内没收到遗嘱（keepalive 是否没生效？）" % args.secs)
        else:
            dt = (t - t_reboot) * 1000.0
            print("[host] LWT(复位): %s  延迟 %.0f ms" % (json.dumps(obj, ensure_ascii=False), dt))
            report["lwt_ms"] = round(dt, 1)
        t2, obj2 = wait_for(online_pred, 20.0)
        if t2 is None:
            ok = False
            print("[host] FAIL 复位后没有重新 online")
        else:
            print("[host] 复位后重新 online: %s" % json.dumps(obj2, ensure_ascii=False))
            report["reboot_back_ms"] = round((t2 - t) * 1000.0, 1) if t else None

    else:  # keepalive：把上行周期拉到 60 s 让链路空闲，才能真正逼出 PINGREQ
        client.publish(H.TOPIC_CMD + "keepalive", "10", qos=1)
        time.sleep(0.3)
        client.publish(H.TOPIC_CMD + "period", "60000", qos=1)
        time.sleep(0.3)
        client.publish(H.TOPIC_CMD + "reconnect", "1", qos=1)
        time.sleep(2.0)
        events.clear()
        tele.clear()
        print("[host] 已切到 keepalive=10s + 上行空闲（period=60 s），观察 %.0f s 的 PINGREQ" % args.secs)
        time.sleep(args.secs)
        off = [e for e in events if e[1] == "status" and isinstance(e[2], dict)
               and e[2].get("online") is False]
        n = len(tele)
        print("[host] 空闲保活 %.0fs: 遥测 %d 条（应≈0），意外遗嘱 %d 次"
              % (args.secs, n, len(off)))
        report.update({"telemetry": n, "unexpected_lwt": len(off)})
        ok = ok and (len(off) == 0)
        client.publish(H.TOPIC_CMD + "period", "1000", qos=1)
        if args.broker_log and os.path.exists(args.broker_log):
            with open(args.broker_log, "r", errors="replace") as f:
                text = f.read()
            pingreq = len(re.findall(r"Received PINGREQ from hpm5e31", text))
            pingresp = len(re.findall(r"Sending PINGRESP to hpm5e31", text))
            print("[host] broker 日志: PINGREQ=%d PINGRESP=%d（keepalive=10s 时约每 10 s 一轮）"
                  % (pingreq, pingresp))
            report["pingreq"] = pingreq
            report["pingresp"] = pingresp
            ok = ok and (pingreq >= 2) and (pingresp >= 2)
            report["keepalive_s"] = 10

    time.sleep(0.3)
    client.loop_stop()
    client.disconnect()
    report["verdict"] = "PASS" if ok else "FAIL"
    H.save_report(args.out or H.default_out("lwt-" + args.mode), report)
    print("[host] lwt_check(%s) => %s" % (args.mode, report["verdict"]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())