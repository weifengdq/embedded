#!/usr/bin/env python3
"""
udp2vcan.py - 纯 python-can 的 cannelloni v2 备用桥（不需要编译 cannelloni）

用途：
  1) 没有/不想构建 stock cannelloni 时，仍能把主机 vcan0 接到板端 can_gateway；
  2) 调试 cannelloni v2 线上格式（逐字段解析，--verbose 打印每帧）。

数据通路：
  主机 vcan0  <-- python-can -->  本脚本  <-- UDP -->  板 192.168.2.100:4000 (app_can_gw)

线上格式（与 ref2/cannelloni v2.1.2 一致，全部多字节字段大端）：
  +0 u8 version=2 | +1 u8 op=0(DATA) | +2 u8 seq | +3 u16 count
  每帧：u32 can_id(bit31=EFF,bit30=RTR) | u8 len(bit7=FD) | [u8 flags] | data[len&0x7F]

用法：
  ./udp2vcan.py --iface vcan0 --board 192.168.2.100 --board-port 4000 --listen-port 4000
  ./udp2vcan.py --verbose          # 打印每个解析到的帧
"""
import argparse
import socket
import struct
import sys
import threading
import time

try:
    import can
except ImportError:
    sys.exit("缺少 python-can：$HOME/pyenv/bin/pip install python-can")

VERSION = 2
OP_DATA = 0
CANFD_FRAME = 0x80
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CANFD_BRS = 0x01


def encode_packet(frames, seq):
    """把 can.Message 列表编码成 cannelloni v2 UDP 载荷。"""
    out = bytearray(struct.pack(">BBBH", VERSION, OP_DATA, seq & 0xFF, len(frames)))
    for f in frames:
        can_id = f.arbitration_id
        if f.is_extended_id:
            can_id |= CAN_EFF_FLAG
        if f.is_remote_frame:
            can_id |= CAN_RTR_FLAG
        out += struct.pack(">I", can_id)
        data = bytes(f.data)
        if f.is_fd:
            out.append(CANFD_FRAME | (len(data) & 0x7F))
            out.append(CANFD_BRS if getattr(f, "bitrate_switch", False) else 0)
        else:
            out.append(len(data) & 0x7F)
        if not f.is_remote_frame:
            out += data
    return bytes(out)


def decode_packet(pkt):
    """解析 cannelloni v2 UDP 载荷，返回 (frames, err)。"""
    if len(pkt) < 5:
        return [], "short header"
    ver, op, seq, count = struct.unpack(">BBBH", pkt[:5])
    if ver != VERSION:
        return [], "bad version %d" % ver
    if op != OP_DATA:
        return [], "bad op %d" % op
    off = 5
    frames = []
    for _ in range(count):
        if off + 5 > len(pkt):
            return frames, "truncated frame header"
        can_id = struct.unpack(">I", pkt[off:off + 4])[0]
        off += 4
        lenb = pkt[off]
        off += 1
        length = lenb & 0x7F
        is_fd = bool(lenb & CANFD_FRAME)
        is_ext = bool(can_id & CAN_EFF_FLAG)
        is_rtr = bool(can_id & CAN_RTR_FLAG)
        brs = False
        if is_fd:
            if off >= len(pkt):
                return frames, "truncated fd flags"
            brs = bool(pkt[off] & CANFD_BRS)
            off += 1
        if is_rtr:
            data = b""
        else:
            data = pkt[off:off + length]
            off += length
        frames.append(can.Message(
            arbitration_id=can_id & 0x1FFFFFFF,
            is_extended_id=is_ext,
            is_remote_frame=is_rtr,
            is_fd=is_fd,
            bitrate_switch=brs,
            dlc=len(data),
            data=data,
            is_rx=True,
        ))
    return frames, None


class Bridge:
    def __init__(self, args):
        self.args = args
        self.bus = can.Bus(interface="socketcan", channel=args.iface)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((args.listen_ip, args.listen_port))
        self.sock.settimeout(0.05)
        self.board = (args.board, args.board_port)
        self.stop = threading.Event()
        self.seq = 0
        self.st = dict(rx_udp=0, tx_udp=0, rx_can=0, tx_can=0, bad=0, drop=0)
        self.txq = []
        self.txq_lock = threading.Lock()

    # ---- UDP -> vcan ----
    def udp_loop(self):
        while not self.stop.is_set():
            try:
                pkt, _src = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break
            self.st["rx_udp"] += 1
            frames, err = decode_packet(pkt)
            if err:
                self.st["bad"] += 1
                if self.args.verbose:
                    print("[udp2vcan] bad packet: %s (%d B)" % (err, len(pkt)))
                continue
            for f in frames:
                try:
                    self.bus.send(f)
                    self.st["tx_can"] += 1
                except can.CanError as e:
                    self.st["drop"] += 1
                    if self.args.verbose:
                        print("[udp2vcan] vcan send failed: %s" % e)

    # ---- vcan -> UDP ----
    def can_loop(self):
        last = time.time()
        while not self.stop.is_set():
            try:
                msg = self.bus.recv(timeout=self.args.flush_ms / 2000.0)
            except can.CanError:
                continue
            if msg is not None:
                msg.is_rx = False
                with self.txq_lock:
                    self.txq.append(msg)
                    if len(self.txq) >= self.args.batch:
                        batch, self.txq = self.txq, []
                    else:
                        batch = []
                if batch:
                    self.flush(batch)
            now = time.time()
            if (now - last) * 1000.0 >= self.args.flush_ms:
                last = now
                with self.txq_lock:
                    batch, self.txq = self.txq, []
                if batch:
                    self.flush(batch)

    def flush(self, batch):
        self.seq = (self.seq + 1) & 0xFF
        pkt = encode_packet(batch, self.seq)
        try:
            self.sock.sendto(pkt, self.board)
            self.st["tx_udp"] += 1
            self.st["rx_can"] += len(batch)
            if self.args.verbose:
                for f in batch:
                    print("[udp2vcan] can -> udp id=0x%X len=%d data=%s" %
                          (f.arbitration_id, len(f.data), f.data.hex()))
        except OSError as e:
            self.st["drop"] += len(batch)
            if self.args.verbose:
                print("[udp2vcan] udp send failed: %s" % e)

    def report_loop(self):
        while not self.stop.wait(self.args.report):
            s = self.st
            print("[udp2vcan] udp_rx=%d frame, can_tx=%d, can_rx=%d, udp_tx=%d pkt, bad=%d, drop=%d"
                  % (s["rx_udp"], s["tx_can"], s["rx_can"], s["tx_udp"], s["bad"], s["drop"]))

    def run(self):
        threads = [
            threading.Thread(target=self.udp_loop, daemon=True),
            threading.Thread(target=self.can_loop, daemon=True),
            threading.Thread(target=self.report_loop, daemon=True),
        ]
        for t in threads:
            t.start()
        print("[udp2vcan] %s <-> UDP %s:%d (listen %s:%d)  batch=%d flush=%dms" %
              (self.args.iface, self.board[0], self.board[1],
               self.args.listen_ip, self.args.listen_port, self.args.batch, self.args.flush_ms))
        try:
            while True:
                time.sleep(0.2)
        except KeyboardInterrupt:
            pass
        finally:
            self.stop.set()
            self.bus.shutdown()
            self.sock.close()
            print("[udp2vcan] stopped:", self.st)


def main():
    ap = argparse.ArgumentParser(description="cannelloni v2 UDP <-> SocketCAN bridge (python-can)")
    ap.add_argument("--iface", default="vcan0", help="SocketCAN 接口（默认 vcan0）")
    ap.add_argument("--board", default="192.168.2.100", help="板端 IP")
    ap.add_argument("--board-port", type=int, default=4000, help="板端网关 UDP 端口")
    ap.add_argument("--listen-ip", default="0.0.0.0", help="本机监听地址")
    ap.add_argument("--listen-port", type=int, default=4000, help="本机监听 UDP 端口（板端回包目的端口）")
    ap.add_argument("--batch", type=int, default=16, help="单包最多帧数（<=16，与 MCU 一致）")
    ap.add_argument("--flush-ms", type=float, default=1.0, help="攒包超时 ms（越小延迟越低）")
    ap.add_argument("--report", type=float, default=5.0, help="统计打印周期 s")
    ap.add_argument("--verbose", action="store_true", help="打印每个解析到的帧")
    args = ap.parse_args()
    Bridge(args).run()


if __name__ == "__main__":
    main()
