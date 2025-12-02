#!/usr/bin/env python3
"""
Script test giao tiếp USB với ESP32 qua cổng COM trên Windows.
Đọc tín hiệu từ ESP32 (W/S) và hiển thị để debug.
"""

import serial
import time
import sys

# Cấu hình cổng COM
COM_PORT = "COM4"  # Thay đổi nếu cần
BAUD_RATE = 115200  # Có thể cần điều chỉnh tùy ESP32

def test_esp32_communication():
    """Test giao tiếp với ESP32 qua USB CDC."""
    print(f"Đang mở cổng {COM_PORT} với baud rate {BAUD_RATE}...")
    
    try:
        # Mở cổng serial
        ser = serial.Serial(
            port=COM_PORT,
            baudrate=BAUD_RATE,
            timeout=1,
            write_timeout=1
        )
        
        # QUAN TRỌNG: Set DTR và RTS thành True để ESP32 biết host đã mở port
        # ESP32 sẽ không gửi dữ liệu nếu DTR/RTS không được set
        ser.dtr = True
        ser.rts = True
        
        # Đợi một chút để ESP32 nhận biết trạng thái
        time.sleep(0.1)
        
        print(f"✓ Đã mở {COM_PORT} thành công")
        print(f"✓ Đã set DTR=True, RTS=True - ESP32 sẽ biết host đã sẵn sàng")
        print(f"Đang chờ tín hiệu từ ESP32...")
        print(f"Chạm vào màn hình ESP32 để xem tín hiệu 'W' (wake)")
        print(f"Đợi 30 giây để xem tín hiệu 'S' (sleep)")
        print(f"Nhấn Ctrl+C để dừng\n")
        
        byte_count = 0
        start_time = time.time()
        
        while True:
            # Đọc dữ liệu từ ESP32
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                byte_count += len(data)
                
                # Hiển thị từng byte
                for byte in data:
                    byte_val = byte if isinstance(byte, int) else ord(byte)
                    char = chr(byte_val) if 32 <= byte_val <= 126 else '.'
                    
                    # Kiểm tra tín hiệu đặc biệt
                    if byte_val == ord('W'):
                        print(f"[{time.time() - start_time:.2f}s] ✓ Nhận được tín hiệu 'W' (0x57) - ESP32: Màn hình BẬT")
                    elif byte_val == ord('S'):
                        print(f"[{time.time() - start_time:.2f}s] ✓ Nhận được tín hiệu 'S' (0x53) - ESP32: Màn hình TẮT")
                    else:
                        print(f"[{time.time() - start_time:.2f}s] Nhận được byte: 0x{byte_val:02x} ('{char}')")
                
                print(f"  → Tổng cộng đã nhận {byte_count} bytes\n")
            else:
                # Không có dữ liệu, đợi một chút
                time.sleep(0.1)
                
                # Hiển thị status mỗi 5 giây
                elapsed = time.time() - start_time
                if int(elapsed) % 5 == 0 and elapsed > 0:
                    print(f"[{elapsed:.0f}s] Đang chờ tín hiệu từ ESP32... (đã nhận {byte_count} bytes)")
                    time.sleep(0.9)  # Tránh in nhiều lần
                    
    except serial.SerialException as e:
        print(f"✗ Lỗi mở cổng {COM_PORT}: {e}")
        print(f"\nKiểm tra:")
        print(f"  1. ESP32 đã được cắm vào USB?")
        print(f"  2. Cổng COM đúng chưa? (hiện tại: {COM_PORT})")
        print(f"  3. Cổng COM có bị chương trình khác sử dụng không?")
        print(f"  4. Driver USB CDC đã được cài đặt?")
        sys.exit(1)
        
    except KeyboardInterrupt:
        print(f"\n\nĐã dừng.")
        print(f"Tổng kết: Đã nhận {byte_count} bytes từ ESP32")
        if ser.is_open:
            ser.close()
        sys.exit(0)
        
    except Exception as e:
        print(f"✗ Lỗi không mong đợi: {e}")
        if ser.is_open:
            ser.close()
        sys.exit(1)


if __name__ == "__main__":
    print("=" * 60)
    print("ESP32 USB Communication Test")
    print("=" * 60)
    print()
    
    # Kiểm tra xem có thể import serial không
    try:
        import serial
    except ImportError:
        print("✗ Lỗi: Chưa cài đặt thư viện pyserial")
        print("Cài đặt bằng lệnh: pip install pyserial")
        sys.exit(1)
    
    test_esp32_communication()

