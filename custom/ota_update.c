#include "ota_update.h"

#if defined(CONFIG_OTA_ENABLE) && defined(ESP_PLATFORM)

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>


#define TAG "ota_update"

// OTA state
static bool s_ota_initialized = false;
static ota_status_t s_ota_status = OTA_STATUS_IDLE;
static int s_ota_progress = 0;
static ota_callback_t s_ota_callback = NULL;
static TaskHandle_t s_ota_task_handle = NULL;

// Event group bits
#define OTA_UPDATE_BIT BIT0

/**
 * @brief HTTP event handler for OTA download
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
             evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    // Progress will be calculated in ota_update_task using esp_https_ota
    // functions
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
    break;
  default:
    break;
  }
  return ESP_OK;
}

/**
 * @brief OTA update task
 */
static void ota_update_task(void *pvParameters) {
  const char *url = (const char *)pvParameters;
  esp_err_t ret = ESP_FAIL;

  s_ota_status = OTA_STATUS_DOWNLOADING;
  s_ota_progress = 0;

  if (s_ota_callback) {
    s_ota_callback(OTA_STATUS_DOWNLOADING, 0, 0);
  }

  ESP_LOGI(TAG, "Starting OTA update from: %s", url);

  esp_http_client_config_t config = {
      .url = url,
      .event_handler = http_event_handler,
      .keep_alive_enable = true,
      .timeout_ms = 30000,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &config,
  };

  esp_https_ota_handle_t https_ota_handle = NULL;
  ret = esp_https_ota_begin(&ota_config, &https_ota_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed: %s", esp_err_to_name(ret));
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ret);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  esp_app_desc_t app_desc;
  ret = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s",
             esp_err_to_name(ret));
    esp_https_ota_abort(https_ota_handle);
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ret);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "New firmware version: %s", app_desc.version);
  ESP_LOGI(TAG, "New firmware project: %s", app_desc.project_name);

  // Get current app description
  const esp_app_desc_t *running_app_info = esp_app_get_description();
  ESP_LOGI(TAG, "Current firmware version: %s", running_app_info->version);

  // Check if version is different (optional - can skip if always want to
  // update)
  if (strcmp(app_desc.version, running_app_info->version) == 0) {
    ESP_LOGW(TAG, "New firmware version is the same as current version");
    // Continue anyway - user might want to reflash same version
  }

  s_ota_status = OTA_STATUS_DOWNLOADING;

  while (1) {
    ret = esp_https_ota_perform(https_ota_handle);
    if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      break;
    }

    // Update progress
    int image_length = esp_https_ota_get_image_len_read(https_ota_handle);
    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    if (image_size > 0) {
      s_ota_progress = (int)((image_length * 100) / image_size);
      if (s_ota_callback) {
        s_ota_callback(OTA_STATUS_DOWNLOADING, s_ota_progress, 0);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "OTA update successful");
    s_ota_status = OTA_STATUS_VERIFYING;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_VERIFYING, 100, 0);
    }

    ret = esp_https_ota_finish(https_ota_handle);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "OTA update finished successfully, rebooting...");
      s_ota_status = OTA_STATUS_SUCCESS;
      if (s_ota_callback) {
        s_ota_callback(OTA_STATUS_SUCCESS, 100, 0);
      }

      // Wait a bit for callback to complete
      vTaskDelay(pdMS_TO_TICKS(1000));

      // Reboot to new firmware
      esp_restart();
    } else {
      ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(ret));
      s_ota_status = OTA_STATUS_FAILED;
      if (s_ota_callback) {
        s_ota_callback(OTA_STATUS_FAILED, 0, ret);
      }
    }
  } else {
    ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
    esp_https_ota_abort(https_ota_handle);
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ret);
    }
  }

  s_ota_task_handle = NULL;
  vTaskDelete(NULL);
}

/**
 * @brief Check for updates task
 */
static void ota_check_task(void *pvParameters) {
  const char *base_url = (const char *)pvParameters;
  char version_url[256];
  char firmware_url[256];

  s_ota_status = OTA_STATUS_CHECKING;
  if (s_ota_callback) {
    s_ota_callback(OTA_STATUS_CHECKING, 0, 0);
  }

  // Build version URL
  snprintf(version_url, sizeof(version_url), "%s/version", base_url);

  ESP_LOGI(TAG, "Checking for updates at: %s", version_url);

  esp_http_client_config_t config = {
      .url = version_url,
      .timeout_ms = 5000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ESP_FAIL);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, err);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  
  ESP_LOGI(TAG, "HTTP Status Code: %d, Content-Length: %d", status_code, content_length);
  
  if (status_code != 200) {
    ESP_LOGE(TAG, "HTTP request failed with status code: %d", status_code);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ESP_FAIL);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  
  // Handle case where Content-Length is not provided (use fixed buffer size)
  int buffer_size = (content_length > 0) ? content_length : 512;
  if (buffer_size > 2048) {
    buffer_size = 2048; // Limit max buffer size
  }
  
  char *buffer = malloc(buffer_size + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for version info");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ESP_ERR_NO_MEM);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // Read response data
  int data_read = 0;
  int total_read = 0;
  
  // Read in chunks if content_length is unknown or large
  while (total_read < buffer_size - 1) {
    data_read = esp_http_client_read(client, buffer + total_read, buffer_size - total_read - 1);
    if (data_read <= 0) {
      break; // No more data or error
    }
    total_read += data_read;
    
    // If we got less than requested, we're done
    if (data_read < (buffer_size - total_read - 1)) {
      break;
    }
  }
  
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (total_read <= 0) {
    ESP_LOGE(TAG, "Failed to read version info (read %d bytes)", total_read);
    free(buffer);
    s_ota_status = OTA_STATUS_FAILED;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_FAILED, 0, ESP_FAIL);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  
  ESP_LOGI(TAG, "Read %d bytes from server", total_read);

  buffer[total_read] = '\0';
  
  // Log received data for debugging
  ESP_LOGD(TAG, "Received response: %s", buffer);

  // Parse JSON (simple parsing - look for "version" field)
  // For full JSON parsing, consider using cJSON library
  const char *version_str = strstr(buffer, "\"version\"");
  if (version_str == NULL) {
    ESP_LOGW(TAG, "Version info not found in response");
    free(buffer);
    s_ota_status = OTA_STATUS_IDLE;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_IDLE, 0, 0);
    }
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // Extract version (simple extraction)
  char new_version[32] = {0};
  const char *version_start = strchr(version_str, '"');
  if (version_start) {
    version_start++; // Skip first quote
    const char *version_end = strchr(version_start, '"');
    if (version_end) {
      int len = version_end - version_start;
      if (len < sizeof(new_version)) {
        strncpy(new_version, version_start, len);
        new_version[len] = '\0';
      }
    }
  }

  free(buffer);

  // Get current version
  const esp_app_desc_t *running_app_info = esp_app_get_description();

  ESP_LOGI(TAG, "Current version: %s", running_app_info->version);
  ESP_LOGI(TAG, "Available version: %s", new_version);

  // Compare versions (simple string compare - can be improved)
  if (strcmp(new_version, running_app_info->version) != 0) {
    ESP_LOGI(TAG, "New version available, starting update...");

    // Build firmware URL
    snprintf(firmware_url, sizeof(firmware_url), "%s/firmware.bin", base_url);

    // Start update
    ota_update_start(firmware_url, s_ota_callback);
  } else {
    ESP_LOGI(TAG, "Firmware is up to date");
    s_ota_status = OTA_STATUS_IDLE;
    if (s_ota_callback) {
      s_ota_callback(OTA_STATUS_IDLE, 0, 0);
    }
  }

  s_ota_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t ota_update_init(void) {
  if (s_ota_initialized) {
    ESP_LOGW(TAG, "OTA already initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Initializing OTA update module");

  s_ota_initialized = true;
  s_ota_status = OTA_STATUS_IDLE;
  s_ota_progress = 0;

  return ESP_OK;
}

esp_err_t ota_update_start(const char *url, ota_callback_t callback) {
  if (!s_ota_initialized) {
    ESP_LOGE(TAG, "OTA not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (s_ota_task_handle != NULL) {
    ESP_LOGW(TAG, "OTA update already in progress");
    return ESP_ERR_INVALID_STATE;
  }

  if (url == NULL || strlen(url) == 0) {
    ESP_LOGE(TAG, "Invalid URL");
    return ESP_ERR_INVALID_ARG;
  }

  s_ota_callback = callback;

  // Create task for OTA update
  BaseType_t ret = xTaskCreate(ota_update_task, "ota_update", 8192, (void *)url,
                               5, &s_ota_task_handle);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA task");
    return ESP_FAIL;
  }

  return ESP_OK;
}

ota_status_t ota_get_status(void) { return s_ota_status; }

int ota_get_progress(void) {
  if (s_ota_status == OTA_STATUS_DOWNLOADING ||
      s_ota_status == OTA_STATUS_VERIFYING) {
    return s_ota_progress;
  }
  return -1;
}

esp_err_t ota_check_for_updates(ota_callback_t callback) {
  if (!s_ota_initialized) {
    ESP_LOGE(TAG, "OTA not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (s_ota_task_handle != NULL) {
    ESP_LOGW(TAG, "OTA operation already in progress");
    return ESP_ERR_INVALID_STATE;
  }

  const char *ota_url = CONFIG_OTA_UPDATE_URL;
  if (ota_url == NULL || strlen(ota_url) == 0) {
    ESP_LOGE(TAG, "OTA URL not configured");
    return ESP_ERR_INVALID_STATE;
  }

  s_ota_callback = callback;

  // Create task for checking updates
  BaseType_t ret = xTaskCreate(ota_check_task, "ota_check", 4096,
                               (void *)ota_url, 5, &s_ota_task_handle);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA check task");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t ota_update_deinit(void) {
  if (!s_ota_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  // Wait for task to complete if running
  if (s_ota_task_handle != NULL) {
    ESP_LOGW(TAG, "OTA task still running, waiting...");
    // Note: In production, you might want to abort the task
    // For now, just wait
    while (s_ota_task_handle != NULL) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  s_ota_initialized = false;
  s_ota_status = OTA_STATUS_IDLE;
  s_ota_progress = 0;
  s_ota_callback = NULL;

  return ESP_OK;
}

#endif // CONFIG_OTA_ENABLE
