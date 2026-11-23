#!/usr/bin/env python3
"""host/mqtt/mqtt_helper.py - paho-mqtt 2.x/1.x 兼容小工具（第 38 章上位机共用）

只做三件事：建客户端（回调签名兼容 v1/v2）、JSON 上报、报告落盘。
所有脚本都只用 paho-mqtt + 标准库，不装别的依赖。
"""
import json
import os
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover
    print("[host] 缺少 paho-mqtt：sudo apt install python3-paho-mqtt（或 pip install paho-mqtt）")
    sys.exit(3)

BROKER_DEFAULT = "192.168.2.1"
PORT_DEFAULT = 1883
TOPIC_TELE = "hpm5e31/telemetry"
TOPIC_STATUS = "hpm5e31/status"
TOPIC_ACK = "hpm5e31/ack"
TOPIC_CMD = "hpm5e31/cmd/"
TOPIC_ECHO_REPLY = "hpm5e31/echo/reply"
TOPIC_BENCH = "hpm5e31/bench"
TOPIC_BENCH_DONE = "hpm5e31/bench/done"


def make_client(client_id, clean=True):
    """建 paho 客户端（兼容 paho 2.x 的 CallbackAPIVersion 与 1.x 老接口）"""
    if hasattr(mqtt, "CallbackAPIVersion"):
        try:
            return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id,
                               clean_session=clean)
        except TypeError:
            pass
    return mqtt.Client(client_id=client_id, clean_session=clean)


def rc_value(rc):
    """把 paho 1.x 的 int rc / 2.x 的 ReasonCode 统一成 int"""
    if hasattr(rc, "value"):
        return int(rc.value)
    return int(rc)


def on_connect_flex(cb):
    """把用户回调包成 v1/v2 都能用的 on_connect"""
    def _wrap(client, userdata, flags, rc, *args):
        cb(client, userdata, flags, rc_value(rc))
    return _wrap


def on_disconnect_flex(cb):
    def _wrap(client, userdata, *args):
        # v1: (rc) ; v2: (disconnect_flags, reason_code, properties)
        rc = None
        for a in reversed(args):
            if hasattr(a, "value"):
                rc = a
                break
        if rc is None:
            for a in args:
                if isinstance(a, int):
                    rc = a
                    break
        cb(client, userdata, rc_value(rc) if rc is not None else 0)
    return _wrap


def now():
    return time.monotonic()


def default_out(name):
    """默认报告路径：仓库 logs/<时间戳>-host-mqtt-<name>.json"""
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    logs = os.path.join(root, "logs")
    os.makedirs(logs, exist_ok=True)
    return os.path.join(logs, time.strftime("%Y%m%d-%H%M%S") + "-host-mqtt-" + name + ".json")


def save_report(path, data):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print("[host] report -> " + path)


def print_json_ok(tag, obj):
    print("[host] " + tag + " " + json.dumps(obj, ensure_ascii=False))


def connect(client, host, port, timeout=5.0):
    client.connect(host, port, keepalive=30)
    client.loop_start()
    t0 = now()
    while not getattr(client, "_connected_flag", False):
        if (now() - t0) > timeout:
            return False
        time.sleep(0.02)
    return True
