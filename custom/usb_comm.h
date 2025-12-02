#pragma once

#include "lvgl.h"
#include "gui_guider.h"

/**
 * Khởi động task nhận dữ liệu từ host qua USB CDC device (cổng USB thứ 2).
 *
 * ESP32-P4 hoạt động như USB CDC device qua esp_tinyusb, tự động tạo COM port mới trên PC.
 * Cổng USB thứ 1 (COM3) vẫn dùng cho flash/debug qua USB Serial/JTAG.
 * Không cần phần cứng thêm (USB-UART bridge), chỉ cần cắm USB-C thứ 2 vào PC.
 *
 * Hàm này an toàn khi gọi nhiều lần; task chỉ được tạo một lần.
 */
void usb_comm_start(void);

/**
 * Gửi tín hiệu backlight state qua USB CDC để host biết khi nào cần gửi dữ liệu sensor.
 * 
 * @param is_on true nếu backlight đang bật, false nếu đang tắt
 */
void usb_comm_send_backlight_state(bool is_on);


