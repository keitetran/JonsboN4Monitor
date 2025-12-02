/*
 * Copyright 2024 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be
 * used strictly in accordance with the applicable license terms. By expressly
 * accepting such terms or by downloading, installing, activating and/or
 * otherwise using the software, you are agreeing that you have read, and that
 * you agree to comply with and are bound by, such license terms.  If you do not
 * agree to be bound by the applicable license terms, then you may not retain,
 * install, activate or otherwise use the software.
 */

/*********************
 *      INCLUDES
 *********************/
#include "custom.h"
#include "gui_guider.h"
#include "usb_comm.h"
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lvgl_port_v9.h"
#include "nvs_flash.h"

#ifdef CONFIG_MQTT_ENABLE
#include "mqtt_comm.h"
#endif

#ifdef CONFIG_OTA_ENABLE
#include "ota_update.h"
#endif

#ifdef CONFIG_WIFI_ENABLE
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include <time.h>
#include <stdlib.h>
#endif

/*********************
 *      DEFINES
 *********************/
#if defined(ESP_PLATFORM)
#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID "wifi_ssid"
#endif
#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD "wifi_pasword"
#endif
#ifndef CONFIG_WIFI_MAXIMUM_RETRY
#define CONFIG_WIFI_MAXIMUM_RETRY 5
#endif
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
// static void remove_tab_padding(lv_obj_t *tabview);
#ifdef CONFIG_WIFI_ENABLE
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void wifi_init_sta(void);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/

/**
 * Create a demo application
 */

#define TAG "custom_app"

#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN                                          \
  (3) // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

#define BSP_LCD_H_RES (480)
#define BSP_LCD_V_RES (800)

#define BSP_I2C_NUM (I2C_NUM_1)
#define BSP_I2C_SDA (GPIO_NUM_7)
#define BSP_I2C_SCL (GPIO_NUM_8)

#define BSP_LCD_TOUCH_RST (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT (GPIO_NUM_NC)

#define BSP_LCD_BACKLIGHT GPIO_NUM_23
#define LCD_LEDC_CH LEDC_CHANNEL_0

lv_ui guider_ui;
static i2c_master_bus_handle_t i2c_handle = NULL;

// Screen timeout variables
static esp_timer_handle_t screen_timeout_timer = NULL;
static bool screen_is_off = false;
#define SCREEN_TIMEOUT_MS (30000) // 30 seconds

#ifdef CONFIG_WIFI_ENABLE
// WiFi variables
static int s_retry_num = 0;

// Extern guider_ui để access screen widgets
extern lv_ui guider_ui;

// Extern digital_clock global variables
extern int screen_digital_clock_1_hour_value;
extern int screen_digital_clock_1_min_value;
extern int screen_digital_clock_1_sec_value;
extern char screen_digital_clock_1_meridiem[];
#endif

static const st7701_lcd_init_cmd_t lcd_cmd[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x10, 0x08}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},

    {0xB0,
     (uint8_t[]){0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09, 0x09, 0x28,
                 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71},
     16, 0},
    {0xB1,
     (uint8_t[]){0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08, 0x08, 0x25,
                 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D},
     16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},

    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x58}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t[]){0x03}, 1, 0},
    {0xBC, (uint8_t[]){0x00}, 1, 0},

    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},

    {0xE0, (uint8_t[]){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1,
     (uint8_t[]){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40,
                 0x40},
     11, 0},
    {0xE2,
     (uint8_t[]){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0,
                 0x00, 0xA0, 0x00},
     13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5,
     (uint8_t[]){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A,
                 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0},
     16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8,
     (uint8_t[]){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29,
                 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0},
     16, 0},

    {0xEB, (uint8_t[]){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t[]){0x08, 0x01}, 2, 0},

    {0xED,
     (uint8_t[]){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF,
                 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B},
     16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},

    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

IRAM_ATTR static bool
mipi_dsi_lcd_on_vsync_event(esp_lcd_panel_handle_t panel,
                            esp_lcd_dpi_panel_event_data_t *edata,
                            void *user_ctx) {
  return lvgl_port_notify_lcd_vsync();
}

static esp_err_t bsp_display_brightness_init(void) {
  const ledc_channel_config_t LCD_backlight_channel = {
      .gpio_num = BSP_LCD_BACKLIGHT,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LCD_LEDC_CH,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = 1,
      .duty = 0,
      .hpoint = 0};
  const ledc_timer_config_t LCD_backlight_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = 1,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK};

  ESP_ERROR_CHECK(ledc_timer_config(&LCD_backlight_timer));
  ESP_ERROR_CHECK(ledc_channel_config(&LCD_backlight_channel));
  return ESP_OK;
}

static esp_err_t bsp_display_brightness_set(int brightness_percent) {
  if (brightness_percent > 100) {
    brightness_percent = 100;
  }
  if (brightness_percent < 0) {
    brightness_percent = 0;
  }

  ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
  uint32_t duty_cycle = (1023 * brightness_percent) / 100;
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
  return ESP_OK;
}

static inline void bsp_display_backlight_off(void) {
  bsp_display_brightness_set(0);
  // Gửi tín hiệu 'S' (sleep) qua USB để host biết dừng gửi dữ liệu
  usb_comm_send_backlight_state(false);
}

static inline void bsp_display_backlight_on(void) {
  bsp_display_brightness_set(100);
  // Gửi tín hiệu 'W' (wake) qua USB để host biết bắt đầu gửi dữ liệu
  usb_comm_send_backlight_state(true);
}

// Helper function to wake up screen
static void wake_up_screen(void) {
  if (screen_is_off) {
    ESP_LOGI(TAG, "Activity detected: turning on backlight");
    bsp_display_backlight_on();
    screen_is_off = false;
    // Trigger activity to reset timeout
    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
      lv_display_trigger_activity(disp);
    }
  }
}

// Event callback để bật màn hình ngay khi có touch
static void screen_touch_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    wake_up_screen();
  }
}

// Screen timeout timer callback
static void screen_timeout_timer_cb(void *arg) {
  lv_display_t *disp = lv_display_get_default();
  if (disp == NULL) {
    return;
  }

  uint32_t inactive_time = lv_display_get_inactive_time(disp);

  if (!screen_is_off && inactive_time >= SCREEN_TIMEOUT_MS) {
    // Tắt màn hình sau 30 giây không hoạt động
    ESP_LOGI(TAG, "Screen timeout: turning off backlight");
    bsp_display_backlight_off();
    screen_is_off = true;
  } else if (screen_is_off && inactive_time < SCREEN_TIMEOUT_MS) {
    // Bật lại màn hình khi có hoạt động mới
    wake_up_screen();
  }
}

#ifdef CONFIG_WIFI_ENABLE
static void update_datetime_from_esp32(void) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  // Update date for datetext_1 (format: YYYY/MM/DD)
  char date_str[32];
  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;
  
  // Clamp values to valid range to avoid format-truncation warning
  if (year < 2000) year = 2000;
  if (year > 2099) year = 2099;
  if (month < 1) month = 1;
  if (month > 12) month = 12;
  if (day < 1) day = 1;
  if (day > 31) day = 31;
  
  // Use explicit size limit in snprintf to satisfy compiler
  int ret = snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d", year, month, day);
  if (ret < 0 || ret >= (int)sizeof(date_str)) {
    ESP_LOGW(TAG, "Date string truncated");
  }
  
  if (lvgl_port_lock(10)) {
    if (guider_ui.screen_datetext_1 != NULL && 
        lv_obj_is_valid(guider_ui.screen_datetext_1)) {
      lv_label_set_text(guider_ui.screen_datetext_1, date_str);
    }
    lvgl_port_unlock();
  }
  
  // Update time for digital_clock_1 (12-hour format with AM/PM)
  int hour_12 = timeinfo.tm_hour % 12;
  if (hour_12 == 0) {
    hour_12 = 12;
  }
  screen_digital_clock_1_hour_value = hour_12;
  screen_digital_clock_1_min_value = timeinfo.tm_min;
  screen_digital_clock_1_sec_value = timeinfo.tm_sec;
  
  if (timeinfo.tm_hour < 12) {
    strcpy(screen_digital_clock_1_meridiem, "AM");
  } else {
    strcpy(screen_digital_clock_1_meridiem, "PM");
  }
  
  // Update digital clock label immediately
  if (lvgl_port_lock(10)) {
    if (guider_ui.screen_digital_clock_1 != NULL && 
        lv_obj_is_valid(guider_ui.screen_digital_clock_1)) {
      lv_label_set_text_fmt(guider_ui.screen_digital_clock_1, 
                           "%d:%02d:%02d %s", 
                           screen_digital_clock_1_hour_value,
                           screen_digital_clock_1_min_value,
                           screen_digital_clock_1_sec_value,
                           screen_digital_clock_1_meridiem);
    }
    lvgl_port_unlock();
  }
  
  ESP_LOGI(TAG, "Updated date/time: %s %d:%02d:%02d %s", 
           date_str, screen_digital_clock_1_hour_value,
           screen_digital_clock_1_min_value, screen_digital_clock_1_sec_value,
           screen_digital_clock_1_meridiem);
}

static void time_sync_notification_cb(struct timeval *tv) {
  ESP_LOGI(TAG, "Time synchronized: %s", ctime(&tv->tv_sec));
  
  // Update date and time widgets after sync
  update_datetime_from_esp32();
}

static void init_sntp(void) {
  ESP_LOGI(TAG, "Initializing SNTP for time sync...");
  
  // Set timezone to Japan (JST = UTC+9)
  setenv("TZ", "JST-9", 1);
  tzset();
  ESP_LOGI(TAG, "Timezone set to JST (Japan Standard Time, UTC+9)");
  
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "time.google.com");
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();
  ESP_LOGI(TAG, "SNTP initialized, waiting for time sync...");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      ESP_LOGE(TAG, "connect to the AP fail");
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    
    // Sync time from NTP server after WiFi got IP
    init_sntp();
    
    // Start MQTT client after WiFi got IP (if enabled)
#ifdef CONFIG_MQTT_ENABLE
    ESP_LOGI(TAG, "WiFi connected - Starting MQTT client...");
    mqtt_comm_start();
#endif

    // Initialize OTA update module after WiFi got IP (if enabled)
#ifdef CONFIG_OTA_ENABLE
    ESP_LOGI(TAG, "WiFi connected - Initializing OTA update...");
    esp_err_t ota_ret = ota_update_init();
    if (ota_ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update module initialized");
        
        // Auto-check for updates if enabled
#ifdef CONFIG_OTA_AUTO_CHECK
        ESP_LOGI(TAG, "Auto-checking for OTA updates...");
        ota_check_for_updates(NULL);
#endif
    } else {
        ESP_LOGE(TAG, "Failed to initialize OTA update: %s", esp_err_to_name(ota_ret));
    }
#endif
  }
}

static void wifi_init_sta(void) {
  ESP_LOGI(TAG, "Initializing WiFi...");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };
  // Copy SSID and password from Kconfig
  strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID,
          sizeof(wifi_config.sta.ssid) - 1);
  wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
  strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi initialization finished. SSID: %s", CONFIG_WIFI_SSID);
}
#endif // CONFIG_WIFI_ENABLE

static void boot_guider_ui(void) {
  if (lvgl_port_lock(-1)) {
    setup_ui(&guider_ui);
    custom_init(&guider_ui);
    lvgl_port_unlock();
  }
}

void app_main(void) {
  // Initialize NVS (needed for other features)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize WiFi (via esp_hosted for ESP32-P4) - only if enabled in Kconfig
#ifdef CONFIG_WIFI_ENABLE
  ESP_LOGI(TAG, "Initializing WiFi Remote...");
  wifi_init_sta();
#else
  ESP_LOGI(TAG, "WiFi is disabled in configuration");
#endif

  // MQTT will be started automatically after WiFi connects (in wifi_event_handler)

  bsp_display_brightness_init();

  i2c_master_bus_config_t i2c_bus_conf = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .sda_io_num = BSP_I2C_SDA,
      .scl_io_num = BSP_I2C_SCL,
      .i2c_port = BSP_I2C_NUM,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle));

  static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
      .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
  };
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan));
  ESP_LOGI(TAG, "MIPI DSI PHY Powered on");

  esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
  esp_lcd_dsi_bus_config_t bus_config = ST7701_PANEL_BUS_DSI_2CH_CONFIG();
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

  ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_dbi_io_config_t dbi_config = ST7701_PANEL_IO_DBI_CONFIG();
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io));

  esp_lcd_panel_handle_t disp_panel = NULL;
  ESP_LOGI(TAG, "Install LCD driver of st7701");
  esp_lcd_dpi_panel_config_t dpi_config = {
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 34,
      .virtual_channel = 0,
      .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
      .num_fbs = LVGL_PORT_LCD_BUFFER_NUMS,
      .video_timing =
          {
              .h_size = BSP_LCD_H_RES,
              .v_size = BSP_LCD_V_RES,
              .hsync_back_porch = 42,
              .hsync_pulse_width = 12,
              .hsync_front_porch = 42,
              .vsync_back_porch = 8,
              .vsync_pulse_width = 2,
              .vsync_front_porch = 166,
          },
      .flags.use_dma2d = true,
  };

  st7701_vendor_config_t vendor_config = {
      .init_cmds = lcd_cmd,
      .init_cmds_size = sizeof(lcd_cmd) / sizeof(st7701_lcd_init_cmd_t),
      .mipi_config =
          {
              .dsi_bus = mipi_dsi_bus,
              .dpi_config = &dpi_config,
          },
      .flags = {
          .use_mipi_interface = 1,
      }};

  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = GPIO_NUM_5,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .vendor_config = &vendor_config,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io, &panel_config, &disp_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(disp_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(disp_panel));

  esp_lcd_dpi_panel_event_callbacks_t cbs = {
#if LVGL_PORT_AVOID_TEAR_MODE
      .on_refresh_done = mipi_dsi_lcd_on_vsync_event,
#else
      .on_color_trans_done = mipi_dsi_lcd_on_vsync_event,
#endif
  };
  ESP_ERROR_CHECK(
      esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, NULL));

  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  esp_lcd_touch_handle_t tp_handle = NULL;
  esp_lcd_panel_io_i2c_config_t tp_io_config =
      ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  tp_io_config.scl_speed_hz = 100000;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle));

  const esp_lcd_touch_config_t tp_cfg = {
      .x_max = BSP_LCD_H_RES,
      .y_max = BSP_LCD_V_RES,
      .rst_gpio_num = BSP_LCD_TOUCH_RST,
      .int_gpio_num = BSP_LCD_TOUCH_INT,
      .levels =
          {
              .reset = 0,
              .interrupt = 0,
          },
      .flags =
          {
              .swap_xy = 0,
              .mirror_x = 0,
              .mirror_y = 0,
          },
  };
  ESP_ERROR_CHECK(
      esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle));

  lvgl_port_interface_t interface = dpi_config.flags.use_dma2d
                                        ? LVGL_PORT_INTERFACE_MIPI_DSI_DMA
                                        : LVGL_PORT_INTERFACE_MIPI_DSI_NO_DMA;
  ESP_ERROR_CHECK(lvgl_port_init(disp_panel, tp_handle, interface));

  bsp_display_brightness_set(100);

  boot_guider_ui();
  bsp_display_backlight_on();

  // Khởi tạo timer để kiểm tra screen timeout
  const esp_timer_create_args_t screen_timeout_timer_args = {
      .callback = &screen_timeout_timer_cb, .name = "screen_timeout"};
  ESP_ERROR_CHECK(
      esp_timer_create(&screen_timeout_timer_args, &screen_timeout_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(screen_timeout_timer,
                                           1000000)); // Check every 1 second
  ESP_LOGI(TAG, "Screen timeout timer started (30 seconds)");
}

void custom_init(lv_ui *ui) {
  if (ui == NULL) {
    return;
  }

  // Khởi động kênh nhận dữ liệu từ host (USB CDC)
  usb_comm_start();

  // Đăng ký event callback để bật màn hình ngay khi có touch
  lv_obj_t *screen = lv_display_get_screen_active(lv_display_get_default());
  if (screen != NULL) {
    lv_obj_add_event_cb(screen, screen_touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, screen_touch_event_cb, LV_EVENT_PRESSING, NULL);
  }
}
