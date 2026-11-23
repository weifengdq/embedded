#!/usr/bin/env python3
"""串口抓取工具: 打开 /dev/ttyACM0, 可选发送命令, 读取固定时长或直到匹配到 expect."""
import argparse, fcntl, re, sys, time
import serial

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--baud', type=int, default=921600)
    ap.add_argument('--secs', type=float, default=5.0)
    ap.add_argument('--send', default=None, help='发送的文本(自动补 \r\n), 可用 ; 分隔多条')
    ap.add_argument('--send-delay', type=float, default=0.3)
    ap.add_argument('--expect', default=None, help='正则, 匹配到即提前退出')
    ap.add_argument('--out', default=None)
    ap.add_argument('--raw', action='store_true')
    a = ap.parse_args()
    port = serial.Serial(a.port, a.baud, timeout=0.05)
    # 独占打开：防止两个 agent 同时读取同一串口把字节流"分尸"
    try:
        fcntl.ioctl(port.fileno(), 0x540E)  # TIOCEXCL
    except Exception:
        pass
    port.reset_input_buffer()
    buf = bytearray()
    t0 = time.time()
    sent = False
    pat = re.compile(a.expect.encode() if a.expect else b'')
    while time.time() - t0 < a.secs:
        if a.send and not sent and time.time() - t0 > a.send_delay:
            for line in a.send.split(';'):
                port.write(line.encode() + b'\r\n')
                port.flush()
                time.sleep(0.05)
            sent = True
        chunk = port.read(512)
        if chunk:
            buf += chunk
            if a.expect and pat.search(bytes(buf)):
                break
    port.close()
    text = bytes(buf).decode('utf-8', 'replace')
    if a.raw:
        sys.stdout.write(text)
    else:
        print(text, end='')
    if a.out:
        with open(a.out, 'w') as f:
            f.write(text)
    if a.expect:
        return 0 if pat.search(bytes(buf)) else 3
    return 0

if __name__ == '__main__':
    sys.exit(main())
