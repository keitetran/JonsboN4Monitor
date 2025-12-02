#pragma once

/**
 * Khởi động MQTT client để kết nối tới MQTT broker.
 *
 * MQTT client sẽ kết nối tới broker được cấu hình trong Kconfig.
 * Chỉ khởi tạo khi CONFIG_MQTT_ENABLE được bật.
 *
 * Hàm này an toàn khi gọi nhiều lần; client chỉ được khởi tạo một lần.
 */
void mqtt_comm_start(void);

