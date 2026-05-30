import serial
import time
import sys

try:
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem5C372138861'
    ser = serial.Serial(port, 115200, timeout=1)
    start_time = time.time()
    print("Listening to serial port for 30 seconds...")
    while time.time() - start_time < 30:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
except Exception as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
