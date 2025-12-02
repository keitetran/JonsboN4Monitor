/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "soc/soc_caps.h"

#if SOC_LCDCAM_RGB_LCD_SUPPORTED
#include "esp_lcd_panel_rgb.h"
#endif
#if SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"         // NOLINT
#include "lvgl_private.h" // NOLINT

#if CONFIG_IDF_TARGET_ESP32P4
#include "driver/ppa.h"
#include "esp_private/esp_cache_private.h"

#include "lvgl_port_v9.h"

#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))
#define BLOCK_SIZE_SMALL (32)
#define BLOCK_SIZE_LARGE (256)

static const char *TAG = "lv_port";

typedef struct {
  esp_lcd_panel_handle_t lcd_handle;
  esp_lcd_touch_handle_t tp_handle;
  bool is_init;
} lvgl_port_task_param_t;

typedef esp_err_t (*get_lcd_frame_buffer_cb_t)(esp_lcd_panel_handle_t panel,
                                               uint32_t fb_num, void **fb0,
                                               ...);

#if LVGL_PORT_PPA_ROTATION_ENABLE
static ppa_client_handle_t ppa_srm_handle = NULL;
static size_t data_cache_line_size = 0;
#endif

static SemaphoreHandle_t lvgl_mux; // LVGL mutex
static TaskHandle_t lvgl_task_handle = NULL;
static lvgl_port_interface_t lvgl_port_interface = LVGL_PORT_INTERFACE_RGB;

#if LVGL_PORT_AVOID_TEAR_ENABLE
static get_lcd_frame_buffer_cb_t lvgl_get_lcd_frame_buffer = NULL;
#endif

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
/**
 * Lấy frame buffer tiếp theo để sử dụng cho rotation.
 * Luân phiên giữa 2 frame buffers để tránh conflict.
 *
 * @param panel_handle Handle của LCD panel
 * @return Con trỏ đến frame buffer tiếp theo
 */
static void *get_next_frame_buffer(esp_lcd_panel_handle_t panel_handle) {
  static void *next_fb = NULL;
  static void *fb[2] = {NULL};
  if (next_fb == NULL) {
    ESP_ERROR_CHECK(lvgl_get_lcd_frame_buffer(panel_handle, 2, &fb[0], &fb[1]));
    next_fb = fb[1];
  } else {
    next_fb = (next_fb == fb[0]) ? fb[1] : fb[0];
  }
  return next_fb;
}

#if !LVGL_PORT_PPA_ROTATION_ENABLE
/**
 * Xoay hình ảnh theo góc rotation (90, 180, 270 độ).
 * Sử dụng block-based rotation để tối ưu cache performance.
 * Hỗ trợ RGB565 (16-bit) và RGB888 (24-bit).
 *
 * @param src Buffer nguồn chứa dữ liệu pixel
 * @param dst Buffer đích để lưu kết quả sau khi xoay
 * @param width Chiều rộng hình ảnh nguồn
 * @param height Chiều cao hình ảnh nguồn
 * @param rotation Góc xoay (90, 180, hoặc 270 độ)
 * @param bpp Bits per pixel (16 hoặc 24)
 */
static void rotate_image(const void *src, void *dst, int width, int height,
                         int rotation, int bpp) {
  int bytes_per_pixel = bpp / 8;
  int block_w =
      rotation == 90 || rotation == 270 ? BLOCK_SIZE_SMALL : BLOCK_SIZE_LARGE;
  int block_h =
      rotation == 90 || rotation == 270 ? BLOCK_SIZE_LARGE : BLOCK_SIZE_SMALL;

  for (int i = 0; i < height; i += block_h) {
    int max_height = i + block_h > height ? height : i + block_h;

    for (int j = 0; j < width; j += block_w) {
      int max_width = j + block_w > width ? width : j + block_w;

      for (int x = i; x < max_height; x++) {
        for (int y = j; y < max_width; y++) {
          void *src_pixel = (uint8_t *)src + (x * width + y) * bytes_per_pixel;
          void *dst_pixel;

          switch (rotation) {
          case 270:
            dst_pixel = (uint8_t *)dst +
                        ((width - 1 - y) * height + x) * bytes_per_pixel;
            break;
          case 180:
            dst_pixel =
                (uint8_t *)dst +
                ((height - 1 - x) * width + (width - 1 - y)) * bytes_per_pixel;
            break;
          case 90:
            dst_pixel = (uint8_t *)dst +
                        (y * height + (height - 1 - x)) * bytes_per_pixel;
            break;
          default:
            return;
          }

          if (bpp == 16) {
            *(uint16_t *)dst_pixel = *(uint16_t *)src_pixel;
          } else if (bpp == 24) {
            ((uint8_t *)dst_pixel)[0] = ((uint8_t *)src_pixel)[0];
            ((uint8_t *)dst_pixel)[1] = ((uint8_t *)src_pixel)[1];
            ((uint8_t *)dst_pixel)[2] = ((uint8_t *)src_pixel)[2];
          }
        }
      }
    }
  }
}
#endif

/**
 * Copy và xoay một vùng pixel từ buffer nguồn sang buffer đích.
 * Sử dụng PPA (Pixel Processing Accelerator) nếu được enable, ngược lại dùng
 * software rotation. Hàm này được đặt trong IRAM để đảm bảo performance.
 *
 * @param from Buffer nguồn chứa pixel data
 * @param to Buffer đích để lưu kết quả
 * @param x_start Tọa độ X bắt đầu của vùng cần copy
 * @param y_start Tọa độ Y bắt đầu của vùng cần copy
 * @param x_end Tọa độ X kết thúc của vùng cần copy
 * @param y_end Tọa độ Y kết thúc của vùng cần copy
 * @param w Chiều rộng của toàn bộ hình ảnh
 * @param h Chiều cao của toàn bộ hình ảnh
 * @param rotation Góc xoay (90, 180, hoặc 270 độ)
 */
IRAM_ATTR static void rotate_copy_pixel(const uint16_t *from, uint16_t *to,
                                        uint16_t x_start, uint16_t y_start,
                                        uint16_t x_end, uint16_t y_end,
                                        uint16_t w, uint16_t h,
                                        uint16_t rotation) {
#if LVGL_PORT_PPA_ROTATION_ENABLE
  ppa_srm_rotation_angle_t ppa_rotation;
  int x_offset = 0, y_offset = 0;

  // Determine rotation settings once and reuse
  switch (rotation) {
  case 90:
    ppa_rotation = PPA_SRM_ROTATION_ANGLE_270;
    x_offset = h - y_end - 1;
    y_offset = x_start;
    break;
  case 180:
    ppa_rotation = PPA_SRM_ROTATION_ANGLE_180;
    x_offset = w - x_end - 1;
    y_offset = h - y_end - 1;
    break;
  case 270:
    ppa_rotation = PPA_SRM_ROTATION_ANGLE_90;
    x_offset = y_start;
    y_offset = w - x_end - 1;
    break;
  default:
    ppa_rotation = PPA_SRM_ROTATION_ANGLE_0;
    break;
  }

  // Fill operation config for PPA rotation, without recalculating each time
  ppa_srm_oper_config_t oper_config = {
      .in.buffer = from,
      .in.pic_w = w,
      .in.pic_h = h,
      .in.block_w = x_end - x_start + 1,
      .in.block_h = y_end - y_start + 1,
      .in.block_offset_x = x_start,
      .in.block_offset_y = y_start,
      .in.srm_cm = (LV_COLOR_DEPTH == 24) ? PPA_SRM_COLOR_MODE_RGB888
                                          : PPA_SRM_COLOR_MODE_RGB565,

      .out.buffer = to,
      .out.buffer_size =
          ALIGN_UP_BY(sizeof(lv_color_t) * w * h, data_cache_line_size),
      .out.pic_w = (ppa_rotation == PPA_SRM_ROTATION_ANGLE_90 ||
                    ppa_rotation == PPA_SRM_ROTATION_ANGLE_270)
                       ? h
                       : w,
      .out.pic_h = (ppa_rotation == PPA_SRM_ROTATION_ANGLE_90 ||
                    ppa_rotation == PPA_SRM_ROTATION_ANGLE_270)
                       ? w
                       : h,
      .out.block_offset_x = x_offset,
      .out.block_offset_y = y_offset,
      .out.srm_cm = (LV_COLOR_DEPTH == 24) ? PPA_SRM_COLOR_MODE_RGB888
                                           : PPA_SRM_COLOR_MODE_RGB565,

      .rotation_angle = ppa_rotation,
      .scale_x = 1.0,
      .scale_y = 1.0,
      .rgb_swap = 0,
      .byte_swap = 0,
      .mode = PPA_TRANS_MODE_BLOCKING,
  };

  ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &oper_config));

#else
  // Fallback: optimized transpose for non-PPA systems
  rotate_image(from, to, w, h, rotation, LV_COLOR_DEPTH);
#endif
}

#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#if LVGL_PORT_AVOID_TEAR_ENABLE

/**
 * Chuyển LCD panel sang sử dụng frame buffer mới.
 * Vẽ toàn bộ frame buffer lên màn hình.
 *
 * @param panel_handle Handle của LCD panel
 * @param fb Con trỏ đến frame buffer mới
 */
static void switch_lcd_frame_buffer_to(esp_lcd_panel_handle_t panel_handle,
                                       void *fb) {
  esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LVGL_PORT_H_RES,
                            LVGL_PORT_V_RES, fb);
}

#if LVGL_PORT_DIRECT_MODE
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
typedef struct {
  uint16_t inv_p;
  uint8_t inv_area_joined[LV_INV_BUF_SIZE];
  lv_area_t inv_areas[LV_INV_BUF_SIZE];
} lv_port_dirty_area_t;

typedef enum { FLUSH_STATUS_PART, FLUSH_STATUS_FULL } lv_port_flush_status_t;

typedef enum {
  FLUSH_PROBE_PART_COPY,
  FLUSH_PROBE_SKIP_COPY,
  FLUSH_PROBE_FULL_COPY,
} lv_port_flush_probe_t;

static lv_port_dirty_area_t dirty_area;

/**
 * Lưu thông tin dirty area (vùng cần refresh) từ LVGL display.
 * Sử dụng để track các vùng cần được copy khi tránh tearing effect.
 *
 * @param dirty_area Con trỏ đến struct chứa thông tin dirty area
 */
static void flush_dirty_save(lv_port_dirty_area_t *dirty_area) {
  lv_disp_t *disp = lv_refr_get_disp_refreshing();
  dirty_area->inv_p = disp->inv_p;
  for (int i = 0; i < disp->inv_p; i++) {
    dirty_area->inv_area_joined[i] = disp->inv_area_joined[i];
    dirty_area->inv_areas[i] = disp->inv_areas[i];
  }
}

/**
 * Probe dirty area để quyết định phương pháp copy (partial hoặc full).
 * Sử dụng để tránh tearing effect, chỉ hoạt động với LVGL direct-mode.
 * Chỉ trigger full copy nếu dirty area lớn hơn 50% màn hình.
 *
 * @param disp LVGL display handle
 * @return Kết quả probe: FLUSH_PROBE_PART_COPY, FLUSH_PROBE_SKIP_COPY, hoặc
 * FLUSH_PROBE_FULL_COPY
 */
static lv_port_flush_probe_t flush_copy_probe(lv_display_t *disp) {
  static lv_port_flush_status_t prev_status = FLUSH_STATUS_PART;
  lv_port_flush_status_t cur_status;
  lv_port_flush_probe_t probe_result;
  lv_disp_t *disp_refr = lv_refr_get_disp_refreshing();

  uint32_t flush_ver = 0;
  uint32_t flush_hor = 0;
  uint32_t total_dirty_area = 0;
  for (int i = 0; i < disp_refr->inv_p; i++) {
    if (disp_refr->inv_area_joined[i] == 0) {
      uint32_t ver =
          (disp_refr->inv_areas[i].y2 + 1 - disp_refr->inv_areas[i].y1);
      uint32_t hor =
          (disp_refr->inv_areas[i].x2 + 1 - disp_refr->inv_areas[i].x1);
      total_dirty_area += ver * hor;
      if (flush_ver == 0) {
        flush_ver = ver;
        flush_hor = hor;
      }
    }
  }
  /* Check if the current full screen refreshes */
  cur_status = ((flush_ver == disp->ver_res) && (flush_hor == disp->hor_res))
                   ? (FLUSH_STATUS_FULL)
                   : (FLUSH_STATUS_PART);

  // FIX: Chỉ trigger full copy nếu dirty area thực sự lớn (> 50% màn hình)
  // Tránh trigger full refresh khi chỉ update label nhỏ
  uint32_t screen_area = disp->hor_res * disp->ver_res;
  bool is_large_update = (total_dirty_area > (screen_area / 2));

  if (prev_status == FLUSH_STATUS_FULL) {
    if ((cur_status == FLUSH_STATUS_PART) && is_large_update) {
      // Chỉ trigger full copy nếu dirty area lớn
      probe_result = FLUSH_PROBE_FULL_COPY;
    } else {
      // Dirty area nhỏ, dùng partial copy
      probe_result = FLUSH_PROBE_PART_COPY;
    }
  } else {
    probe_result = FLUSH_PROBE_PART_COPY;
  }
  prev_status = cur_status;

  return probe_result;
}

/**
 * Lấy frame buffer tiếp theo để sử dụng cho flush operation.
 *
 * @param panel_handle Handle của LCD panel
 * @return Con trỏ đến frame buffer tiếp theo
 */
static inline void *flush_get_next_buf(void *panel_handle) {
  return get_next_frame_buffer(panel_handle);
}

/**
 * Copy các dirty area từ buffer nguồn sang buffer đích.
 * Sử dụng để tránh tearing effect, chỉ hoạt động với LVGL direct-mode.
 * Tự động xoay pixel nếu cần thiết.
 *
 * @param dst Buffer đích để copy dữ liệu
 * @param src Buffer nguồn chứa dữ liệu từ LVGL
 * @param dirty_area Thông tin về các vùng dirty cần copy
 */
static void flush_dirty_copy(void *dst, void *src,
                             lv_port_dirty_area_t *dirty_area) {
  lv_coord_t x_start, x_end, y_start, y_end;
  for (int i = 0; i < dirty_area->inv_p; i++) {
    /* Refresh the unjoined areas*/
    if (dirty_area->inv_area_joined[i] == 0) {
      x_start = dirty_area->inv_areas[i].x1;
      x_end = dirty_area->inv_areas[i].x2;
      y_start = dirty_area->inv_areas[i].y1;
      y_end = dirty_area->inv_areas[i].y2;

      rotate_copy_pixel(src, dst, x_start, y_start, x_end, y_end, LV_HOR_RES,
                        LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);
    }
  }
}

/**
 * Callback function được gọi bởi LVGL khi cần flush (vẽ) dữ liệu lên màn hình.
 * Hỗ trợ avoid tearing effect với direct-mode, sử dụng double/triple buffering.
 * Tự động xoay và copy dirty areas khi cần thiết.
 *
 * @param disp LVGL display handle
 * @param area Vùng cần flush (tọa độ x1, y1, x2, y2)
 * @param color_map Buffer chứa dữ liệu pixel cần vẽ
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *color_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;
  void *next_fb = NULL;
  lv_port_flush_probe_t probe_result = FLUSH_PROBE_PART_COPY;

  /* Action after last area refresh */
  if (lv_disp_flush_is_last(disp)) {
    /* Check if the `full_refresh` flag has been triggered */
    if (disp->render_mode == LV_DISPLAY_RENDER_MODE_FULL) {
      /* Reset flag */
      disp->render_mode = LV_DISPLAY_RENDER_MODE_DIRECT;

      // Rotate and copy data from the whole screen LVGL's buffer to the next
      // frame buffer
      next_fb = flush_get_next_buf(panel_handle);
      rotate_copy_pixel((uint16_t *)color_map, next_fb, offsetx1, offsety1,
                        offsetx2, offsety2, LV_HOR_RES, LV_VER_RES,
                        EXAMPLE_LVGL_PORT_ROTATION_DEGREE);

      /* Switch the current LCD frame buffer to `next_fb` */
      switch_lcd_frame_buffer_to(panel_handle, next_fb);

      /* Waiting for the current frame buffer to complete transmission */
      ulTaskNotifyValueClear(NULL, ULONG_MAX);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      /* Synchronously update the dirty area for another frame buffer */
      flush_dirty_copy(flush_get_next_buf(panel_handle), color_map,
                       &dirty_area);
      flush_get_next_buf(panel_handle);
    } else {
      /* Probe the copy method for the current dirty area */
      probe_result = flush_copy_probe(disp);

      // FIX: Disable full copy probe để tránh nhấp nháy khi update label nhỏ
      // Luôn dùng partial copy thay vì full refresh
      if (0 && probe_result == FLUSH_PROBE_FULL_COPY) {
        /* Save current dirty area for next frame buffer */
        flush_dirty_save(&dirty_area);

        /* Set LVGL full-refresh flag and set flush ready in advance */
        disp->render_mode = LV_DISPLAY_RENDER_MODE_FULL;
        disp->rendering_in_progress = false;
        lv_disp_flush_ready(disp);

        /* Force to refresh whole screen, and will invoke `flush_callback`
         * recursively */
        lv_refr_now(lv_refr_get_disp_refreshing());
      } else {
        /* Update current dirty area for next frame buffer */
        next_fb = flush_get_next_buf(panel_handle);
        flush_dirty_save(&dirty_area);
        flush_dirty_copy(next_fb, color_map, &dirty_area);

        /* Switch the current LCD frame buffer to `next_fb` */
        switch_lcd_frame_buffer_to(panel_handle, next_fb);

        /* Waiting for the current frame buffer to complete transmission */
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (probe_result == FLUSH_PROBE_PART_COPY) {
          /* Synchronously update the dirty area for another frame buffer */
          flush_dirty_save(&dirty_area);
          flush_dirty_copy(flush_get_next_buf(panel_handle), color_map,
                           &dirty_area);
          flush_get_next_buf(panel_handle);
        }
      }
    }
  }

  lv_disp_flush_ready(disp);
}

#else

/**
 * Callback function flush đơn giản cho non-direct mode.
 * Chỉ switch frame buffer và chờ sync signal.
 *
 * @param disp LVGL display handle
 * @param area Vùng cần flush
 * @param color_map Buffer chứa dữ liệu pixel
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *color_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

  /* Action after last area refresh */
  if (lv_disp_flush_is_last(disp)) {
    /* Switch the current LCD frame buffer to `color_map` */
    switch_lcd_frame_buffer_to(panel_handle, color_map);

    /* Waiting for the last frame buffer to complete transmission */
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }

  lv_disp_flush_ready(disp);
}
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_BUFFER_NUMS == 2

/**
 * Callback function flush cho full refresh mode với 2 buffers.
 * Switch frame buffer và chờ sync signal.
 *
 * @param disp LVGL display handle
 * @param area Vùng cần flush
 * @param color_map Buffer chứa dữ liệu pixel
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *color_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

  /* Switch the current LCD frame buffer to `color_map` */
  switch_lcd_frame_buffer_to(panel_handle, color_map);

  /* Waiting for the last frame buffer to complete transmission */
  ulTaskNotifyValueClear(NULL, ULONG_MAX);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  lv_disp_flush_ready(disp);
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_BUFFER_NUMS == 3

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
static void *lvgl_port_rgb_last_buf = NULL;
static void *lvgl_port_rgb_next_buf = NULL;
static void *lvgl_port_flush_next_buf = NULL;
#endif

/**
 * Callback function flush cho full refresh mode với 3 buffers.
 * Hỗ trợ rotation nếu được enable.
 *
 * @param disp LVGL display handle
 * @param area Vùng cần flush
 * @param color_map Buffer chứa dữ liệu pixel
 */
void flush_callback(lv_display_t *disp, const lv_area_t *area,
                    uint8_t *color_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;
  void *next_fb = get_next_frame_buffer(panel_handle);

  /* Rotate and copy dirty area from the current LVGL's buffer to the next LCD
   * frame buffer */
  rotate_copy_pixel((uint16_t *)color_map, next_fb, offsetx1, offsety1,
                    offsetx2, offsety2, LV_HOR_RES, LV_VER_RES,
                    EXAMPLE_LVGL_PORT_ROTATION_DEGREE);

  /* Switch the current LCD frame buffer to `next_fb` */
  switch_lcd_frame_buffer_to(panel_handle, next_fb);
#else
  if (disp->buf_act == disp->buf_1) {
    disp->buf_2->data = lvgl_port_flush_next_buf;
  } else {
    disp->buf_1->data = lvgl_port_flush_next_buf;
  }
  lvgl_port_flush_next_buf = color_map;

  /* Switch the current LCD frame buffer to `color_map` */
  switch_lcd_frame_buffer_to(panel_handle, color_map);

  lvgl_port_rgb_next_buf = color_map;
#endif

  lv_disp_flush_ready(disp);
}
#endif

#else

/**
 * Callback function flush mặc định (partial refresh mode).
 * Copy dữ liệu trực tiếp từ color_map lên LCD frame buffer.
 *
 * @param disp LVGL display handle
 * @param area Vùng cần flush
 * @param color_map Buffer chứa dữ liệu pixel
 */
void flush_callback(lv_display_t *disp, const lv_area_t *area,
                    uint8_t *color_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;

  /* Just copy data from the color map to the LCD frame buffer */
  esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1,
                            offsety2 + 1, color_map);

  if (lvgl_port_interface != LVGL_PORT_INTERFACE_MIPI_DSI_DMA) {
    lv_disp_flush_ready(disp);
  }
}

#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

/**
 * Khởi tạo LVGL display với LCD panel handle.
 * Cấp phát buffer cho LVGL, đăng ký flush callback và cấu hình render mode.
 * Hỗ trợ PPA rotation nếu được enable.
 *
 * @param panel_handle Handle của LCD panel đã được khởi tạo
 * @return Con trỏ đến LVGL display object, NULL nếu lỗi
 */
static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle) {
#if LVGL_PORT_PPA_ROTATION_ENABLE
  // Initialize the PPA
  ppa_client_config_t ppa_srm_config = {
      .oper_type = PPA_OPERATION_SRM,
  };
  ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
  ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM,
                                          &data_cache_line_size));
#endif

  assert(panel_handle);

  // alloc draw buffers used by LVGL
  void *buf1 = NULL;
  void *buf2 = NULL;
  int buffer_size = 0;

  ESP_LOGD(TAG, "Malloc memory for LVGL buffer");
#if LVGL_PORT_AVOID_TEAR_ENABLE
  // To avoid the tearing effect, we should use at least two frame buffers: one
  // for LVGL rendering and another for RGB output
  buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
#if (LVGL_PORT_LCD_BUFFER_NUMS == 3) &&                                        \
    (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0) && LVGL_PORT_FULL_REFRESH
  // With the usage of three buffers and full-refresh, we always have one buffer
  // available for rendering, eliminating the need to wait for the RGB's sync
  // signal
  ESP_ERROR_CHECK(lvgl_get_lcd_frame_buffer(
      panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2));
  lvgl_port_rgb_next_buf = lvgl_port_rgb_last_buf;
  lvgl_port_flush_next_buf = buf2;
#elif (LVGL_PORT_LCD_BUFFER_NUMS == 3) &&                                      \
    (EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0)
  // Here we are using three frame buffers, one for LVGL rendering, and the
  // other two for RGB driver (one of them is used for rotation)
  void *fbs[3];
  ESP_ERROR_CHECK(
      lvgl_get_lcd_frame_buffer(panel_handle, 3, &fbs[0], &fbs[1], &fbs[2]));
  buf1 = fbs[2];
#else
  ESP_ERROR_CHECK(lvgl_get_lcd_frame_buffer(panel_handle, 2, &buf1, &buf2));
#endif
#else
  // Normmaly, for RGB LCD, we just use one buffer for LVGL rendering
  buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
  buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t),
                          LVGL_PORT_BUFFER_MALLOC_CAPS);
  // buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
  // buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf1);
  ESP_LOGI(TAG, "LVGL buffer size: %dKB",
           buffer_size * sizeof(lv_color_t) / 1024);
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

  ESP_LOGD(TAG, "Register display driver to LVGL");
  lv_display_t *display = lv_display_create(
#if (EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 90) &&                               \
    (EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 270)
      LVGL_PORT_H_RES, LVGL_PORT_V_RES
#else
      LVGL_PORT_V_RES, LVGL_PORT_H_RES
#endif
  );

  lv_display_set_buffers(display, buf1, buf2, buffer_size * sizeof(lv_color_t),
#if LVGL_PORT_FULL_REFRESH
                         LV_DISPLAY_RENDER_MODE_FULL
#elif LVGL_PORT_DIRECT_MODE
                         LV_DISPLAY_RENDER_MODE_DIRECT
#else
                         LV_DISPLAY_RENDER_MODE_PARTIAL
#endif
  );
  lv_display_set_flush_cb(display, flush_callback);
  lv_display_set_user_data(display, panel_handle);

  return display;
}

/**
 * Callback function đọc dữ liệu từ touchpad.
 * Được gọi bởi LVGL input device driver để lấy thông tin touch.
 *
 * @param indev_drv LVGL input device driver handle
 * @param data Con trỏ đến struct chứa dữ liệu touch (tọa độ, trạng thái)
 */
static void touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data) {
  esp_lcd_touch_handle_t tp =
      (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev_drv);
  assert(tp);

  uint16_t touchpad_x;
  uint16_t touchpad_y;
  uint8_t touchpad_cnt = 0;
  /* Read data from touch controller into memory */
  esp_lcd_touch_read_data(tp);

  /* Read data from touch controller */
  bool touchpad_pressed = esp_lcd_touch_get_coordinates(
      tp, &touchpad_x, &touchpad_y, NULL, &touchpad_cnt, 1);
  if (touchpad_pressed && touchpad_cnt > 0) {
    data->point.x = touchpad_x;
    data->point.y = touchpad_y;
    data->state = LV_INDEV_STATE_PRESSED;

    ESP_LOGD(TAG, "Touch position: %d,%d", touchpad_x, touchpad_y);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

/**
 * Khởi tạo LVGL input device cho touchpad.
 * Đăng ký touchpad read callback và cấu hình input device type.
 *
 * @param tp Handle của touchpad đã được khởi tạo
 * @return Con trỏ đến LVGL input device object, NULL nếu lỗi
 */
static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp) {
  assert(tp);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*See below.*/
  lv_indev_set_user_data(indev, tp);
  lv_indev_set_read_cb(indev, touchpad_read); /*See below.*/

  return indev;
}

/**
 * Callback function tăng LVGL tick counter.
 * Được gọi định kỳ bởi esp_timer để cập nhật thời gian cho LVGL.
 *
 * @param arg Tham số callback (không sử dụng)
 */
static void tick_increment(void *arg) {
  /* Tell LVGL how many milliseconds have elapsed */
  lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

/**
 * Khởi tạo tick timer cho LVGL.
 * Tạo esp_timer định kỳ để gọi tick_increment callback.
 *
 * @return ESP_OK nếu thành công, ESP_ERR_* nếu lỗi
 */
static esp_err_t tick_init(void) {
  // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &tick_increment, .name = "LVGL tick"};
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  return esp_timer_start_periodic(lvgl_tick_timer,
                                  LVGL_PORT_TICK_PERIOD_MS * 1000);
}

/**
 * Task chính của LVGL port.
 * Khởi tạo LVGL, display, touchpad và chạy timer handler trong vòng lặp vô hạn.
 * Tự động điều chỉnh delay dựa trên thời gian xử lý của LVGL.
 *
 * @param arg Con trỏ đến lvgl_port_task_param_t chứa LCD và touchpad handles
 */
static void lvgl_port_task(void *arg) {
  ESP_LOGD(TAG, "Starting LVGL task");

  lvgl_port_task_param_t *param = (lvgl_port_task_param_t *)arg;

  lv_init();
  ESP_ERROR_CHECK(tick_init());

  lv_display_t *disp = display_init(param->lcd_handle);
  assert(disp);

  if (param->tp_handle) {
    lv_indev_t *indev = indev_init(param->tp_handle);
    assert(indev);

#if EXAMPLE_LVGL_PORT_ROTATION_90
    esp_lcd_touch_set_swap_xy(param->tp_handle, true);
    esp_lcd_touch_set_mirror_x(param->tp_handle, true);
#elif EXAMPLE_LVGL_PORT_ROTATION_180
    esp_lcd_touch_set_mirror_x(param->tp_handle, false);
    esp_lcd_touch_set_mirror_y(param->tp_handle, false);
#elif EXAMPLE_LVGL_PORT_ROTATION_270
    esp_lcd_touch_set_swap_xy(param->tp_handle, true);
    esp_lcd_touch_set_mirror_x(param->tp_handle, false);
    esp_lcd_touch_set_mirror_y(param->tp_handle, true);
#endif
  }

  param->is_init = true;

  uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
  while (1) {
    if (lvgl_port_lock(-1)) {
      task_delay_ms = lv_timer_handler();
      lvgl_port_unlock();
    }
    if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
      task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
      task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
  }
}

/**
 * Khởi tạo LVGL port với LCD panel và touchpad.
 * Tạo mutex, task và chờ cho đến khi LVGL được khởi tạo hoàn tất.
 *
 * @param lcd_handle Handle của LCD panel đã được khởi tạo
 * @param tp_handle Handle của touchpad (có thể NULL nếu không có touchpad)
 * @param interface Loại interface (RGB, MIPI DSI, etc.)
 * @return ESP_OK nếu thành công, ESP_ERR_* nếu lỗi
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                         esp_lcd_touch_handle_t tp_handle,
                         lvgl_port_interface_t interface) {
  lvgl_port_task_param_t lvgl_task_param = {
      .lcd_handle = lcd_handle, .tp_handle = tp_handle, .is_init = false};

  lvgl_port_interface = interface;
#if LVGL_PORT_AVOID_TEAR_ENABLE
  switch (interface) {
#if SOC_LCDCAM_RGB_LCD_SUPPORTED
  case LVGL_PORT_INTERFACE_RGB:
    lvgl_get_lcd_frame_buffer = esp_lcd_rgb_panel_get_frame_buffer;
    break;
#endif

#if SOC_MIPI_DSI_SUPPORTED
  case LVGL_PORT_INTERFACE_MIPI_DSI_DMA:
  case LVGL_PORT_INTERFACE_MIPI_DSI_NO_DMA:
    lvgl_get_lcd_frame_buffer = esp_lcd_dpi_panel_get_frame_buffer;
    break;
#endif

  default:
    ESP_LOGE(TAG, "Invalid interface type");
    return ESP_ERR_INVALID_ARG;
  }
#endif

  lvgl_mux = xSemaphoreCreateRecursiveMutex();
  assert(lvgl_mux);

  ESP_LOGI(TAG, "Create LVGL task");
  BaseType_t core_id =
      (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
  BaseType_t ret = xTaskCreatePinnedToCore(
      lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, &lvgl_task_param,
      LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LVGL task");
    return ESP_FAIL;
  }

  while (!lvgl_task_param.is_init) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  return ESP_OK;
}

/**
 * Lock LVGL mutex để đảm bảo thread-safe khi truy cập LVGL API.
 * Phải được gọi trước khi gọi bất kỳ LVGL API nào từ task khác.
 *
 * @param timeout_ms Timeout tính bằng milliseconds. -1 = chờ vô hạn.
 * @return true nếu lock thành công, false nếu timeout
 */
bool lvgl_port_lock(int timeout_ms) {
  assert(lvgl_mux && "lvgl_port_init must be called first");

  const TickType_t timeout_ticks =
      (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

/**
 * Unlock LVGL mutex sau khi hoàn tất truy cập LVGL API.
 * Phải được gọi sau mỗi lần gọi lvgl_port_lock().
 */
void lvgl_port_unlock(void) {
  assert(lvgl_mux && "lvgl_port_init must be called first");
  xSemaphoreGiveRecursive(lvgl_mux);
}

/**
 * Thông báo khi LCD VSYNC signal xảy ra (từ ISR).
 * Sử dụng để đồng bộ frame buffer switching và tránh tearing.
 *
 * @return true nếu cần yield task, false nếu không
 */
bool lvgl_port_notify_lcd_vsync(void) {
  BaseType_t need_yield = pdFALSE;
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) &&          \
    (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0)
  if (lvgl_port_rgb_next_buf != lvgl_port_rgb_last_buf) {
    lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf;
    lvgl_port_rgb_last_buf = lvgl_port_rgb_next_buf;
  }
#elif LVGL_PORT_AVOID_TEAR_ENABLE
  // Notify that the current LCD frame buffer has been transmitted
  if (lvgl_task_handle) {
    xTaskNotifyFromISR(lvgl_task_handle, ULONG_MAX, eNoAction, &need_yield);
  }
#else
  if (lvgl_port_interface == LVGL_PORT_INTERFACE_MIPI_DSI_DMA) {
    lv_display_t *disp = lv_disp_get_default();
    lv_disp_flush_ready(disp);
  }
#endif
  return (need_yield == pdTRUE);
}

#endif /* ESP_PLATFORM */
