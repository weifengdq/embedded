#!/usr/bin/env python3
"""TC397 sdmmc shell driver (Windows/Linux, letter-shell over UART 921600).

Keeps the port open the whole time (the board DTR-resets on open, so opening a
fresh port for every command swallows it), drains the boot banner, then sends a
list of commands one after another and prints every response.

Usage:
    python sd_shell.py -p COM169 "sd init" "sd info" "sd bench 8"
    python sd_shell.py -p COM169 -w 3.0 "sd big w 200000 320"
    python sd_shell.py -p COM169 --listen 5        # just watch the boot log
"""
import argparse
import sys
import time

import serial


def drain(ser, seconds, label=None, out=None, idle=1.0):
    """Read whatever arrives during `seconds`; return it as text.

    Returns early once `idle` seconds pass with no new data, so a short wait
    still returns immediately, while a long wait can cover a slow operation
    (e.g. `sd mkfs`, which prints nothing until it is done)."""
    end = time.time() + seconds
    buf = bytearray()
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            end = max(end, time.time() + idle)
        else:
            time.sleep(0.01)
    text = buf.decode("utf-8", "replace")
    if out is not None:
        out.append(text)
    if label:
        print(f"----- {label} -----")
    sys.stdout.write(text)
    sys.stdout.flush()
    return text


def main():
    ap = argparse.ArgumentParser(description="TC397 sdmmc shell driver")
    ap.add_argument("-p", "--port", default="COM169")
    ap.add_argument("-b", "--baud", type=int, default=921600)
    ap.add_argument("-w", "--wait", type=float, default=2.0,
                    help="seconds to wait for each command's output")
    ap.add_argument("--idle", type=float, default=1.0,
                    help="consider a command finished after N s without data")
    ap.add_argument("--boot", type=float, default=3.0,
                    help="seconds to wait for the boot banner after open")
    ap.add_argument("--listen", type=float, default=0.0,
                    help="only listen for N seconds, send nothing")
    ap.add_argument("cmds", nargs="*", help="commands to send in order")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1,
                        bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
                        stopbits=serial.STOPBITS_ONE)
    log = []
    try:
        time.sleep(0.2)
        drain(ser, args.boot, "boot", log, args.idle)

        if args.listen:
            drain(ser, args.listen, "listen", log, args.idle)
            return 0

        for cmd in args.cmds:
            idle = args.idle
            # per-command idle override, e.g. "@180:sd mkfs fat32" waits up to
            # 180 s of silence (sd mkfs prints nothing until it is finished)
            if cmd.startswith("@"):
                head, _, rest = cmd.partition(":")
                try:
                    idle = float(head[1:])
                    cmd = rest
                except ValueError:
                    pass
            ser.write((cmd + "\r\n").encode())
            sys.stdout.write(f"\n>>> {cmd}\n")
            sys.stdout.flush()
            drain(ser, args.wait, None, log, idle)
    finally:
        ser.close()

    with open("sd_shell_log.txt", "a", encoding="utf-8") as f:
        f.write("".join(log))
    print(f"\n[log appended to sd_shell_log.txt]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
