#!/usr/bin/env python3
"""DLT 报文接收 / 解析 / 落盘（配合 samples/app/dlt_log）

监听 UDP 3490（板端 -> 主机 192.168.2.1:3490），按 DLT 格式解析：
  Standard Header 4 B : HTYP(1) MCNT(1) LEN(2 BE)
  Extended Header     : MSIN(1) NOAR(1) APID(4) CTID(4) [TMSP(4, 0.1 ms LE) if HTYP.WTMS]
  Payload             : NOAR 个参数，每个 TypeInfo(4 LE) + 数据
                        TYLE bit0-3; UINT 0x40 / SINT 0x20 / STRG 0x200
统计：包数/秒、MCNT 丢包、APID/CTID/level 直方图、解析错误；原始报文写入 .dlt 文件。

用法：
  python3 dlt_recv.py --duration 6 --out /tmp/dlt/board.dlt
  python3 dlt_recv.py --port 3490 --duration 5 --expected-rate 100
"""
from __future__ import annotations

import argparse
import collections
import os
import socket
import struct
import sys
import time

LEVELS = ["fatal", "error", "warn", "info", "debug", "verbose"]
# DLT_TYPE_INFO_TYLE: 1=8bit 2=16bit 3=32bit 4=64bit 5=128bit
TYLE_BYTES = {1: 1, 2: 2, 3: 4, 4: 8, 5: 16}


def parse_packet(pkt: bytes):
    """返回 dict 或 None（格式不符）。

    布局（DLT v1，与 ref2/dlt-daemon/src/shared/dlt_common.c 的 headersize 计算一致）：
      Standard Header 4 B : HTYP(1) MCNT(1) LEN(2 BE)
      Standard-extra      : WEID->ECU ID(4) / WSID->session(4) / WTMS->TMSP(4 LE)  ← 在扩展头之前
      Extended Header 10 B: MSIN(1) NOAR(1) APID(4) CTID(4)
      Payload             : NOAR 个参数
    """
    if len(pkt) < 4:
        return None
    htyp, mcnt, length = pkt[0], pkt[1], struct.unpack(">H", pkt[2:4])[0]
    if length > len(pkt):
        return None
    off = 4
    msin = noar = None
    apid = ctid = None
    tmsp = None
    if htyp & 0x04:  # WEID: 4-byte ECU ID
        off += 4
    if htyp & 0x08:  # WSID: 4-byte session id
        off += 4
    if htyp & 0x10:  # WTMS: 4-byte timestamp (0.1 ms, LE) —— 标准头扩展，非扩展头
        if len(pkt) < off + 4:
            return None
        tmsp = struct.unpack("<I", pkt[off:off + 4])[0]
        off += 4
    if htyp & 0x01:  # UEH
        if len(pkt) < off + 10:
            return None
        msin = pkt[off]
        noar = pkt[off + 1]
        apid = pkt[off + 2:off + 6].decode("ascii", "replace")
        ctid = pkt[off + 6:off + 10].decode("ascii", "replace")
        off += 10
    args = []
    for _ in range(noar or 0):
        if len(pkt) < off + 4:
            break
        ti = struct.unpack("<I", pkt[off:off + 4])[0]
        off += 4
        tyle = ti & 0x0F
        if ti & 0x200:  # STRG
            if len(pkt) < off + 2:
                break
            slen = struct.unpack("<H", pkt[off:off + 2])[0]
            off += 2
            args.append(pkt[off:off + slen].decode("ascii", "replace"))
            off += slen
        elif ti & 0x40:  # UINT
            n = TYLE_BYTES.get(tyle, 4)
            args.append(int.from_bytes(pkt[off:off + n], "little"))
            off += n
        elif ti & 0x20:  # SINT
            n = TYLE_BYTES.get(tyle, 4)
            args.append(int.from_bytes(pkt[off:off + n], "little", signed=True))
            off += n
        else:
            break
    return {"htyp": htyp, "mcnt": mcnt, "len": length, "msin": msin, "noar": noar,
            "apid": apid, "ctid": ctid, "tmsp": tmsp, "args": args, "raw": pkt[:length]}


def main() -> int:
    ap = argparse.ArgumentParser(description="DLT UDP receiver")
    ap.add_argument("--port", type=int, default=3490)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--duration", type=float, default=6.0)
    ap.add_argument("--out", default="/tmp/dlt/board.dlt")
    ap.add_argument("--expected-rate", type=float, default=100.0, help="板端标称条/秒")
    args = ap.parse_args()

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    sock.settimeout(0.2)

    print("=== DLT 接收：UDP %s:%d，%0.1f s ===" % (args.bind, args.port, args.duration))
    pkts = []
    errs = 0
    mcnts = []
    apids = collections.Counter()
    ctids = collections.Counter()
    levels = collections.Counter()
    t_start = time.monotonic()
    t_end = t_start + args.duration
    first_ts = last_ts = None
    while time.monotonic() < t_end:
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        t = time.monotonic()
        r = parse_packet(data)
        if r is None:
            errs += 1
            continue
        pkts.append(r)
        mcnts.append(r["mcnt"])
        apids[r["apid"]] += 1
        ctids[r["ctid"]] += 1
        if r["args"] and isinstance(r["args"][0], int):
            levels[r["args"][0]] += 1
        if first_ts is None:
            first_ts = t
        last_ts = t
    sock.close()

    # 丢包：MCNT 是 1 字节计数器
    lost = 0
    for a, b in zip(mcnts, mcnts[1:]):
        step = (b - a) & 0xFF
        if step == 0:
            continue
        lost += step - 1
    sent_est = len(mcnts) + lost
    rate = (len(pkts) / (last_ts - first_ts)) if (first_ts and last_ts and last_ts > first_ts) else 0.0

    # 写标准 .dlt：每帧前缀 DltStorageHeader（pattern + sec + usec + ecuid_len + ecuid），
    # 这样 dlt-viewer / dlt-receive 可以直接打开（见第 38 章 U-38-3）
    # DltStorageHeader v1（16 B，dlt_common.h）：pattern[4]='DLT\x01' + seconds(u32 LE)
    # + microseconds(i32 LE) + ecu[4]（定长 4 字符，无长度字节）
    ecuid = b"HPM5"
    with open(args.out, "wb") as fp:
        for r in pkts:
            tmsp = r["tmsp"] or 0
            sec, usec = tmsp // 10000, (tmsp % 10000) * 100
            fp.write(b"DLT\x01" + struct.pack("<Ii", sec, usec) + ecuid + r["raw"])

    print("包数           : %d（解析错误 %d）" % (len(pkts), errs))
    print("实测速率       : %.1f 包/s（标称 %.0f）" % (rate, args.expected_rate))
    print("MCNT 丢包      : %d / %d（%.2f %%）" % (lost, sent_est, (lost * 100.0 / sent_est) if sent_est else 0))
    print("APID 直方图    : %s" % ", ".join("%s:%d" % (k, v) for k, v in apids.most_common()))
    print("CTID 直方图    : %s" % ", ".join("%s:%d" % (k, v) for k, v in ctids.most_common()))
    print("level 直方图   : %s" % ", ".join("%s:%d" % (LEVELS[k] if k < len(LEVELS) else k, v)
                                           for k, v in sorted(levels.items())))
    print(".dlt 文件      : %s（%d 字节）" % (args.out, sum(len(r["raw"]) for r in pkts)))
    if pkts:
        r = pkts[len(pkts) // 2]
        print("样例报文       : APID=%s CTID=%s msin=0x%02X noar=%d tmsp=%.1f ms args=%s"
              % (r["apid"], r["ctid"], r["msin"] or 0, r["noar"] or 0, (r["tmsp"] or 0) / 10.0,
                 [a if not isinstance(a, str) else a for a in r["args"]]))
        print("期望 LEN       : %d = 4(std) + 4(TMSP extra) + 10(ext) + payload(%d)"
              % (r["len"], r["len"] - 18))

    ok = (errs == 0) and (len(pkts) >= args.duration * args.expected_rate * 0.9) and (lost * 100.0 / sent_est <= 1.0)
    print("=== RESULT: %s ===" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
