import serial
import time
import sys

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem5C372138861'
baud = 115200

try:
    ser = serial.Serial(port, baud, timeout=1)
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    time.sleep(0.1)
    ser.setDTR(False) # Some boards need DTR false after RTS

    print("Listening to boot logs...")
    start_time = time.time()
    while time.time() - start_time < 15:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
except Exception as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
