#!/usr/bin/env python3
"""obd_scan.py - OBD-II 扫描器（服务 0x01 / 0x09）

支持：
  01 00   支持的 PID 位图（01~20）
  01 05   冷却液温度（A-40 ℃）
  01 0C   发动机转速（((A<<8)|B)/4 rpm）
  01 0D   车速（A km/h）
  01 1C   OBD 标准（A=1..）
  09 02   VIN（17 字节 ASCII，多帧）

用法：
  python3 obd_scan.py --channel vcan0
  python3 obd_scan.py --channel vcan5 --req-id 0x7E0
"""
import argparse
import sys
import time

import common

PIDS = {
    0x05: ("冷却液温度", "℃", lambda d: d[0] - 40),
    0x0C: ("发动机转速", "rpm", lambda d: ((d[0] << 8) | d[1]) / 4.0),
    0x0D: ("车速", "km/h", lambda d: d[0]),
    0x1C: ("OBD 标准", "", lambda d: d[0]),
}


def main():
    ap = argparse.ArgumentParser(description="OBD-II PID 扫描器")
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--req-id", default="0x7DF", help="功能请求 ID（默认 0x7DF 广播）")
    ap.add_argument("--resp-id", default="0x7E8")
    args = ap.parse_args()
    req_id, resp_id = int(args.req_id, 0), int(args.resp_id, 0)
    bus = common.RawCan(args.channel)
    tp = common.IsoTp(bus, rx_id=resp_id, tx_id=req_id)
    cl = common.UdsClient(tp, p2star=3.0)

    print("== OBD-II 扫描: %s 0x%03X/0x%03X ==" % (args.channel, req_id, resp_id))
    ok = 0
    total = 0
    try:
        resp, rtt, _ = cl.request([0x01, 0x00])
        total += 1
        if len(resp) >= 6 and resp[0] == 0x41 and resp[1] == 0x00:
            bitmap = (resp[2] << 24) | (resp[3] << 16) | (resp[4] << 8) | resp[5]
            print("  01 00 支持 PID 位图 = 0x%08X (rtt=%d us)" % (bitmap, rtt))
            ok += 1
        else:
            print("  01 00 -> %s" % common.describe(resp))
    except common.IsoTpError as e:
        print("  01 00 timeout: %s" % e)

    for pid, (name, unit, conv) in PIDS.items():
        total += 1
        try:
            resp, rtt, _ = cl.request([0x01, pid])
            if len(resp) >= 3 and resp[0] == 0x41 and resp[1] == pid:
                val = conv(resp[2:])
                print("  01 %02X %-12s = %-8.2f %-4s (rtt=%d us)" % (pid, name, val, unit, rtt))
                ok += 1
            else:
                print("  01 %02X %-12s -> %s" % (pid, name, common.describe(resp)))
        except common.IsoTpError as e:
            print("  01 %02X timeout: %s" % (pid, e))

    # 09 02 VIN
    total += 1
    try:
        resp, rtt, _ = cl.request([0x09, 0x02])
        if len(resp) >= 3 and resp[0] == 0x49:
            vin = resp[3:].decode("ascii", "replace").strip("\x00")
            print("  09 02 VIN = %s (rtt=%d us)" % (vin, rtt))
            ok += 1
        else:
            print("  09 02 -> %s" % common.describe(resp))
    except common.IsoTpError as e:
        print("  09 02 timeout: %s" % e)
    bus.close()
    print("[obd_scan] %s pids=%d/%d" % ("PASS" if ok == total else "FAIL", ok, total))
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main())
