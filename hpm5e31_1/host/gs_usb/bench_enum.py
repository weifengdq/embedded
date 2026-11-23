#!/usr/bin/env python3
"""bench_enum.py —— 测 gs_usb 的枚举/接口就绪耗时（供 fig-40-4）

方法：对已经枚举的 gs_usb 设备做 **驱动 rebind**（unbind -> bind），用高精度时钟测：
  t0 = 写 bind 的瞬间
  t1 = /sys/class/net/can3 出现（4 个 netdev 全部就绪）
  t2 = can0 可被 ip link set 配置成功（接口真正可用）
重复 N 次取统计。rebind 只动驱动绑定，不烧固件、不改设备配置，安全可重复。

用法：sudo -n python3 host/gs_usb/bench_enum.py [--repeat 5] [--json]
"""

import argparse
import json
import os
import subprocess
import sys
import time

PORT_GLOB = "/sys/bus/usb/devices"
DRV = "/sys/bus/usb/drivers/gs_usb"


def sh(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return p.returncode, ((p.stdout or "") + (p.stderr or "")).strip()


def find_port():
    for name in sorted(os.listdir(PORT_GLOB)):
        d = os.path.join(PORT_GLOB, name)
        try:
            with open(os.path.join(d, "idVendor")) as f:
                v = f.read().strip()
            with open(os.path.join(d, "idProduct")) as f:
                p = f.read().strip()
        except OSError:
            continue
        if v == "1209" and p == "2323":
            return name
    return None


def driver_iface_name(port):
    """gs_usb 绑定在 USB 接口上，unbind/bind 要写 "1-8:1.0" 而不是 "1-8"。"""
    import glob
    cands = glob.glob(f"/sys/bus/usb/devices/{port}:*")
    for c in cands:
        drv = os.path.join(c, "driver")
        if os.path.exists(drv) and os.path.basename(os.readlink(drv)) == "gs_usb":
            return os.path.basename(c)
    return f"{port}:1.0"


def wait_ifaces(count=4, timeout=5.0):
    t_start = time.monotonic()
    while time.monotonic() - t_start < timeout:
        n = sum(1 for i in range(count) if os.path.isdir(f"/sys/class/net/can{i}"))
        if n == count:
            return time.monotonic() - t_start
        time.sleep(0.002)
    return None


def wait_usable(iface="can0", timeout=2.0):
    t_start = time.monotonic()
    while time.monotonic() - t_start < timeout:
        rc, _ = sh(f"ip link set {iface} down 2>/dev/null; ip link set {iface} type can bitrate 500000 fd off")
        if rc == 0:
            return time.monotonic() - t_start
        time.sleep(0.005)
    return None


def main():
    ap = argparse.ArgumentParser(description="gs_usb 枚举/接口就绪耗时")
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    port = find_port()
    res = {"test": "enum", "port": port, "repeat": args.repeat,
           "netdev_ready_ms": [], "usable_ms": [], "verdict": "FAIL"}
    if not port:
        res["reason"] = "找不到 1209:2323 设备"
        print(json.dumps(res, ensure_ascii=False)); return 2

    for i in range(args.repeat):
        iface = driver_iface_name(port)
        # unbind
        rc, out = sh(f"echo -n {iface} > {DRV}/unbind")
        if rc != 0:
            res["reason"] = f"unbind 失败: {out}"
            break
        # 等到 netdev 全部消失
        for _ in range(200):
            if not os.path.isdir("/sys/class/net/can3"):
                break
            time.sleep(0.005)
        t0 = time.monotonic()
        rc, out = sh(f"echo -n {iface} > {DRV}/bind")
        if rc != 0:
            res["reason"] = f"bind 失败: {out}"
            break
        d_net = wait_ifaces(4)
        d_use = wait_usable("can0")
        if d_net is not None:
            res["netdev_ready_ms"].append(round(d_net * 1000.0, 1))
        if d_use is not None:
            res["usable_ms"].append(round((d_net or 0) * 1000.0 + d_use * 1000.0, 1))
        sh("ip link set can0 down 2>/dev/null")
        time.sleep(0.2)

    import statistics
    def stats(v):
        if not v:
            return None
        return {"min": min(v), "median": statistics.median(v), "max": max(v),
                "mean": round(statistics.fmean(v), 1), "n": len(v)}
    res["netdev_ready"] = stats(res["netdev_ready_ms"])
    res["usable"] = stats(res["usable_ms"])
    if res["netdev_ready"] and res["netdev_ready"]["median"] < 3000:
        res["verdict"] = "PASS"
    else:
        res.setdefault("reason", "netdev 就绪超时或失败")

    if args.json:
        print(json.dumps(res, ensure_ascii=False))
    else:
        print(f"设备端口: {port}  重复 {args.repeat} 次")
        if res["netdev_ready"]:
            s = res["netdev_ready"]
            print(f"4 个 netdev 就绪耗时: min {s['min']} / 中位 {s['median']} / max {s['max']} ms（{s['n']} 次）")
        if res["usable"]:
            s = res["usable"]
            print(f"can0 可配置耗时    : min {s['min']} / 中位 {s['median']} / max {s['max']} ms")
        print(f"结论: {res['verdict']} {res.get('reason','')}")
    return 0 if res["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
