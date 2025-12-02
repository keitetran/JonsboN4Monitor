#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#if defined(CONFIG_OTA_ENABLE) && defined(ESP_PLATFORM)

#include "esp_err.h"

/**
 * @brief OTA update status
 */
typedef enum {
  OTA_STATUS_IDLE = 0,    ///< OTA idle, no update in progress
  OTA_STATUS_CHECKING,    ///< Checking for updates
  OTA_STATUS_DOWNLOADING, ///< Downloading firmware
  OTA_STATUS_VERIFYING,   ///< Verifying firmware
  OTA_STATUS_SUCCESS,     ///< Update successful
  OTA_STATUS_FAILED,      ///< Update failed
} ota_status_t;

/**
 * @brief OTA update callback function type
 *
 * @param status Current OTA status
 * @param progress Progress percentage (0-100)
 * @param error_code Error code if status is OTA_STATUS_FAILED
 */
typedef void (*ota_callback_t)(ota_status_t status, int progress,
                               int error_code);

/**
 * @brief Initialize OTA update module
 *
 * This function initializes the OTA update system. Should be called
 * after WiFi is connected.
 *
 * @return
 *    - ESP_OK: Success
 *    - ESP_ERR_INVALID_STATE: Already initialized
 *    - ESP_FAIL: Initialization failed
 */
esp_err_t ota_update_init(void);

/**
 * @brief Start OTA update from URL
 *
 * Downloads and installs firmware from the specified URL.
 * The update runs in a separate task and will reboot the device
 * when complete if successful.
 *
 * @param url URL to download firmware from (HTTP or HTTPS)
 * @param callback Optional callback function to receive status updates
 * @return
 *    - ESP_OK: Update started successfully
 *    - ESP_ERR_INVALID_STATE: OTA not initialized or update already in progress
 *    - ESP_ERR_INVALID_ARG: Invalid URL
 *    - ESP_FAIL: Failed to start update
 */
esp_err_t ota_update_start(const char *url, ota_callback_t callback);

/**
 * @brief Get current OTA status
 *
 * @return Current OTA status
 */
ota_status_t ota_get_status(void);

/**
 * @brief Get OTA update progress
 *
 * @return Progress percentage (0-100), or -1 if not updating
 */
int ota_get_progress(void);

/**
 * @brief Check for updates automatically
 *
 * Checks the configured OTA URL for new firmware version.
 * If a new version is available, starts the update automatically.
 *
 * @param callback Optional callback function to receive status updates
 * @return
 *    - ESP_OK: Check started successfully
 *    - ESP_ERR_INVALID_STATE: OTA not initialized
 *    - ESP_FAIL: Failed to start check
 */
esp_err_t ota_check_for_updates(ota_callback_t callback);

/**
 * @brief Deinitialize OTA update module
 *
 * @return
 *    - ESP_OK: Success
 */
esp_err_t ota_update_deinit(void);

#endif // CONFIG_OTA_ENABLE
