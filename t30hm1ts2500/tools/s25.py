#!/usr/bin/env python3
"""s25.py - S2500 调试串口助手

用法:
  ./s25.py -p /dev/ttyACM0 -c "s2500 info"        # 执行单条命令并打印输出
  ./s25.py -p /dev/ttyACM0 -c "s2500 status" -c "netif"
  ./s25.py --auto stm32 -c "help"                 # 按 VID:PID 自动找口
  ./s25.py -p /dev/ttyACM0 -i                     # 交互模式 (Ctrl-] 退出)

自动识别 (--auto):
  stm32  -> 0483:3754 CDC (STLINK-V3 VCP)
  h723   -> 同 stm32 (STM32H723 板)
  s32k   -> 1a86:55d3 第 1 个 (CH343P)
  rk3506 -> 1a86:55d3 第 2 个 (CH343P)
  hpm    -> 0403:6010 第 2 个接口 (FT2232H channel B)
  tc387  -> 058b:0043 (DAP JDS TriBoard)
"""
import argparse
import glob
import os
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("需要 pyserial: pip3 install pyserial")

BOARDS = {
    "stm32":  ("0483", "3754", 1),
    "h723":   ("0483", "3754", 1),   # STM32H723 复用同一块 STLINK-V3 的 VCP
    "s32k":   ("1a86", "55d3", 0),
    "rk3506": ("1a86", "55d3", 1),
    "hpm":    ("0403", "6010", 1),
    "tc387":  ("058b", "0043", 0),
}


def find_port(board):
    vid, pid, index = BOARDS[board.lower()]
    cands = []
    for p in list_ports.comports():
        if p.vid is None:
            continue
        if f"{p.vid:04x}" == vid and f"{p.pid:04x}" == pid:
            cands.append(p.device)
    cands.sort()
    if not cands:
        raise SystemExit(f"未找到 {board} 对应的串口 ({vid}:{pid})")
    if index >= len(cands):
        index = 0
    return cands[index]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port")
    ap.add_argument("--auto", choices=list(BOARDS))
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-c", "--cmd", action="append", default=[])
    ap.add_argument("-i", "--interactive", action="store_true")
    ap.add_argument("-t", "--timeout", type=float, default=2.0)
    ap.add_argument("--idle", type=float, default=0.25, help="判定输出结束的静默时间")
    args = ap.parse_args()

    port = args.port
    if not port:
        if not args.auto:
            sys.exit("需要 -p 或 --auto")
        port = find_port(args.auto)

    if port.startswith("/dev/serial/by-id/"):
        pass
    ser = serial.Serial(port, args.baud, timeout=0.05)
    ser.reset_input_buffer()
    print(f"# port={port} baud={args.baud}", file=sys.stderr)

    if args.interactive:
        print("交互模式, Ctrl-] 退出", file=sys.stderr)
        try:
            while True:
                data = ser.read(256)
                if data:
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
                if sys.stdin in __import__("select").select([sys.stdin], [], [], 0)[0]:
                    line = sys.stdin.readline()
                    if line.strip() == "\x1d":
                        break
                    ser.write(line.encode())
        except KeyboardInterrupt:
            pass
        ser.close()
        return

    for cmd in args.cmd:
        ser.reset_input_buffer()
        ser.write((cmd + "\r").encode())
        out = b""
        deadline = time.time() + args.timeout
        last = time.time()
        while time.time() < deadline:
            chunk = ser.read(512)
            if chunk:
                out += chunk
                last = time.time()
            elif time.time() - last > args.idle and out:
                break
        text = out.decode("utf-8", "replace")
        # 去掉回显的输入行与末尾提示符
        lines = [ln for ln in text.replace("\r", "").split("\n")]
        while lines and not lines[0].strip():
            lines.pop(0)
        if lines and cmd in lines[0]:
            lines.pop(0)
        print(f"$ {cmd}")
        print("\n".join(lines).rstrip())
        print()
    ser.close()


if __name__ == "__main__":
    main()
