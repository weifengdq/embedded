#!/usr/bin/env python3
"""Serial reader for TC387 UART4 (P00.9/P00.12, 921600) on Linux"""
import serial, time, sys, argparse

def main():
    parser = argparse.ArgumentParser(description="TC387 serial monitor")
    parser.add_argument("--port", default="/dev/ttyACM0", help="serial port (default /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=921600, help="baud (default 921600)")
    parser.add_argument("--duration", type=int, default=15, help="seconds to read (0 = forever)")
    parser.add_argument("--cmd", default="", help="command to send after open, e.g. 'help'")
    args = parser.parse_args()

    print(f"Opening {args.port} at {args.baud}...")
    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        )
    except serial.SerialException as e:
        print(f"ERROR opening {args.port}: {e}", file=sys.stderr)
        print("Try: ls /dev/serial/by-id/* && ls -l /dev/ttyACM* /dev/ttyUSB*", file=sys.stderr)
        return 1

    # Clear buffers
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    if args.cmd:
        time.sleep(0.2)
        ser.write((args.cmd + "\r\n").encode())
        print(f">>> {args.cmd}")

    print(f"Connected. Reading {'forever' if args.duration==0 else f'for {args.duration}s'}... Ctrl+C to stop")
    print("="*60)
    start = time.time()
    total = 0
    try:
        while True:
            if args.duration and (time.time()-start) >= args.duration:
                break
            n = ser.in_waiting
            if n:
                data = ser.read(n)
                total += len(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            else:
                time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    print(f"\n{'='*60}")
    print(f"Total {total} bytes in {time.time()-start:.1f}s")
    if total == 0:
        print("*** NO DATA ***")
        print("1) Check baud 921600, port is /dev/ttyACM0 (CH340) not /dev/ttyUSB0 (DAP)")
        print("2) Press RESET button on TriBoard")
        print("3) Check P00.9/P00.12 wiring")
        print("4) If garbled, verify oversampling 16 and FIFO level 1 (see UART_Logging.c)")
    return 0 if total>0 else 1

if __name__ == "__main__":
    sys.exit(main())
