#!/usr/bin/env python3
"""mini_flasher.py - UDS 刷写最小实现（0x34/0x36/0x37 + 0x31 校验）

流程（与 samples/app/uds_flash 的板内 tester 一致）：
  10 02 编程会话 -> 27 01/02 解锁 -> 34 请求下载（ALFID=0x44，4B 地址 + 4B 长度）
  -> N × 36 每块 256 B -> 37 退出 -> 31 01 FF00 读回 CRC32 比对
  -> 31 01 FF01 依赖检查 ->（可选 --reset）11 01 复位

镜像布局（与固件一致，32 KB）：
  [0..7] "HPMIMG01" | [8..11] 长度 u32 LE | [12..15] 数据区 CRC32 | [256..] 数据

用法：
  python3 mini_flasher.py --channel vcan0 --size 32768
  python3 mini_flasher.py --channel vcan5 --size 32768 --addr 0x800F0000
"""
import argparse
import struct
import sys
import time

import common

MAGIC = b"HPMIMG01"
HDR = 256


def build_image(size, seed=0xC0FFEE11):
    buf = bytearray(b"\xff" * size)
    buf[0:8] = MAGIC
    struct.pack_into("<I", buf, 8, size)
    x = seed
    for i in range(HDR, size):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        x &= 0xFFFFFFFF
        buf[i] = (x >> 24) & 0xFF
    struct.pack_into("<I", buf, 12, common.crc32(bytes(buf[HDR:])))
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser(description="UDS 刷写最小实现（34/36/37 + 31 校验）")
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--req-id", default="0x7E0")
    ap.add_argument("--resp-id", default="0x7E8")
    ap.add_argument("--addr", default="0x800F0000", help="下载目标地址（固件固定为 0x800F0000）")
    ap.add_argument("--size", type=int, default=32768, help="镜像大小（32 KB）")
    ap.add_argument("--block", type=int, default=256, help="0x36 每块数据字节数")
    ap.add_argument("--reset", action="store_true", help="成功后发 11 01 复位")
    args = ap.parse_args()
    req_id, resp_id = int(args.req_id, 0), int(args.resp_id, 0)
    addr = int(args.addr, 0)

    bus = common.RawCan(args.channel)
    tp = common.IsoTp(bus, rx_id=resp_id, tx_id=req_id, n_bs=5.0, n_cr=5.0)
    cl = common.UdsClient(tp, p2star=8.0)
    img = build_image(args.size)
    crc_local = struct.unpack_from("<I", img, 12)[0]

    def step(name, req, want_pos, want_nrc=None):
        t0 = time.perf_counter()
        resp, rtt, pend = cl.request(req)
        rtt = int((time.perf_counter() - t0) * 1e6)
        if want_nrc is not None:
            ok = common.nrc_of(resp) == want_nrc
        else:
            ok = common.is_positive(resp, req[0])
        print("  %-26s %-34s %8d us %s%s" % (name, common.describe(resp)[:34], rtt,
                                             "PASS" if ok else "FAIL",
                                             (" pending×%d" % pend) if pend else ""))
        return ok, resp, rtt

    print("== mini_flasher: %s -> 0x%03X/0x%03X  addr=0x%08X size=%d B block=%d ==" %
          (args.channel, req_id, resp_id, addr, args.size, args.block))
    ok = True
    ok &= step("10 02 编程会话", [0x10, 0x02], True)[0]
    seed_resp, _, _ = cl.request([0x27, 0x01])
    if len(seed_resp) != 6:
        print("  27 01 取种子失败: %s" % common.describe(seed_resp))
        return 1
    seed = struct.unpack(">I", seed_resp[2:6])[0]
    key = common.security_key(seed, 1)
    ok &= step("27 01/02 解锁", bytes([0x27, 0x02]) + struct.pack(">I", key), True)[0]
    print("     seed=0x%08X key=0x%08X" % (seed, key))

    # 34: dfi=0x00, alfi=0x44 (4B 地址 + 4B 长度)
    req34 = bytes([0x34, 0x00, 0x44]) + struct.pack(">II", addr, args.size)
    t0 = time.perf_counter()
    st_ok, resp, rtt = step("34 请求下载(擦除)", req34, True)
    ok &= st_ok
    blk = struct.unpack(">H", resp[2:4])[0] if len(resp) >= 4 and st_ok else 0
    if blk == 0 or blk > 4096:
        blk = args.block + 2
    data_max = max(1, blk - 2)
    print("     maxNumberOfBlockLength=%d (数据 %d B/块)" % (blk, data_max))

    # 36 分块
    sent = 0
    seq = 1
    blocks = 0
    t_all0 = time.perf_counter()
    while sent < args.size:
        n = min(data_max, args.size - sent)
        chunk = img[sent:sent + n]          # 整个镜像（含 256 B 头）下发
        req = bytes([0x36, seq]) + chunk
        resp, rtt, pend = cl.request(req)
        if not (len(resp) == 2 and resp[0] == 0x76 and resp[1] == seq):
            print("  36 block %d FAIL: %s" % (seq, common.describe(resp)))
            return 1
        sent += n
        blocks += 1
        seq = seq + 1 if seq < 0xFF else 1
    dt = time.perf_counter() - t_all0
    kbps = args.size / dt / 1024
    print("  36 传输数据                  %d 块 %d B  %.1f KB/s  总耗时 %.3f s" %
          (blocks, sent, kbps, dt))
    ok &= step("37 请求退出传输", [0x37], True)[0]

    # 31 01 FF00 读回 CRC
    st_ok, resp, _ = step("31 01 FF00 校验 CRC32", [0x31, 0x01, 0xFF, 0x00], True)
    crc_ecu = struct.unpack(">I", resp[5:9])[0] if st_ok and len(resp) >= 9 else 0
    crc_ok = st_ok and crc_ecu == crc_local
    print("     ECU CRC32=0x%08X 本地=0x%08X -> %s" % (crc_ecu, crc_local, "MATCH" if crc_ok else "MISMATCH"))
    ok &= crc_ok
    st_ok, resp, _ = step("31 01 FF01 依赖检查", [0x31, 0x01, 0xFF, 0x01], True)
    ok &= st_ok and len(resp) >= 5 and resp[4] == 0x00
    if args.reset:
        ok &= step("11 01 复位", [0x11, 0x01], True)[0]
    bus.close()
    print("[mini_flasher] %s 总耗时 %.2f s  刷写速率 %.1f KB/s" %
          ("PASS" if ok else "FAIL", time.perf_counter() - t0, kbps))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
