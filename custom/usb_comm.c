#include "usb_comm.h"
#include "esp_log.h"
#include "gui_guider.h"
#include "lvgl_port_v9.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

// USB CDC buffer size
#define USB_CDC_BUF_SIZE 64

// Extern guider_ui để access screen widgets
extern lv_ui guider_ui;

static bool s_usb_comm_started = false;      // USB comm đã khởi động chưa
static bool s_tinyusb_installed = false;     // TinyUSB đã cài đặt chưa
static bool s_cdc_ready = false;             // Trạng thái CDC ready (host đã mở port)
static bool s_screen_switched = false;       // Đã chuyển từ screen_loading sang screen chưa
static bool s_data_received = false;         // Đã nhận được dữ liệu từ host chưa
static bool s_screen_switch_pending = false; // Đã gửi signal chuyển screen vào queue chưa
static char s_label_version[64] = "";        // Lưu giá trị label_version để join vào label_account

// Queue để gửi tín hiệu backlight state (tránh block khi gửi)
static QueueHandle_t s_backlight_signal_queue = NULL;

// Queue để signal cần chuyển screen (1 = chuyển screen, 0 = không làm gì)
static QueueHandle_t s_screen_switch_queue = NULL;

// Struct để map label name đến widget
typedef enum {
  WIDGET_TYPE_LABEL,
  WIDGET_TYPE_BAR,
  WIDGET_TYPE_ARC,
} widget_type_t;

typedef struct {
  const char *label_name;    // Tên label như "label_storage_1", "label_fan1_value", etc.
  lv_obj_t **widget_ptr;     // Pointer to widget pointer trong guider_ui (để luôn lấy giá trị mới nhất)
  widget_type_t widget_type; // Loại widget
} label_widget_map_t;

// Mapping table: map label name đến widget pointer
static const label_widget_map_t s_label_map[] = {
    // Storage labels
    {"label_storage_1", &guider_ui.screen_label_storage_1, WIDGET_TYPE_LABEL},
    {"label_storage_total_1", &guider_ui.screen_label_storage_total_1, WIDGET_TYPE_LABEL},
    {"label_storage_2", &guider_ui.screen_label_storage_2, WIDGET_TYPE_LABEL},
    {"label_storage_total_2", &guider_ui.screen_label_storage_total_2, WIDGET_TYPE_LABEL},
    {"label_storage_3", &guider_ui.screen_label_storage_3, WIDGET_TYPE_LABEL},
    {"label_storage_total_3", &guider_ui.screen_label_storage_total_3, WIDGET_TYPE_LABEL},
    {"label_storage_4", &guider_ui.screen_label_storage_4, WIDGET_TYPE_LABEL},
    {"label_storage_total_4", &guider_ui.screen_label_storage_total_4, WIDGET_TYPE_LABEL},

    // Storage arcs (cập nhật khi nhận label_storage_x)
    {"arc_storage_1", &guider_ui.screen_arc_storage_1, WIDGET_TYPE_ARC},
    {"arc_storage_2", &guider_ui.screen_arc_storage_2, WIDGET_TYPE_ARC},
    {"arc_storage_3", &guider_ui.screen_arc_storage_3, WIDGET_TYPE_ARC},
    {"arc_storage_4", &guider_ui.screen_arc_storage_4, WIDGET_TYPE_ARC},

    // Fan labels
    {"label_fan2_value", &guider_ui.screen_label_fan2_value, WIDGET_TYPE_LABEL},
    {"label_fan3_value", &guider_ui.screen_label_fan3_value, WIDGET_TYPE_LABEL},

    // CPU labels và bar
    {"label_cpu_usage", &guider_ui.screen_label_cpu_usage, WIDGET_TYPE_LABEL},
    {"label_cpu_usage_per", &guider_ui.screen_label_cpu_usage_per, WIDGET_TYPE_LABEL},
    {"bar_cpu_usage", &guider_ui.screen_bar_cpu_usage, WIDGET_TYPE_BAR},

    // RAM labels và bar
    {"label_ram_usage", &guider_ui.screen_label_ram_usage, WIDGET_TYPE_LABEL},
    {"label_ram_usage_per", &guider_ui.screen_label_ram_usage_per, WIDGET_TYPE_LABEL},
    {"bar_ram_usage", &guider_ui.screen_bar_ram_usage, WIDGET_TYPE_BAR},

    // GPU labels và bar
    {"label_gpu_usage", &guider_ui.screen_label_gpu_usage, WIDGET_TYPE_LABEL},
    {"label_gpu_usage_per", &guider_ui.screen_label_gpu_usage_per, WIDGET_TYPE_LABEL},
    {"label_gpu_fan_speed", &guider_ui.screen_label_gpu_fan_speed, WIDGET_TYPE_LABEL},
    {"bar_gpu_usage", &guider_ui.screen_bar_gpu_usage, WIDGET_TYPE_BAR},

    // Temperature labels - drive
    {"label_temp_drive1", &guider_ui.screen_label_temp_drive1, WIDGET_TYPE_LABEL},
    {"label_temp_drive2", &guider_ui.screen_label_temp_drive2, WIDGET_TYPE_LABEL},
    {"label_temp_drive3", &guider_ui.screen_label_temp_drive3, WIDGET_TYPE_LABEL},
    {"label_temp_drive4", &guider_ui.screen_label_temp_drive4, WIDGET_TYPE_LABEL},
    {"label_temp_drive5", &guider_ui.screen_label_temp_drive5, WIDGET_TYPE_LABEL},

    // Temperature labels - nvme
    {"label_temp_nvme1", &guider_ui.screen_label_temp_nvme1, WIDGET_TYPE_LABEL},
    {"label_temp_nvme2", &guider_ui.screen_label_temp_nvme2, WIDGET_TYPE_LABEL},
    {"label_temp_nvme3", &guider_ui.screen_label_temp_nvme3, WIDGET_TYPE_LABEL},
    {"label_temp_nvme4", &guider_ui.screen_label_temp_nvme4, WIDGET_TYPE_LABEL},
    {"label_temp_nvme5", &guider_ui.screen_label_temp_nvme5, WIDGET_TYPE_LABEL},

    // Temperature labels - system
    {"label_temp_motherboard", &guider_ui.screen_label_temp_motherboard, WIDGET_TYPE_LABEL},
    {"label_temp_chipset", &guider_ui.screen_label_temp_chipset, WIDGET_TYPE_LABEL},
    {"label_temp_cpu", &guider_ui.screen_label_temp_cpu, WIDGET_TYPE_LABEL},
    {"label_temp_gpu", &guider_ui.screen_label_temp_gpu, WIDGET_TYPE_LABEL},
    {"label_temp_ram", &guider_ui.screen_label_ram, WIDGET_TYPE_LABEL},

    // System info labels
    {"label_hostname", &guider_ui.screen_label_hostname, WIDGET_TYPE_LABEL},
    {"label_account", &guider_ui.screen_label_account, WIDGET_TYPE_LABEL},
    {"label_version", NULL, WIDGET_TYPE_LABEL}, // Lưu giá trị để join vào label_account

    // System status labels
    {"label_system_status", &guider_ui.screen_label_system_status, WIDGET_TYPE_LABEL},
    {"label_thermal_status", &guider_ui.screen_label_thermal_status, WIDGET_TYPE_LABEL},
    {"label_upgrade_available", &guider_ui.screen_label_upgrade_available, WIDGET_TYPE_LABEL},
    {"label_power_status", &guider_ui.screen_label_power_status, WIDGET_TYPE_LABEL},
    {"label_system_fan_status", &guider_ui.screen_label_system_fan_status, WIDGET_TYPE_LABEL},

    // Network speed labels
    {"label_download_total", &guider_ui.screen_label_download_total, WIDGET_TYPE_LABEL},
    {"label_upload_total", &guider_ui.screen_label_upload_total, WIDGET_TYPE_LABEL},
    {"label_ping_total", &guider_ui.screen_label_ping_total, WIDGET_TYPE_LABEL},

    // Disk I/O labels
    {"label_disk_iops", &guider_ui.screen_label_disk_iops, WIDGET_TYPE_LABEL},
    {"label_disk_read", &guider_ui.screen_label_disk_read, WIDGET_TYPE_LABEL},
    {"label_disk_write", &guider_ui.screen_label_disk_write, WIDGET_TYPE_LABEL},

    // Drive status labels (chỉ dùng để kiểm tra, không update widget)
    {"label_status_drive0", NULL, WIDGET_TYPE_LABEL},
    {"label_status_drive1", NULL, WIDGET_TYPE_LABEL},
    {"label_status_drive2", NULL, WIDGET_TYPE_LABEL},
    {"label_status_drive3", NULL, WIDGET_TYPE_LABEL},
    {"label_status_drive4", NULL, WIDGET_TYPE_LABEL},
    {"label_status_drive5", NULL, WIDGET_TYPE_LABEL},
    {"label_status_nvme1", NULL, WIDGET_TYPE_LABEL},
    {"label_status_nvme2", NULL, WIDGET_TYPE_LABEL},
    {"label_status_nvme3", NULL, WIDGET_TYPE_LABEL},
    {"label_status_nvme4", NULL, WIDGET_TYPE_LABEL},
    {"label_status_nvme5", NULL, WIDGET_TYPE_LABEL},
};

static const size_t s_label_map_count = sizeof(s_label_map) / sizeof(s_label_map[0]);

/**
 * Lấy container tương ứng với label_status_* để đổi màu border.
 * Trả về NULL nếu không có container tương ứng (ví dụ drive0).
 */
static lv_obj_t *get_status_container(const char *label_name) {
  if (label_name == NULL) {
    return NULL;
  }

  if (strcmp(label_name, "label_status_drive1") == 0) {
    return guider_ui.screen_cont_temp_drive1;
  }

  if (strcmp(label_name, "label_status_drive2") == 0) {
    return guider_ui.screen_cont_temp_drive2;
  }

  if (strcmp(label_name, "label_status_drive3") == 0) {
    return guider_ui.screen_cont_temp_drive3;
  }

  if (strcmp(label_name, "label_status_drive4") == 0) {
    return guider_ui.screen_cont_temp_drive4;
  }

  if (strcmp(label_name, "label_status_drive5") == 0) {
    return guider_ui.screen_cont_temp_drive5;
  }

  if (strcmp(label_name, "label_status_nvme1") == 0) {
    return guider_ui.screen_cont_temp_nvme1;
  }

  if (strcmp(label_name, "label_status_nvme2") == 0) {
    return guider_ui.screen_cont_temp_nvme2;
  }

  if (strcmp(label_name, "label_status_nvme3") == 0) {
    return guider_ui.screen_cont_temp_nvme3;
  }

  if (strcmp(label_name, "label_status_nvme4") == 0) {
    return guider_ui.screen_cont_temp_nvme4;
  }

  if (strcmp(label_name, "label_status_nvme5") == 0) {
    return guider_ui.screen_cont_temp_nvme5;
  }

  return NULL;
}

/**
 * Callback khi line state của CDC thay đổi (host mở/đóng port)
 */
static void cdc_line_state_changed_cb(int itf, cdcacm_event_t *event) {
  // Kiểm tra event type
  if (event->type != CDC_EVENT_LINE_STATE_CHANGED) {
    return;
  }

  bool dtr = event->line_state_changed_data.dtr;
  bool rts = event->line_state_changed_data.rts;

  // CDC ready CHỈ khi DTR = 1 (DTR là tín hiệu chính cho biết host đã mở port)
  // RTS có thể vẫn = 1 khi host đóng port, nên chỉ dựa vào DTR
  bool was_ready = s_cdc_ready;
  s_cdc_ready = dtr; // Chỉ dựa vào DTR

  ESP_LOGI("usb_comm", "CDC line state changed: DTR=%d, RTS=%d -> ready=%d (was=%d)", dtr, rts, s_cdc_ready, was_ready);

  if (s_cdc_ready) {
    // CDC ready: host đã mở port
    // Scenario 1: First connection (just became ready) and no data received yet
    if (!was_ready) {
      // Reset flag data received when CDC just became ready (new session)
      s_data_received = false;

      // Set message when first connected
      if (lvgl_port_lock(10)) {
        if (guider_ui.screen_loading_label_loading != NULL &&
            lv_obj_is_valid(guider_ui.screen_loading_label_loading)) {
          lv_label_set_text(guider_ui.screen_loading_label_loading, "Connected. Waiting for data...");
        }
        lvgl_port_unlock();
      }
    } else {
      // Scenario 2: Already connected, but no data received yet and still on loading screen
      if (!s_screen_switched && !s_data_received) {
        if (lvgl_port_lock(10)) {
          if (guider_ui.screen_loading_label_loading != NULL &&
              lv_obj_is_valid(guider_ui.screen_loading_label_loading)) {
            lv_label_set_text(guider_ui.screen_loading_label_loading, "Waiting for data from host...");
          }
          lvgl_port_unlock();
        }
      }
    }

    // QUAN TRỌNG: Nếu CDC vừa mới ready (chuyển từ not ready -> ready)
    // Cần chuyển từ screen_loading sang screen trước khi gửi tín hiệu 'W'
    if (!was_ready && !s_screen_switched && !s_screen_switch_pending) {
      ESP_LOGI("usb_comm", "CDC vừa ready (DTR=1) - Yêu cầu chuyển từ screen_loading sang screen");
      // Signal task để chuyển screen
      if (s_screen_switch_queue != NULL) {
        uint8_t signal = 1; // 1 = chuyển sang screen
        // Kiểm tra xem queue có đang rỗng không (không có signal pending)
        if (uxQueueMessagesWaiting(s_screen_switch_queue) == 0) {
          BaseType_t result = xQueueSend(s_screen_switch_queue, &signal, pdMS_TO_TICKS(100));
          if (result == pdTRUE) {
            s_screen_switch_pending = true; // Đánh dấu đã gửi signal
            ESP_LOGI("usb_comm", "Đã yêu cầu chuyển screen vào queue");
          } else {
            ESP_LOGW("usb_comm", "Không thể yêu cầu chuyển screen vào queue (timeout hoặc queue full)");
          }
        } else {
          ESP_LOGD("usb_comm", "Đã có signal chuyển screen trong queue, bỏ qua");
        }
      } else {
        ESP_LOGW("usb_comm", "s_screen_switch_queue chưa được tạo - có thể usb_comm_start() chưa được gọi");
      }
    }
  } else {
    // CDC not ready: host đã đóng port (DTR=0)
    ESP_LOGI("usb_comm", "CDC not ready: host đã đóng port (DTR=0, RTS=%d)", rts);

    // Scenario 3: Connection was active, data was received, but then disconnected
    bool had_data = s_data_received; // Save state before reset

    // Reset flag data received when CDC disconnected
    s_data_received = false;

    // IMPORTANT: When host closes connection (DTR=0), always switch back to screen_loading
    // if currently on screen. No need to check was_ready as callback may be called multiple times
    if (s_screen_switched) {
      ESP_LOGI("usb_comm", "Host đã đóng kết nối (DTR=0) - Yêu cầu chuyển về screen_loading");
      // Reset flag to indicate screen not switched yet
      s_screen_switched = false;

      // If data was received in this session, set message when disconnected
      if (had_data) {
        if (lvgl_port_lock(10)) {
          if (guider_ui.screen_loading_label_loading != NULL &&
              lv_obj_is_valid(guider_ui.screen_loading_label_loading)) {
            lv_label_set_text(guider_ui.screen_loading_label_loading, "Connection lost. Waiting for reconnect...");
          }
          lvgl_port_unlock();
        }
      }

      // Signal task để chuyển về screen_loading
      if (s_screen_switch_queue != NULL) {
        uint8_t signal = 0; // 0 = chuyển về screen_loading
        BaseType_t result = xQueueSend(s_screen_switch_queue, &signal, 0);
        if (result == pdTRUE) {
          ESP_LOGI("usb_comm", "Đã yêu cầu chuyển về screen_loading vào queue");
        } else {
          ESP_LOGW("usb_comm", "Không thể yêu cầu chuyển về screen_loading vào queue, thử lại...");
          // Retry với timeout ngắn
          result =
              xQueueSend(s_screen_switch_queue, &signal, pdMS_TO_TICKS(10));
          if (result == pdTRUE) {
            ESP_LOGI("usb_comm", "Đã yêu cầu chuyển về screen_loading vào queue (sau retry)");
          }
        }
      }
    }
  }
}

/**
 * Khởi tạo USB CDC-ACM device để giao tiếp với host qua USB.
 * Cài đặt TinyUSB driver và cấu hình CDC-ACM port.
 * Hàm này an toàn khi gọi nhiều lần; driver chỉ được cài đặt một lần.
 */
static void usb_cdc_init(void) {
  // Install TinyUSB driver (chỉ 1 lần)
  if (!s_tinyusb_installed) {
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
      ESP_LOGE("usb_comm", "Lỗi install TinyUSB driver: %s", esp_err_to_name(ret));
      return;
    }
    s_tinyusb_installed = true;
    ESP_LOGI("usb_comm", "TinyUSB driver installed");
  }

  // Cấu hình USB CDC-ACM device
  // LƯU Ý: Cần enable CDC trong menuconfig: Component config → TinyUSB →
  // CDC-ACM
  const tinyusb_config_cdcacm_t acm_cfg = {
      .cdc_port = TINYUSB_CDC_ACM_0,
      .callback_rx = NULL, // Không dùng callback, sẽ polling trong task
      .callback_rx_wanted_char = NULL,
      .callback_line_state_changed = cdc_line_state_changed_cb, // Theo dõi trạng thái
      .callback_line_coding_changed = NULL,
  };

  // Install USB CDC-ACM driver
  esp_err_t ret = tinyusb_cdcacm_init(&acm_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE("usb_comm", "Lỗi init CDC-ACM: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI("usb_comm", "CDC-ACM initialized, đợi host mở port...");
}

/**
 * Hàm helper để set màu cho temperature label dựa trên giá trị nhiệt độ.
 * - Dưới 40°C: màu xanh (#2195f6)
 * - 40-50°C: màu cam (#FFA500)
 * - Trên 50°C: màu đỏ (#FF0000)
 *
 * @param widget Widget label cần set màu
 * @param temp_value Giá trị nhiệt độ (số nguyên)
 */
static void usb_set_temp_label_color(lv_obj_t *widget, int temp_value) {
  lv_color_t color;

  if (temp_value < 40) {
    // Dưới 40: màu mặc định (#2195f6)
    color = lv_color_hex(0x2195f6);
  } else if (temp_value < 50) {
    // 40-50: màu warning (vàng/cam)
    color = lv_color_hex(0xFFA500); // Orange
  } else {
    // Trên 50: màu error (đỏ)
    color = lv_color_hex(0xFF0000); // Red
  }

  lv_obj_set_style_text_color(widget, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * Format status value và set màu text cho status labels.
 * - 1: "Normal" với màu hiện tại (giữ nguyên)
 * - 2: "Failed" với màu error (đỏ #FF0000)
 * - Khác: "Unknown" với màu hiện tại
 *
 * @param widget Widget label cần set text và màu
 * @param status_value Giá trị status (1, 2, hoặc khác)
 */
static void usb_set_status_label_text_and_color(lv_obj_t *widget, int status_value) {
  const char *text;
  lv_color_t color;

  if (status_value == 1) {
    text = "Normal";
    // Giữ màu hiện tại (không đổi)
    color = lv_obj_get_style_text_color(widget, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else if (status_value == 2) {
    text = "Failed";
    // Màu error (đỏ)
    color = lv_color_hex(0xFF0000);
  } else {
    text = "Unknown";
    // Giữ màu hiện tại
    color = lv_obj_get_style_text_color(widget, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  lv_label_set_text(widget, text);
  lv_obj_set_style_text_color(widget, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * Format upgradeAvailable value và set màu text.
 * - 1: "Available" với màu hiện tại
 * - 2: "Unavailable" với màu error
 * - 3: "Connecting" với màu hiện tại
 * - 4: "Disconnected" với màu error
 * - 5: "Others" với màu hiện tại
 * - Khác: "Unknown" với màu hiện tại
 *
 * @param widget Widget label cần set text và màu
 * @param upgrade_value Giá trị upgradeAvailable (1-5 hoặc khác)
 */
static void usb_set_upgrade_label_text_and_color(lv_obj_t *widget, int upgrade_value) {
  const char *text;
  lv_color_t color;

  if (upgrade_value == 1) {
    text = "Available";
    color = lv_obj_get_style_text_color(widget, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else if (upgrade_value == 2) {
    text = "Unavailable";
    color = lv_color_hex(0xFF0000); // Error color
  } else if (upgrade_value == 3) {
    text = "Connecting";
    color = lv_obj_get_style_text_color(widget, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else if (upgrade_value == 4) {
    text = "Disconnected";
    color = lv_color_hex(0xFF0000); // Error color
  } else if (upgrade_value == 5) {
    text = "Others";
    color = lv_obj_get_style_text_color(widget, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else {
    text = "Unknown";
    color =
        lv_obj_get_style_text_color(widget, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  lv_label_set_text(widget, text);
  lv_obj_set_style_text_color(widget, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * Update widget mà không lock (đã lock ở caller)
 * @param widget Widget cần update
 * @param widget_type Loại widget
 * @param value_str String chứa giá trị
 */
static void usb_update_widget_unlocked(lv_obj_t *widget, widget_type_t widget_type, const char *value_str) {
  // Validate inputs
  if (!widget || !value_str) {
    ESP_LOGW("usb_comm", "usb_update_widget_unlocked: widget=%p, value_str=%p", (void *)widget, (void *)value_str);
    return;
  }

  // Kiểm tra widget vẫn hợp lệ
  if (!lv_obj_is_valid(widget)) {
    ESP_LOGW("usb_comm", "usb_update_widget_unlocked: widget không hợp lệ");
    return;
  }

  if (widget_type == WIDGET_TYPE_BAR || widget_type == WIDGET_TYPE_ARC) {
    // Nếu là bar hoặc arc, parse số từ value_str và set giá trị
    int value = 0;
    if (sscanf(value_str, "%d", &value) == 1) {
      // Clamp trong khoảng 0-100
      if (value < 0)
        value = 0;
      if (value > 100)
        value = 100;

      if (widget_type == WIDGET_TYPE_BAR) {
        lv_bar_set_value(widget, (int32_t)value, LV_ANIM_OFF);
      } else { // WIDGET_TYPE_ARC
        lv_arc_set_value(widget, (int32_t)value);
      }
    }
  } else {
    // Nếu là label, copy value_str vào buffer local để tránh lỗi khi buffer gốc bị overwrite
    // LVGL có thể giữ reference đến string, nên cần đảm bảo string vẫn valid
    char text_buf[128]; // Buffer đủ lớn cho hầu hết các label

    // Validate và copy value_str an toàn
    size_t len = 0;
    if (value_str != NULL) {
      // Tìm độ dài string một cách an toàn (giới hạn tối đa)
      for (len = 0; len < sizeof(text_buf) - 1 && value_str[len] != '\0'; len++) {
        // Chỉ copy nếu là printable character
        if (value_str[len] < 0x20 && value_str[len] != '\0' && value_str[len] != '\n' && value_str[len] != '\r' && value_str[len] != '\t') {
          // Invalid character, stop
          break;
        }
      }
    }

    if (len > 0) {
      memcpy(text_buf, value_str, len);
      text_buf[len] = '\0';

      // Kiểm tra lại widget trước khi set text (có thể đã bị xóa)
      if (lv_obj_is_valid(widget)) {
        lv_label_set_text(widget, text_buf);
      } else {
        ESP_LOGW("usb_comm", "Widget không hợp lệ khi set text");
      }
    } else {
      ESP_LOGW("usb_comm", "value_str không hợp lệ hoặc rỗng");
    }
  }
}

/**
 * Task đọc dữ liệu từ USB CDC-ACM và cập nhật UI widgets.
 * Đọc dữ liệu theo dòng, parse format "label_name: value" và cập nhật widget
 * tương ứng. Hỗ trợ label, bar, arc widgets. Tự động set màu cho temperature
 * labels. Khi nhận label_storage_x, cũng tự động cập nhật arc_storage_x tương
 * ứng.
 *
 * @param arg Tham số task (không sử dụng)
 */
static void usb_reader_task(void *arg) {
  uint8_t buf[USB_CDC_BUF_SIZE];
  char line[128]; // Tăng size để chứa label dài hơn
  size_t line_len = 0;

  const size_t num_labels = s_label_map_count;

  while (1) {
    // Đọc data từ USB CDC-ACM
    size_t len = 0;
    esp_err_t ret = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &len);
    if (ret != ESP_OK || len == 0) {
      // Delay lâu hơn để không chiếm CPU, nhường cho LVGL task
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Thêm delay nhỏ sau khi đọc được data để tránh quá tải khi nhận nhiều data liên tiếp
    vTaskDelay(pdMS_TO_TICKS(5));

    for (int i = 0; i < len; i++) {
      char c = (char)buf[i];
      if (c == '\r' || c == '\0') {
        continue;
      }
      if (c == '\n') {
        if (line_len == 0) {
          continue;
        }
        line[line_len] = '\0';
        line_len = 0;

        // Parse format: "label_name: value"
        // Ví dụ: "label_storage_1: 26%" hoặc "bar_cpu_usage: 3"
        char *colon = strchr(line, ':');
        if (!colon) {
          continue; // Không có dấu :, bỏ qua
        }

        // Tách label name và value
        *colon = '\0'; // Kết thúc label name tại đây
        char *label_name = line;
        char *value_str = colon + 1;

        // Bỏ khoảng trắng đầu value
        while (*value_str == ' ' || *value_str == '\t') {
          value_str++;
        }

        // Validate value_str vẫn nằm trong buffer line
        if (value_str < line || value_str >= line + sizeof(line)) {
          ESP_LOGW("usb_comm", "value_str nằm ngoài buffer, bỏ qua label %s", label_name);
          continue;
        }

        // Tìm label trong mapping table
        for (size_t j = 0; j < num_labels; j++) {
          if (strcmp(label_name, s_label_map[j].label_name) == 0) {

            // Đánh dấu đã nhận được dữ liệu từ host
            if (!s_data_received) {
              s_data_received = true;
              ESP_LOGI("usb_comm", "Đã nhận được dữ liệu đầu tiên từ host");

              // QUAN TRỌNG: Nếu chưa chuyển screen, trigger chuyển screen ngay
              // Điều này đảm bảo screen được chuyển ngay cả khi signal từ CDC callback bị mất
              if (!s_screen_switched && !s_screen_switch_pending && s_screen_switch_queue != NULL) {
                ESP_LOGI("usb_comm", "Nhận được dữ liệu - Yêu cầu chuyển từ screen_loading sang screen");
                uint8_t signal = 1; // 1 = chuyển sang screen

                // Kiểm tra xem queue có đang rỗng không
                if (uxQueueMessagesWaiting(s_screen_switch_queue) == 0) {
                  BaseType_t result = xQueueSend(s_screen_switch_queue, &signal, pdMS_TO_TICKS(100));
                  if (result == pdTRUE) {
                    s_screen_switch_pending = true; // Đánh dấu đã gửi signal
                    ESP_LOGI("usb_comm", "Đã yêu cầu chuyển screen vào queue (từ data received)");
                  } else {
                    ESP_LOGW("usb_comm", "Không thể yêu cầu chuyển screen vào queue (queue full?)");
                  }
                } else {
                  ESP_LOGD("usb_comm", "Đã có signal chuyển screen trong queue, bỏ qua");
                }
              }
            }

            // Tìm thấy label, lấy widget pointer mới nhất từ guider_ui
            // (dereference pointer to pointer để luôn có giá trị mới nhất sau khi screen được khởi tạo)
            lv_obj_t *widget = NULL;
            if (s_label_map[j].widget_ptr != NULL) {
              widget = *(s_label_map[j].widget_ptr);
            }

            // Kiểm tra widget và value_str có hợp lệ không
            if (widget != NULL && value_str != NULL && lv_obj_is_valid(widget)) {

              // Widget tồn tại, update giá trị
              // Lock LVGL một lần cho tất cả các update trong label này
              bool locked = lvgl_port_lock(20); // Tăng timeout lên 20ms

              if (locked) {
                // Kiểm tra lại widget sau khi lock (có thể đã bị xóa)
                if (!lv_obj_is_valid(widget)) {
                  ESP_LOGW("usb_comm", "Widget %s không còn hợp lệ sau khi lock LVGL", label_name);
                  lvgl_port_unlock();
                  break;
                }

                // Đặc biệt: Nếu là temperature label, set màu dựa trên giá trị
                if (strncmp(label_name, "label_temp_", 11) == 0) {
                  // Parse giá trị nhiệt độ từ value_str (format: "36°C" hoặc "N/A")
                  int temp_value = -1;
                  if (sscanf(value_str, "%d", &temp_value) == 1) {
                    usb_set_temp_label_color(widget, temp_value);
                  }
                  // Nếu không parse được (ví dụ "N/A"), giữ màu mặc định

                  // Luôn cập nhật text hiển thị giá trị nhiệt độ (kể cả "N/A")
                  usb_update_widget_unlocked(widget, s_label_map[j].widget_type, value_str);
                }
                // Đặc biệt: Xử lý status labels - format text và đổi màu
                else if (strcmp(label_name, "label_system_status") == 0 ||
                         strcmp(label_name, "label_thermal_status") == 0 ||
                         strcmp(label_name, "label_power_status") == 0 ||
                         strcmp(label_name, "label_system_fan_status") == 0) {

                  // Parse giá trị status (1=Normal, 2=Failed)
                  int status_value = 0;
                  if (sscanf(value_str, "%d", &status_value) == 1) {
                    usb_set_status_label_text_and_color(widget, status_value);
                  } else {
                    // Nếu không parse được (ví dụ "N/A"), hiển thị trực tiếp text
                    usb_update_widget_unlocked(widget, WIDGET_TYPE_LABEL, value_str);
                  }
                }
                // Đặc biệt: Xử lý upgradeAvailable label - format text và đổi màu
                else if (strcmp(label_name, "label_upgrade_available") == 0) {

                  // Parse giá trị upgradeAvailable (1-5)
                  int upgrade_value = 0;

                  if (sscanf(value_str, "%d", &upgrade_value) == 1) {
                    usb_set_upgrade_label_text_and_color(widget, upgrade_value);
                  } else {
                    // Nếu không parse được, hiển thị trực tiếp text
                    usb_update_widget_unlocked(widget, WIDGET_TYPE_LABEL, value_str);
                  }
                }
                // Update widget với text/value (cho các label thông thường)
                else {
                  usb_update_widget_unlocked(widget, s_label_map[j].widget_type, value_str);
                }

                lvgl_port_unlock();
              } else {
                // Không lock được, bỏ qua lần này (LVGL đang render)
                ESP_LOGD("usb_comm", "Không thể lock LVGL, bỏ qua update %s", label_name);
              }

              // Break sau khi tìm thấy và xử lý label
              break;
            } else {
              // Widget không tồn tại hoặc không hợp lệ
              // Nếu là label_status_drive0 (không có widget), vẫn cần xử lý container nếu có
              // Nhưng nếu không phải label_status_*, break ngay
              if (strncmp(label_name, "label_status_", 13) != 0) {
                // Không phải label_status_*, break ngay
                ESP_LOGD("usb_comm", "Widget cho label %s không tồn tại, bỏ qua", label_name);
                break;
              }
              // Nếu là label_status_* nhưng widget = NULL, vẫn tiếp tục để xử lý container (nếu có)
            }

            // Đặc biệt: Xử lý drive status labels - set border color cho containers
            // CHỈ xử lý nếu label không phải là drive0 (không có container)
            if (strncmp(label_name, "label_status_", 13) == 0) {

              // Nếu là drive0, bỏ qua hoàn toàn (không có container)
              if (strcmp(label_name, "label_status_drive0") == 0) {
                // Bỏ qua label_status_drive0, break ngay
                break;
              }

              // Parse giá trị status (INTEGER: 1 = OK, khác 1 = error)
              // Nếu value_str là "N/A", sscanf sẽ fail và bỏ qua
              int status_value = 0;
              if (sscanf(value_str, "%d", &status_value) == 1) {
                lv_obj_t *container = get_status_container(label_name);

                // Set border color dựa trên status value
                if (container != NULL && lv_obj_is_valid(container)) {
                  if (lvgl_port_lock(10)) {
                    // Kiểm tra lại container sau khi lock
                    if (lv_obj_is_valid(container)) {
                      if (status_value == 1) {
                        // Status OK: màu xanh mặc định
                        lv_obj_set_style_border_color(
                            container, lv_color_hex(0x2195f6),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
                      } else {
                        // Status error: màu đỏ
                        lv_obj_set_style_border_color(
                            container, lv_color_hex(0xFF0000),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
                      }
                    }
                    lvgl_port_unlock();
                  }
                } else {
                  ESP_LOGW("usb_comm", "Không tìm thấy container cho %s hoặc container invalid", label_name);
                }
              }
              // Break sau khi xử lý label_status_* (dù có update container hay không)
              break;
            }

            // Thêm delay nhỏ sau mỗi lần xử lý label để tránh quá tải
            vTaskDelay(pdMS_TO_TICKS(1));

            // Đặc biệt: Khi nhận label_storage_x, cũng cần update arc_storage_x
            // tương ứng Ví dụ: label_storage_1: 26% -> update cả label và
            // arc_storage_1 với giá trị 26
            if (strncmp(label_name, "label_storage_", 14) == 0 &&
                strlen(label_name) > 14 && label_name[14] >= '1' &&
                label_name[14] <= '4' && label_name[15] == '\0') {

              // Tìm arc widget tương ứng
              char arc_name[32];
              snprintf(arc_name, sizeof(arc_name), "arc_storage_%c", label_name[14]);

              // Tìm arc trong mapping
              for (size_t k = 0; k < num_labels; k++) {
                if (strcmp(arc_name, s_label_map[k].label_name) == 0) {
                  // Lấy widget pointer mới nhất từ guider_ui
                  lv_obj_t *arc_widget = NULL;
                  if (s_label_map[k].widget_ptr != NULL) {
                    arc_widget = *(s_label_map[k].widget_ptr);
                  }

                  if (arc_widget != NULL && lv_obj_is_valid(arc_widget)) {
                    // Parse giá trị từ value_str (có thể có ký tự %)
                    // Ví dụ: "26%" -> 26
                    int arc_value = 0;
                    if (sscanf(value_str, "%d", &arc_value) == 1) {
                      // Clamp trong khoảng 0-100
                      if (arc_value < 0)
                        arc_value = 0;
                      if (arc_value > 100)
                        arc_value = 100;

                      // Update arc với giá trị số
                      if (lvgl_port_lock(10)) {
                        lv_arc_set_value(arc_widget, (int32_t)arc_value);
                        lvgl_port_unlock();
                      }
                    }
                  }
                  break;
                }
              }
            }

            // Đặc biệt: Xử lý label_version - lưu giá trị để join vào
            // label_account
            if (strcmp(label_name, "label_version") == 0) {

              // Lưu version vào static variable
              strncpy(s_label_version, value_str, sizeof(s_label_version) - 1);
              s_label_version[sizeof(s_label_version) - 1] = '\0';

              // Nếu đã có label_account, update lại với version
              if (lvgl_port_lock(10)) {
                // Đọc giá trị hiện tại của label_account
                const char *current_account = lv_label_get_text(guider_ui.screen_label_account);
                if (current_account && strlen(current_account) > 0) {
                  // Join account và version
                  char joined_text[128];
                  snprintf(joined_text, sizeof(joined_text), "%s - %s", current_account, s_label_version);
                  lv_label_set_text(guider_ui.screen_label_account, joined_text);
                }
                lvgl_port_unlock();
              }
            }

            // Đặc biệt: Xử lý label_account - join với version nếu có
            if (strcmp(label_name, "label_account") == 0) {
              // Nếu đã có version, join vào account
              if (strlen(s_label_version) > 0) {
                char joined_text[128];
                snprintf(joined_text, sizeof(joined_text), "%s - %s", value_str, s_label_version);
                if (lvgl_port_lock(10)) {
                  lv_label_set_text(guider_ui.screen_label_account, joined_text);
                  lvgl_port_unlock();
                }
                // Không update widget ở phần chung nữa vì đã update ở đây
                break;
              }
            }

            break; // Đã tìm thấy, không cần tìm tiếp
          }
        }
      } else {
        if (line_len < sizeof(line) - 1) {
          line[line_len++] = c;
        } else {
          // Quá dài, reset dòng
          line_len = 0;
        }
      }
    }
  }
}

/*
Khởi động task nhận dữ liệu từ host qua USB CDC device (cổng USB thứ 2).
ESP32-P4 hoạt động như USB CDC device qua esp_tinyusb, tự động tạo COM port mới
trên PC. Cổng USB thứ 1 (COM3) vẫn dùng cho flash/debug qua USB Serial/JTAG.
Không cần phần cứng thêm (USB-UART bridge), chỉ cần cắm USB-C thứ 2 vào PC.
Giao thức chuỗi đơn giản, ví dụ:
  "CPU:2.2,10\n"
Hàm này an toàn khi gọi nhiều lần; task chỉ được tạo một lần.
*/
// Task để chuyển từ screen_loading sang screen khi USB kết nối thành công
static void usb_screen_switch_task(void *arg) {
  uint8_t signal;
  int retry_count = 0;
  const int MAX_RETRIES = 50; // Tối đa 5 giây (50 * 100ms)

  while (1) {
    // Chờ tín hiệu từ queue để chuyển screen
    if (xQueueReceive(s_screen_switch_queue, &signal, portMAX_DELAY) == pdTRUE) {
      retry_count = 0; // Reset retry count khi nhận signal mới

      if (signal == 0) { // 0 = chuyển về screen_loading
        ESP_LOGI("usb_comm", "Task screen switch: Bắt đầu chuyển về screen_loading");

        // Kiểm tra screen widgets đã được khởi tạo chưa với timeout
        while (guider_ui.screen_loading == NULL && retry_count < MAX_RETRIES) {
          ESP_LOGW("usb_comm", "Task screen switch: screen_loading chưa được khởi tạo, đợi... (retry %d/%d)", retry_count + 1, MAX_RETRIES);
          vTaskDelay(pdMS_TO_TICKS(100));
          retry_count++;
        }

        if (guider_ui.screen_loading == NULL) {
          ESP_LOGE("usb_comm", "Task screen switch: screen_loading vẫn chưa được khởi tạo sau %d retries, bỏ qua", MAX_RETRIES);
          continue; // Bỏ qua thay vì retry vô tận
        }

        // Chuyển về screen_loading với LVGL lock
        if (lvgl_port_lock(100)) { // Timeout 100ms
          // Chuyển về screen_loading
          lv_screen_load(guider_ui.screen_loading);
          lvgl_port_unlock();

          ESP_LOGI("usb_comm", "Task screen switch: Đã chuyển về screen_loading thành công");

          // Đánh dấu screen chưa chuyển (đã về screen_loading) và reset pending flag
          s_screen_switched = false;
          s_screen_switch_pending = false;
        } else {
          ESP_LOGW("usb_comm", "Task screen switch: Không thể lock LVGL, sẽ thử lại sau");

          // Gửi lại signal vào queue để thử lại
          vTaskDelay(pdMS_TO_TICKS(100));
          xQueueSend(s_screen_switch_queue, &signal, 0);
        }
      } else if (signal == 1) { // 1 = chuyển sang screen
        ESP_LOGI("usb_comm", "Task screen switch: Bắt đầu chuyển từ screen_loading sang screen");

        // Kiểm tra screen_loading đã được khởi tạo chưa
        retry_count = 0;
        while (guider_ui.screen_loading == NULL && retry_count < MAX_RETRIES) {
          ESP_LOGW("usb_comm", "Task screen switch: screen_loading chưa được khởi tạo, đợi... (retry %d/%d)", retry_count + 1, MAX_RETRIES);
          vTaskDelay(pdMS_TO_TICKS(100));
          retry_count++;
        }

        if (guider_ui.screen_loading == NULL) {
          ESP_LOGE("usb_comm", "Task screen switch: screen_loading vẫn chưa được khởi tạo sau %d retries, bỏ qua", MAX_RETRIES);
          s_screen_switch_pending = false;
          // Xóa tất cả signal còn lại trong queue để tránh loop
          uint8_t dummy;
          while (xQueueReceive(s_screen_switch_queue, &dummy, 0) == pdTRUE) {
            ESP_LOGD("usb_comm", "Đã xóa signal cũ khỏi queue");
          }
          continue;
        }

        // Nếu screen chưa được khởi tạo, khởi tạo nó ngay
        // Note: setup_scr_screen được declare trong gui_guider.h (đã include)
        if (guider_ui.screen == NULL) {
          ESP_LOGI("usb_comm", "Task screen switch: screen chưa được khởi tạo, đang khởi tạo...");
          if (lvgl_port_lock(500)) { // Timeout 500ms để khởi tạo screen
            setup_scr_screen(&guider_ui);
            lvgl_port_unlock();
            ESP_LOGI("usb_comm", "Task screen switch: Đã khởi tạo screen thành công");
          } else {
            ESP_LOGE("usb_comm", "Task screen switch: Không thể lock LVGL để khởi tạo screen");
            s_screen_switch_pending = false;
            continue;
          }
        }

        // Chuyển screen với LVGL lock
        if (lvgl_port_lock(100)) { // Timeout 100ms
          // Screen đã được setup trong setup_ui() rồi, chỉ cần load
          // Chuyển từ screen_loading sang screen
          lv_screen_load(guider_ui.screen);
          lvgl_port_unlock();

          ESP_LOGI("usb_comm", "Task screen switch: Đã chuyển sang screen thành công");

          // Đánh dấu đã chuyển screen và reset pending flag
          s_screen_switched = true;
          s_screen_switch_pending = false;

          // Đợi một chút để đảm bảo screen đã được render xong
          vTaskDelay(pdMS_TO_TICKS(200));

          // QUAN TRỌNG: Sau khi chuyển screen xong và CDC ready, luôn bật backlight
          // và gửi tín hiệu 'W' để host biết bắt đầu gửi dữ liệu
          if (s_cdc_ready) {
            // Trigger activity để reset screen timeout (nếu màn hình đã tắt)
            lv_display_t *disp = lv_display_get_default();
            if (disp != NULL) {
              lv_display_trigger_activity(disp);
            }

            ESP_LOGI("usb_comm", "Task screen switch: Screen đã chuyển, CDC ready - Bật backlight và gửi tín hiệu 'W' để bắt đầu nhận dữ liệu");
            if (s_backlight_signal_queue != NULL) {
              uint8_t wake_signal = 'W';
              BaseType_t result = xQueueSend(s_backlight_signal_queue, &wake_signal, pdMS_TO_TICKS(100));
              if (result == pdTRUE) {
                ESP_LOGI("usb_comm", "Đã gửi tín hiệu 'W' vào queue");
              } else {
                ESP_LOGW("usb_comm", "Không thể gửi tín hiệu 'W' vào queue");
              }
            }
          } else {
            ESP_LOGW("usb_comm", "Task screen switch: CDC chưa ready, không gửi 'W'");
          }
        } else {
          ESP_LOGW("usb_comm", "Task screen switch: Không thể lock LVGL, sẽ thử lại sau");
          // Gửi lại signal vào queue để thử lại
          vTaskDelay(pdMS_TO_TICKS(100));
          xQueueSend(s_screen_switch_queue, &signal, 0);
        }
      }
    }
  }
}

// Task để gửi tín hiệu backlight state (chạy riêng để không block)
static void usb_sender_task(void *arg) {
  uint8_t signal;

  while (1) {
    // Chờ tín hiệu từ queue
    if (xQueueReceive(s_backlight_signal_queue, &signal, portMAX_DELAY) == pdTRUE) {
      const char *signal_name = (signal == 'W') ? "W (wake)" : "S (sleep)";

      ESP_LOGI("usb_comm", "Task sender: Gửi tín hiệu backlight: %s (0x%02x)", signal_name, signal);

      // Kiểm tra CDC ready trước khi gửi
      if (!s_cdc_ready) {
        ESP_LOGW("usb_comm", "Task sender: CDC chưa ready, bỏ qua tín hiệu %s", signal_name);
        continue;
      }

      // Kiểm tra CDC connected (theo TinyUSB API)
      if (!tud_cdc_connected()) {
        ESP_LOGW("usb_comm", "Task sender: CDC chưa connected, bỏ qua tín hiệu %s", signal_name);
        continue;
      }

      // QUAN TRỌNG: Clear write buffer trước khi gửi (theo document)
      tud_cdc_write_clear();

      // Đợi một chút sau khi clear để CDC sẵn sàng hoàn toàn
      vTaskDelay(pdMS_TO_TICKS(20));

      // Kiểm tra buffer available trước khi gửi
      uint32_t available = tud_cdc_write_available();
      if (available == 0) {
        ESP_LOGW("usb_comm", "Task sender: CDC buffer không available, bỏ qua tín hiệu %s", signal_name);
        continue;
      }

      // Gửi tín hiệu trực tiếp qua TinyUSB API (thay vì wrapper)
      uint32_t written = tud_cdc_write(&signal, 1);
      if (written == 1) {
        // Flush để đảm bảo dữ liệu được gửi ngay
        tud_cdc_write_flush();
        ESP_LOGI("usb_comm", "Task sender: Đã gửi tín hiệu %s thành công", signal_name);
      } else {
        ESP_LOGE("usb_comm", "Task sender: LỖI gửi tín hiệu %s - chỉ gửi được %lu/1 bytes", signal_name, (unsigned long)written);
      }
    }
  }
}

void usb_comm_start(void) {
  if (s_usb_comm_started) {
    return;
  }
  s_usb_comm_started = true;

  usb_cdc_init();

  // Tạo queue cho tín hiệu backlight
  s_backlight_signal_queue = xQueueCreate(10, sizeof(uint8_t));
  if (s_backlight_signal_queue == NULL) {
    ESP_LOGE("usb_comm", "Không thể tạo queue cho backlight signal");
    return;
  }

  // Tạo queue cho screen switch signal
  s_screen_switch_queue = xQueueCreate(5, sizeof(uint8_t));
  if (s_screen_switch_queue == NULL) {
    ESP_LOGE("usb_comm", "Không thể tạo queue cho screen switch signal");
    return;
  }

  // Priority thấp hơn LVGL task (4) để không block render
  xTaskCreate(usb_reader_task, "usb_comm_cdc", 4096, NULL, 3, NULL);

  // Task screen switch với priority giống reader
  xTaskCreate(usb_screen_switch_task, "usb_comm_screen", 2048, NULL, 3, NULL);

  // Task sender với priority thấp hơn reader một chút
  xTaskCreate(usb_sender_task, "usb_comm_sender", 2048, NULL, 2, NULL);
}

/**
 * Gửi tín hiệu backlight state qua USB CDC để host biết khi nào cần gửi dữ liệu
 * sensor. Gửi 'W' (0x57) khi backlight bật, 'S' (0x53) khi backlight tắt.
 *
 * @param is_on true nếu backlight đang bật, false nếu đang tắt
 */
void usb_comm_send_backlight_state(bool is_on) {
  if (!s_usb_comm_started) {
    ESP_LOGW("usb_comm", "usb_comm chưa khởi động, không thể gửi tín hiệu backlight");
    return; // Chưa khởi động, không gửi
  }

  if (s_backlight_signal_queue == NULL) {
    ESP_LOGE("usb_comm", "Queue chưa được tạo, không thể gửi tín hiệu backlight");
    return;
  }

  // QUAN TRỌNG: Chỉ gửi tín hiệu 'W' sau khi screen đã chuyển từ screen_loading
  // sang screen. Nếu screen chưa chuyển, bỏ qua yêu cầu hiện tại
  if (is_on && !s_screen_switched) {
    ESP_LOGI("usb_comm", "Backlight bật nhưng screen chưa chuyển - Chờ screen chuyển trước khi gửi 'W'");
    return; // Chưa gửi, sẽ gửi sau khi screen chuyển
  }

  uint8_t signal = is_on ? 'W' : 'S';
  const char *signal_name = is_on ? "W (wake)" : "S (sleep)";

  ESP_LOGI("usb_comm", "Yêu cầu gửi tín hiệu backlight: %s (0x%02x)", signal_name, signal);

  // Gửi tín hiệu vào queue (non-blocking)
  // Task sender sẽ lấy và gửi qua USB CDC
  BaseType_t result = xQueueSend(s_backlight_signal_queue, &signal, 0);

  if (result == pdTRUE) {
    ESP_LOGI("usb_comm", "Đã đưa tín hiệu %s vào queue, task sender sẽ gửi", signal_name);
  } else {
    ESP_LOGW("usb_comm", "Queue đầy, không thể đưa tín hiệu %s vào queue", signal_name);
    // Thử lại với timeout ngắn
    result = xQueueSend(s_backlight_signal_queue, &signal, pdMS_TO_TICKS(10));
    if (result == pdTRUE) {
      ESP_LOGI("usb_comm", "Đã đưa tín hiệu %s vào queue (sau retry)", signal_name);
    } else {
      ESP_LOGE("usb_comm", "LỖI: Không thể đưa tín hiệu %s vào queue", signal_name);
    }
  }
}