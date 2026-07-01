#!/usr/bin/env python3
"""Serial reader for STM32H503 debug UART (COM80, 2Mbaud)"""
import serial
import time
import sys

def main():
    print(f"Opening COM80 at 2000000 baud...")
    try:
        ser = serial.Serial(
            port='COM80',
            baudrate=2000000,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        )
    except serial.SerialException as e:
        print(f"ERROR opening COM80: {e}")
        return 1

    # Try toggling DTR to trigger reset on some boards
    ser.dtr = False
    time.sleep(0.1)
    ser.dtr = True
    
    print(f"Connected. Reading for 15 seconds...")
    print("=" * 60)
    
    start = time.time()
    total_bytes = 0
    
    try:
        while time.time() - start < 15:
            n = ser.in_waiting
            if n > 0:
                data = ser.read(n)
                total_bytes += len(data)
                # Print raw data - try multiple encodings
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            else:
                time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    
    print(f"\n{'=' * 60}")
    print(f"Total received: {total_bytes} bytes in {time.time()-start:.1f}s")
    
    if total_bytes == 0:
        print("\n*** NO DATA RECEIVED ***")
        print("Possible causes:")
        print("1. Device not booting (hard fault / clock issue)")
        print("2. TX/RX pins swapped or not connected")
        print("3. Wrong baud rate")
        print("4. Device held in reset")
        print("5. COM port conflict (check if another app has COM80 open)")
    
    return 0 if total_bytes > 0 else 1

if __name__ == '__main__':
    sys.exit(main())
