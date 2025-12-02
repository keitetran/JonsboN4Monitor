import serial
import time

# COM port của USB CDC (ESP32-P4 USB device)
# Kiểm tra Device Manager để tìm COM port mới (ví dụ COM4, COM5...)
PORT = "COM4"  # ĐỔI THÀNH COM PORT ĐÚNG CỦA BẠN
BAUD = 115200

# File chứa dữ liệu sensor
DUMP_FILE = "server/sensors.txt"

ser = serial.Serial(PORT, BAUD, timeout=1)
# QUAN TRỌNG: Set DTR=True để ESP32 biết host đã mở port (CDC ready)
# ESP32 sẽ chuyển từ screen_loading sang screen khi DTR=1
ser.dtr = True
ser.rts = True
print(f"Đã mở {PORT} cho gửi dữ liệu")
print("Đã set DTR=True, RTS=True - ESP32 sẽ nhận biết CDC ready")

# Đọc file dump.txt
try:
    with open(DUMP_FILE, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f if line.strip()]
    print(f"Đã đọc {len(lines)} dòng từ {DUMP_FILE}")
except FileNotFoundError:
    print(f"Error: File {DUMP_FILE} không tồn tại")
    ser.close()
    exit(1)

while True:
    # Gửi từng dòng từ dump.txt
    for line in lines:
        # Đảm bảo có newline ở cuối
        msg = line + "\n"
        print("send:", msg.strip())
        ser.write(msg.encode("utf-8"))
        ser.flush()
        time.sleep(0.1)  # Delay giữa các dòng
    
    time.sleep(1)  # Đợi 1 giây trước khi gửi lại

