import serial
import time
import sys

try:
    ser = serial.Serial('/dev/cu.usbmodem5C372138861', 115200, timeout=1)
    start_time = time.time()
    print("Listening to serial port for 300 seconds...")
    while time.time() - start_time < 300:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
                sys.stdout.flush()
except Exception as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
