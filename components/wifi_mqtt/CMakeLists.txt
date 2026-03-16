idf_component_register(SRCS "wifi_mqtt.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "mqtt" "esp_wifi" "nvs_flash" "esp_netif" "esp_event" "esp_driver_gpio")
