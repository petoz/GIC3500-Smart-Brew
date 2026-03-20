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

#include "nvs.h"

#define AP_SSID        "GIC3500-Config"
#define NVS_NAMESPACE  "gic3500_cfg"

static char wifi_ssid[64] = {0};
static char wifi_pass[64] = {0};
static char mqtt_ip[64]   = {0};
static char mqtt_user[64] = {0};
static char mqtt_pass[64] = {0};

static const char *MQTT_TOPIC_TEMP   = "brew/sensor/temp";
static const char *MQTT_TOPIC_STATUS = "brew/cooker/status";

#define WIFI_ENABLE GPIO_NUM_3
#define WIFI_ANT_CONFIG GPIO_NUM_14

static const char *TAG = "WIFI_MQTT";
static float latest_temperature = -1.0f; // -1 means invalid/unknown
static bool mqtt_connected = false;
static bool mqtt_log_enabled = false;
static esp_mqtt_client_handle_t mqtt_client;
static int s_retry_num = 0;
#define ESP_MAXIMUM_RETRY  5

static vprintf_like_t default_logger;
static int custom_logger(const char *fmt, va_list ap) {
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    if (len > 0 && mqtt_log_enabled && mqtt_connected && mqtt_client) {
        // Prevent recursive logging from wifi or mqtt tasks themselves
        if (!strstr(buf, "mqtt_client") && !strstr(buf, "WIFI_MQTT") && !strstr(buf, "wifi")) {
            esp_mqtt_client_publish(mqtt_client, "brew/cooker/log", buf, len, 0, 0);
        }
    }
    return default_logger(fmt, ap);
}

void enable_mqtt_logging(bool enable) {
    mqtt_log_enabled = enable;
}

bool get_mqtt_logging(void) {
    return mqtt_log_enabled;
}

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
    gpio_set_level(WIFI_ANT_CONFIG, 0);   // Use external antenna 0 = internal, 1 = external
    ESP_LOGI(TAG, "Antenna configured: %s", gpio_get_level(WIFI_ANT_CONFIG) == 0 ? "Internal" : "External");
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

static void start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting AP Mode for configuration...");
    
    // Stop any existing Wi-Fi running state
    esp_wifi_stop();
    
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP Mode started: %s", AP_SSID);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, ESP_MAXIMUM_RETRY);
        } else {
            ESP_LOGW(TAG, "Failed to connect to STA %.0s. Falling back to AP mode.", wifi_ssid); // string truncated via .0s if not set
            esp_wifi_stop();
            start_ap_mode();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        
        char broker_uri[128];
        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s", mqtt_ip);
        
        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = broker_uri,
        };
        if (strlen(mqtt_user) > 0) {
            mqtt_cfg.credentials.username = mqtt_user;
        }
        if (strlen(mqtt_pass) > 0) {
            mqtt_cfg.credentials.authentication.password = mqtt_pass;
        }
        
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqtt_client);
    }
}

static void load_nvs_config(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    size_t required_size = 0;
    
    // Read SSID
    err = nvs_get_str(my_handle, "wifi_ssid", NULL, &required_size);
    if (err == ESP_OK) nvs_get_str(my_handle, "wifi_ssid", wifi_ssid, &required_size);
    
    // Read PASS
    err = nvs_get_str(my_handle, "wifi_pass", NULL, &required_size);
    if (err == ESP_OK) nvs_get_str(my_handle, "wifi_pass", wifi_pass, &required_size);

    // Read MQTT_IP
    err = nvs_get_str(my_handle, "mqtt_ip", NULL, &required_size);
    if (err == ESP_OK) nvs_get_str(my_handle, "mqtt_ip", mqtt_ip, &required_size);

    // Read MQTT_USER
    err = nvs_get_str(my_handle, "mqtt_user", NULL, &required_size);
    if (err == ESP_OK) nvs_get_str(my_handle, "mqtt_user", mqtt_user, &required_size);

    // Read MQTT_PASS
    err = nvs_get_str(my_handle, "mqtt_pass", NULL, &required_size);
    if (err == ESP_OK) nvs_get_str(my_handle, "mqtt_pass", mqtt_pass, &required_size);

    int32_t log_en = 0;
    err = nvs_get_i32(my_handle, "mqtt_log", &log_en);
    if (err == ESP_OK) mqtt_log_enabled = (log_en != 0);

    nvs_close(my_handle);
}

void wifi_mqtt_init(void) {
    configure_antenna();

    default_logger = esp_log_set_vprintf(custom_logger);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    load_nvs_config();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Default setup
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (strlen(wifi_ssid) == 0) {
        ESP_LOGI(TAG, "No Wi-Fi credentials found in NVS. Starting in AP mode.");
        start_ap_mode();
        return;
    }

    // Try STA mode if we have credentials
    esp_netif_create_default_wifi_sta();
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    memcpy(wifi_config.sta.ssid, wifi_ssid, strlen(wifi_ssid));
    memcpy(wifi_config.sta.password, wifi_pass, strlen(wifi_pass));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
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

void mqtt_publish_log(const char* message) {
    if (!mqtt_connected || mqtt_client == NULL) return;
    esp_mqtt_client_publish(mqtt_client, "brew/cooker/log", message, 0, 0, 0);
}
