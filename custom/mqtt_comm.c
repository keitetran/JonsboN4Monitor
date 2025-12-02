#include "mqtt_comm.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifdef CONFIG_MQTT_ENABLE

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>

#define TAG "mqtt_comm"

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_started = false;

/**
 * MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to broker");
            if (event->session_present) {
                ESP_LOGI(TAG, "  Session present: 1");
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            if (event->error_handle) {
                ESP_LOGW(TAG, "  Disconnect reason: %d", event->error_handle->connect_return_code);
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received, topic=%.*s, data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);
            // TODO: Parse data and update labels
            break;
        case MQTT_EVENT_ERROR:
            if (event->error_handle) {
                ESP_LOGE(TAG, "MQTT error: error_type=%d", event->error_handle->error_type);
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                    ESP_LOGE(TAG, "  ESP-TLS error: %d", event->error_handle->esp_tls_last_esp_err);
                } else if (event->error_handle->esp_transport_sock_errno) {
                    ESP_LOGE(TAG, "  Transport socket error: %d", event->error_handle->esp_transport_sock_errno);
                } else if (event->error_handle->esp_tls_stack_err) {
                    ESP_LOGE(TAG, "  TLS stack error: %d", event->error_handle->esp_tls_stack_err);
                } else {
                    ESP_LOGE(TAG, "  Connection refused or other error");
                }
            } else {
                ESP_LOGE(TAG, "MQTT error: error_handle is NULL");
            }
            break;
        default:
            ESP_LOGI(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}

void mqtt_comm_start(void) {
    ESP_LOGI(TAG, "mqtt_comm_start() called");
    
    if (s_mqtt_started) {
        ESP_LOGW(TAG, "MQTT client already started, skipping");
        return;
    }
    s_mqtt_started = true;

    ESP_LOGI(TAG, "Initializing MQTT client...");

    // Build MQTT broker URI from config
    char mqtt_broker_uri[64];
    snprintf(mqtt_broker_uri, sizeof(mqtt_broker_uri), 
             "mqtt://%s:%d", CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_BROKER_PORT);

    ESP_LOGI(TAG, "MQTT Broker URI: %s", mqtt_broker_uri);
    ESP_LOGI(TAG, "MQTT Client ID: %s", CONFIG_MQTT_CLIENT_ID);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_broker_uri,
        .credentials.client_id = CONFIG_MQTT_CLIENT_ID,
    };

    // Set username and password if configured
    if (strlen(CONFIG_MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = CONFIG_MQTT_USERNAME;
        ESP_LOGI(TAG, "MQTT Username: %s", CONFIG_MQTT_USERNAME);
        if (strlen(CONFIG_MQTT_PASSWORD) > 0) {
            mqtt_cfg.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
            ESP_LOGI(TAG, "MQTT Password: ****");
        }
    }

    // Create MQTT client
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        s_mqtt_started = false;
        return;
    }

    // Register event handler
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, 
                                    mqtt_event_handler, NULL);

    // Start MQTT client (this will trigger connection in background)
    ESP_LOGI(TAG, "Starting MQTT client (connection will happen in background)...");
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s (0x%x)", esp_err_to_name(ret), ret);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_mqtt_started = false;
        return;
    }

    ESP_LOGI(TAG, "MQTT client start() returned OK - waiting for connection events...");
}

#else

void mqtt_comm_start(void) {
    ESP_LOGW("mqtt_comm", "mqtt_comm_start() called but CONFIG_MQTT_ENABLE is not defined!");
    // No-op when MQTT is disabled
}

#endif // CONFIG_MQTT_ENABLE

