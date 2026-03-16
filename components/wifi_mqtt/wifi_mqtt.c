#include "wifi_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdlib.h>

#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASS"

#define MQTT_BROKER_URI "mqtt://192.168.1.100" // Replace with real IP or hostname
#define MQTT_TOPIC_TEMP "brew/sensor/temp"
#define MQTT_TOPIC_STATUS "brew/cooker/status"

#define WIFI_ENABLE GPIO_NUM_3
#define WIFI_ANT_CONFIG GPIO_NUM_14

static const char *TAG = "WIFI_MQTT";
static float latest_temperature = -1.0f; // -1 means invalid/unknown
static bool mqtt_connected = false;
static esp_mqtt_client_handle_t mqtt_client;

static void configure_antenna(void) {
    ESP_LOGI(TAG, "Configuring Xiao ESP32-C6 antenna");
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << WIFI_ENABLE) | (1ULL << WIFI_ANT_CONFIG),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    gpio_set_level(WIFI_ENABLE, 0);       // Activate RF switch control
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(WIFI_ANT_CONFIG, 0);   // Use external antenna (0 = external usually for Xiao C6)
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_TEMP, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_DATA:
        if (strncmp(event->topic, MQTT_TOPIC_TEMP, event->topic_len) == 0) {
            char temp_str[16];
            int len = event->data_len < 15 ? event->data_len : 15;
            memcpy(temp_str, event->data, len);
            temp_str[len] = '\0';
            latest_temperature = atof(temp_str);
            ESP_LOGI(TAG, "Received Temperature: %.2f", latest_temperature);
        }
        break;
    default:
        break;
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        
        // Start MQTT when WiFi is connected
        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = MQTT_BROKER_URI,
        };
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqtt_client);
    }
}

void wifi_mqtt_init(void) {
    configure_antenna();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

bool is_mqtt_connected(void) {
    return mqtt_connected;
}

float get_latest_temperature(void) {
    return latest_temperature;
}

void mqtt_publish_status(float current_temp, float target_temp, int stage, int phase, int time_left_s) {
    if (!mqtt_connected || mqtt_client == NULL) return;

    char payload[128];
    snprintf(payload, sizeof(payload), "{\"current_temp\":%.2f, \"target_temp\":%.2f, \"power_stage\":%d, \"phase\":%d, \"time_left_s\":%d}",
             current_temp, target_temp, stage, phase, time_left_s);

    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, payload, 0, 1, 0);
}
